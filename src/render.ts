import Ffmpeg, { AudioVideoFilter, FilterSpecification } from "fluent-ffmpeg";
import Queue from "promise-queue";
import { connectFilters } from "./connectFilters";
import { getConversionFilterSpecs } from "./pixelFormatConversion";
import { HardwarePixelFormat } from "./pixelFormats";

import { getMetadata, notEmpty, parseNumber } from "./utils";

// Holy grail: ffmpeg -hwaccel vaapi -hwaccel_output_format vaapi -init_hw_device vaapi=intel:/dev/dri/renderD128 -init_hw_device opencl=ocl@intel -hwaccel_device intel -filter_hw_device ocl -i 1390.mkv -vf 'hwmap=derive_device=opencl,boxblur_opencl,hwmap=derive_device=vaapi:reverse=1' -c:v h264_vaapi ocl.mkv -y -v verbose
// https://lists.cinelerra-gg.org/pipermail/cin/2019-May/000650.html

/**
 * H.264:
 * 19 - "visually lossless"
 * 23 - great
 * 25 - pretty good
 * 28 - a bit dodgy
 */
const VAAPI_QP = 19;

const analyseQueue = new Queue(2);
const encodeQueue = new Queue(4);

type FilterPipeline = {
  inputPad: string;
  inputFormat: HardwarePixelFormat;
  filters: FilterSpecification[];
  outputFormat: HardwarePixelFormat;
  outputPad: string;
};

type InputConfiguration = {
  inputOptions: string[];
  filters: FilterSpecification[];
  outputFormat: HardwarePixelFormat;
  filterDeviceFormat: HardwarePixelFormat;
  openclMappedFromVaapi: boolean;
  outputPad: string;
};

type VaapiVendor = "INTEL" | "AMD" | null;

type HwAccel = "VAAPI" | "NVDEC" | null;

type RenderOptions = {
  encodeOnly: boolean;
  analyseOnly: boolean;
  start: string | null;
  duration: string | null;
  end: string | null;
  roll: number;
  pitch: number;
  yaw: number;
  width: number | null;
  height: number | null;
  upsample: number;
  filter: "vidstab" | "deshake" | "deshake_opencl" | "dewobble";
  stabilise: "none" | "fixed" | "smooth";
  stabiliseRadius: number;
  stabiliseBuffer: number;
  inputDfov: number;
  projection: "flat" | "fisheye";
  zoom: number;
  crop: string | null;
  hwAccel: HwAccel;
  vaapiVendor: VaapiVendor;
  openClPlatform: number | null;
  mapOpenClFromVaapi: boolean;
  encoder: string;
  debug: boolean;
};

const makeGeneratePadName = () => {
  let number: number = -1;
  return () => {
    number += 1;
    return `p${number}`;
  };
};

const getVaapiDeviceConfig = (
  vendor: VaapiVendor
): { initLine: string; deviceName: string } | null => {
  switch (vendor) {
    case "INTEL": {
      return {
        initLine: "intel_vaapi:,driver=iHD,kernel_driver=i915",
        deviceName: "intel_vaapi",
      };
    }
    case "AMD": {
      return {
        initLine: "amd_vaapi:,driver=radeonsi,kernel_driver=amdgpu",
        deviceName: "amd_vaapi",
      };
    }
    case null: {
      return null;
    }
    default: {
      throw new Error(`Unknown VAAPI vvendor: ${vendor}`);
    }
  }
};

const getOpenClDeviceConfig = ({
  vaapiVendor,
  openClPlatform,
}: {
  vaapiVendor: VaapiVendor;
  openClPlatform: number | null;
}): { initLine: string; deviceName: string } | null => {
  if (vaapiVendor === "INTEL") {
    return {
      initLine: `intel_opencl@intel_vaapi`,
      deviceName: "intel_opencl",
    };
  }
  if (openClPlatform !== null) {
    return {
      initLine: `opencl_${openClPlatform}:${openClPlatform}.0`,
      deviceName: `opencl_${openClPlatform}`,
    };
  }
  return null;
};

const getHardwareConfiguration = ({
  hwAccel,
  vaapiVendor,
  openClPlatform,
  mapOpenClFromVaapi,
}: {
  hwAccel: HwAccel;
  vaapiVendor: VaapiVendor;
  openClPlatform: number | null;
  mapOpenClFromVaapi: boolean;
}): {
  options: string[];
  outputFormat: HardwarePixelFormat;
  filterDeviceFormat: HardwarePixelFormat;
  openclMappedFromVaapi: boolean;
} => {
  const vaapiConfig = getVaapiDeviceConfig(vaapiVendor);
  const openClConfig = getOpenClDeviceConfig({
    vaapiVendor: mapOpenClFromVaapi
      ? vaapiVendor
      : openClPlatform !== null
      ? null
      : vaapiVendor,
    openClPlatform,
  });
  const useVaapi = hwAccel === "VAAPI" && vaapiConfig;
  const filterDeviceFormat: HardwarePixelFormat = openClConfig
    ? "OPENCL"
    : vaapiConfig
    ? "VAAPI"
    : null;
  return {
    options: [
      vaapiConfig && `-init_hw_device vaapi=${vaapiConfig.initLine}`,
      useVaapi && `-hwaccel vaapi`,
      useVaapi && vaapiConfig && `-hwaccel_device ${vaapiConfig.deviceName}`,
      useVaapi && `-hwaccel_output_format vaapi`,
      hwAccel === "NVDEC" && `-hwaccel nvdec`,
      openClConfig && `-init_hw_device opencl=${openClConfig.initLine}`,
      openClConfig
        ? `-filter_hw_device ${openClConfig.deviceName}`
        : vaapiConfig
        ? `-filter_hw_device ${vaapiConfig.deviceName}`
        : null,
    ].filter(notEmpty),
    outputFormat: useVaapi ? "VAAPI" : null,
    filterDeviceFormat,
    openclMappedFromVaapi: mapOpenClFromVaapi && vaapiVendor === "INTEL",
  };
};

const getInputConfiguration = ({
  hwAccel,
  vaapiVendor,
  openClPlatform,
  mapOpenClFromVaapi,
  start,
  duration,
  end,
  upsample,
  generatePadName,
}: RenderOptions & { generatePadName: () => string }): InputConfiguration => {
  const hardwareOptions = getHardwareConfiguration({
    hwAccel,
    vaapiVendor,
    openClPlatform,
    mapOpenClFromVaapi,
  });
  const outputPad = generatePadName();
  return {
    inputOptions: [
      ...hardwareOptions.options,
      start && `-ss ${start}`,
      duration && `-t ${duration}`,
      end && `-to ${end}`,
    ].filter(notEmpty),
    filters: connectFilters(
      [
        upsample && {
          filter: hwAccel == "VAAPI" ? "scale_vaapi" : "scale",
          options: {
            w: `iw*${(upsample || 100) / 100}`,
            h: `ih*${(upsample || 100) / 100}`,
          },
        },
      ].filter(notEmpty),
      "0:v",
      outputPad,
      generatePadName
    ),
    outputFormat: hardwareOptions.outputFormat,
    filterDeviceFormat: hardwareOptions.filterDeviceFormat,
    openclMappedFromVaapi: hardwareOptions.openclMappedFromVaapi,
    outputPad,
  };
};

type OutputConfiguration = {
  inputPad: string;
  outputPad: string;
  inputFormat: HardwarePixelFormat;
  filters: FilterSpecification[];
  options: string[];
};

const getOutputConfiguration = ({
  encoder,
  crop,
  generatePadName,
}: RenderOptions & { generatePadName: () => string }): OutputConfiguration => {
  const isVaapiEncoder = encoder.indexOf("vaapi") != -1;
  const isAmfEncoder = encoder.indexOf("amf") != -1;
  const inputFormat = isVaapiEncoder || isAmfEncoder ? "VAAPI" : null;
  const inputPad = generatePadName();
  const outputPad = generatePadName();
  return {
    inputFormat,
    filters: connectFilters(
      [
        crop
          ? {
              filter: `crop=${crop}`,
              options: {},
            }
          : undefined,
        crop
          ? {
              filter: inputFormat === "VAAPI" ? "scale_vaapi" : "scale",
              options: {},
            }
          : undefined,
        isAmfEncoder && inputFormat === "VAAPI"
          ? {
              filter: "hwmap",
              options: {},
            }
          : undefined,
      ].filter(notEmpty),
      inputPad,
      outputPad,
      generatePadName
    ),
    options: [`-c:v ${encoder}`, `-qp ${VAAPI_QP}`].filter(Boolean),
    inputPad,
    outputPad,
  };
};

const getAnalysePipeline = ({
  destFileName,
  stabilise,
  filter,
  inputPad,
  outputPad,
  generatePadName,
}: RenderOptions & {
  destFileName: string;
  inputPad: string;
  outputPad: string;
  generatePadName: () => string;
}): FilterPipeline => {
  return {
    inputFormat: null,
    inputPad,
    filters: connectFilters(
      [
        filter === "vidstab" &&
          stabilise !== "none" && {
            filter: "vidstabdetect",
            options: {
              result: `${destFileName}.trf`,
              shakiness: 10,
              mincontrast: 0.2,
              stepsize: 12,
              tripod: stabilise === "fixed" ? 1 : 0,
            },
          },
      ].filter(notEmpty),
      inputPad,
      outputPad,
      generatePadName
    ),
    outputFormat: null,
    outputPad,
  };
};

const combinePipelines = ({
  pipelines,
  filterDeviceFormat,
  openclMappedFromVaapi,
  inputPad,
  outputPad,
  generatePadName,
}: {
  pipelines: FilterPipeline[];
  filterDeviceFormat: HardwarePixelFormat;
  openclMappedFromVaapi: boolean;
  inputPad: string;
  outputPad: string;
  generatePadName: () => string;
}): FilterPipeline => {
  const nonEmptyPipelines = pipelines.filter(
    (pipeline) =>
      pipeline.filters.length > 0 && pipeline.filters[0]?.filter !== "null"
  );
  if (nonEmptyPipelines.length === 0) {
    return {
      inputPad,
      inputFormat: null,
      filters: [],
      outputFormat: null,
      outputPad,
    };
  }
  const pipelineFilters = nonEmptyPipelines.reduce(
    (filters: FilterSpecification[], pipeline, index) => {
      return filters.concat(
        pipeline.filters,
        index < nonEmptyPipelines.length - 1
          ? getConversionFilterSpecs({
              inputPad: pipeline.outputPad,
              inputFormat: pipeline.outputFormat,
              outputFormat: nonEmptyPipelines[index + 1].inputFormat,
              outputPad: nonEmptyPipelines[index + 1].inputPad,
              filterDeviceFormat,
              openclMappedFromVaapi,
              generatePadName,
            })
          : []
      );
    },
    []
  );
  return {
    inputPad: nonEmptyPipelines[0].inputPad,
    inputFormat: nonEmptyPipelines[0].inputFormat,
    filters: pipelineFilters,
    outputFormat: nonEmptyPipelines[nonEmptyPipelines.length - 1].outputFormat,
    outputPad: nonEmptyPipelines[nonEmptyPipelines.length - 1].outputPad,
  };
};

const connectPipelines = ({
  input,
  pipelines,
  output,
  filterDeviceFormat,
  openclMappedFromVaapi,
  generatePadName,
}: {
  input: InputConfiguration;
  pipelines: FilterPipeline[];
  output: OutputConfiguration;
  filterDeviceFormat: HardwarePixelFormat;
  openclMappedFromVaapi: boolean;
  generatePadName: () => string;
}): FilterSpecification[] => {
  const combinedPipeline = combinePipelines({
    pipelines,
    filterDeviceFormat,
    openclMappedFromVaapi,
    inputPad: generatePadName(),
    outputPad: generatePadName(),
    generatePadName,
  });
  if (combinedPipeline.filters.length === 0) {
    return input.filters.concat(
      getConversionFilterSpecs({
        inputFormat: input.outputFormat,
        outputFormat: output.inputFormat,
        filterDeviceFormat,
        openclMappedFromVaapi,
        inputPad: input.outputPad,
        outputPad: output.inputPad,
        generatePadName,
      }),
      output.filters
    );
  }
  return input.filters.concat(
    getConversionFilterSpecs({
      inputFormat: input.outputFormat,
      outputFormat: combinedPipeline.inputFormat,
      filterDeviceFormat,
      openclMappedFromVaapi,
      inputPad: input.outputPad,
      outputPad: combinedPipeline.inputPad,
      generatePadName,
    }),
    combinedPipeline.filters,
    getConversionFilterSpecs({
      inputFormat: combinedPipeline.outputFormat,
      outputFormat: output.inputFormat,
      filterDeviceFormat,
      openclMappedFromVaapi,
      inputPad: combinedPipeline.outputPad,
      outputPad: output.inputPad,
      generatePadName,
    }),
    output.filters
  );
};

const getV360Pipeline = ({
  upsample,
  stabiliseBuffer,
  projection,
  inputDfov,
  width,
  height,
  inputWidth,
  inputHeight,
  roll,
  pitch,
  yaw,
  zoom,
  inputPad,
  outputPad,
  generatePadName,
}: RenderOptions & {
  inputWidth: number;
  inputHeight: number;
  inputPad: string;
  outputPad: string;
  generatePadName: () => string;
}): FilterPipeline => {
  return {
    inputFormat: null,
    inputPad,
    filters: connectFilters(
      [
        {
          filter: "v360",
          options: {
            // Input: GoPro HERO5 Black
            input: "fisheye",
            id_fov: inputDfov * (1 + stabiliseBuffer / 100),

            // Output
            output: projection,
            // Diagonal FOV preserves the entire input in stereographic => rectilinear
            d_fov: (inputDfov * 100) / (100 + (zoom || 0)),
            w: width || (inputWidth * (upsample || 100)) / 100,
            h: height || (inputHeight * (upsample || 100)) / 100,

            pitch: (((pitch || 0) + 180) % 360) - 180,
            yaw: (((yaw || 0) + 180) % 360) - 180,
            roll: (((roll || 0) + 180) % 360) - 180,

            interp: "lanc",
          },
        },
      ],
      inputPad,
      outputPad,
      generatePadName
    ),
    outputPad,
    outputFormat: null,
  };
};

const getVidStabPipeline = ({
  destFileName,
  stabilise,
  stabiliseBuffer,
  stabiliseRadius,
  inputPad,
  outputPad,
  generatePadName,
}: RenderOptions & {
  destFileName: string;
  inputPad: string;
  outputPad: string;
  generatePadName: () => string;
}): FilterPipeline => {
  return {
    inputPad,
    inputFormat: null,
    filters: connectFilters(
      [
        stabilise !== "none" && {
          filter: "vidstabtransform",
          options: {
            input: `${destFileName}.trf`,
            optzoom: 0,
            zoom: -stabiliseBuffer,
            interpol: "bicubic",
            smoothing: stabiliseRadius,
            crop: "black",
            tripod: stabilise === "fixed" ? 1 : 0,
          },
        },
      ].filter(notEmpty),
      inputPad,
      outputPad,
      generatePadName
    ),
    outputFormat: null,
    outputPad,
  };
};

const getDewobbleProjectionPipeline = ({
  projection,
  stabiliseBuffer,
  inputDfov,
  inputPad,
  outputPad,
  generatePadName,
}: RenderOptions & {
  inputPad: string;
  outputPad: string;
  generatePadName: () => string;
}): FilterPipeline => {
  const outputProjection =
    projection === "fisheye" ? "fish" : projection === "flat" ? "rect" : null;
  if (outputProjection === null) {
    throw new Error(`Projection ${projection} not supported by Dewobble`);
  }
  return {
    inputPad,
    inputFormat: "OPENCL",
    filters: connectFilters(
      [
        {
          filter: "libdewobble",
          options: {
            in_p: "fish",
            in_dfov: inputDfov * (1 + stabiliseBuffer / 100),
            out_p: outputProjection,
            out_dfov: inputDfov,
            stab: "none",
          },
        },
      ],
      inputPad,
      outputPad,
      generatePadName
    ),
    outputFormat: "OPENCL",
    outputPad,
  };
};

const getDewobbleStabilisePipeline = ({
  projection,
  stabilise,
  inputDfov,
  stabiliseRadius,
  debug,
  inputPad,
  outputPad,
  generatePadName,
}: RenderOptions & {
  inputPad: string;
  outputPad: string;
  generatePadName: () => string;
}): FilterPipeline => {
  const outputProjection =
    projection === "fisheye" ? "fish" : projection === "flat" ? "rect" : null;
  if (outputProjection === null) {
    throw new Error(`Projection ${projection} not supported by Dewobble`);
  }
  return {
    inputPad,
    inputFormat: "OPENCL",
    filters: connectFilters(
      [
        {
          filter: "libdewobble",
          options: {
            in_p: "fish",
            in_dfov: inputDfov,
            out_p: outputProjection,
            out_dfov: inputDfov,
            stab: stabilise === "smooth" ? "sg" : stabilise,
            stab_r: stabiliseRadius,
            debug: debug ? 1 : 0,
          },
        },
      ],
      inputPad,
      outputPad,
      generatePadName
    ),
    outputPad,
    outputFormat: "OPENCL",
  };
};

const getDewobbleBufferPipeline = ({
  stabiliseBuffer,
  inputDfov,
  inputPad,
  outputPad,
  generatePadName,
}: RenderOptions & {
  inputPad: string;
  outputPad: string;
  generatePadName: () => string;
}): FilterPipeline => {
  return {
    inputPad,
    inputFormat: "OPENCL",
    filters: connectFilters(
      [
        stabiliseBuffer !== 0 && {
          filter: "libdewobble",
          options: {
            in_p: "fish",
            in_dfov: inputDfov,
            out_p: "fish",
            out_dfov: inputDfov * (1 + stabiliseBuffer / 100),
            stab: "none",
          },
        },
      ].filter(notEmpty),
      inputPad,
      outputPad,
      generatePadName
    ),
    outputPad,
    outputFormat: "OPENCL",
  };
};

const getDeshakePipeline = ({
  stabilise,
  stabiliseBuffer,
  inputWidth,
  inputHeight,
  inputPad,
  outputPad,
  generatePadName,
}: RenderOptions & {
  inputWidth: number;
  inputHeight: number;
  inputPad: string;
  outputPad: string;
  generatePadName: () => string;
}): FilterPipeline => {
  const borderSize = stabiliseBuffer / (2 * (100 + stabiliseBuffer));
  return {
    inputPad,
    inputFormat: null,
    filters: connectFilters(
      [
        {
          filter: "deshake",
          options: {
            edge: "blank",
            blocksize: 40,
            rx: 32,
            ry: 32,
            x: inputWidth * borderSize,
            y: inputHeight * borderSize,
            w: inputWidth * (1 - borderSize),
            h: inputHeight * (1 - borderSize),
          },
        },
      ],
      inputPad,
      outputPad,
      generatePadName
    ),
    outputPad,
    outputFormat: null,
  };
};

const getBlurEdgesPipeline = ({
  inputPad,
  outputPad,
  stabiliseBuffer,
  generatePadName,
  inputWidth,
  inputHeight,
}: RenderOptions & {
  inputPad: string;
  outputPad: string;
  inputWidth: number;
  inputHeight: number;
  generatePadName: () => string;
}): FilterPipeline => {
  const blur = stabiliseBuffer > 0;
  const beforeBlur = generatePadName();
  const blurMask = generatePadName();
  const blackSource = generatePadName();
  const blurRadius = 32;
  return {
    inputPad,
    inputFormat: null,
    filters: [
      ...connectFilters(
        [
          blur && {
            filter: "format",
            options: {
              pix_fmts: "rgba",
            },
          },
        ].filter(notEmpty),
        inputPad,
        blur ? beforeBlur : outputPad,
        generatePadName
      ),
      blur && {
        inputs: [beforeBlur, blurMask],
        filter: "overlay",
        outputs: outputPad,
      },
      blur && {
        inputs: [],
        filter: "color",
        options: {
          color: "black",
          size: `${inputWidth}x${inputHeight}`,
          rate: 1,
          duration: 1,
        },
        outputs: blackSource,
      },
      ...(blur
        ? connectFilters(
            [
              {
                filter: "format",
                options: {
                  pix_fmts: "rgba",
                },
              },
              {
                filter: "geq",
                options: {
                  r: `r(X,Y)`,
                  a: `max(255-max(0,min(255,min(X*${255 / blurRadius},(W-X)*${
                    255 / blurRadius
                  }))),255-max(0,min(255,min(Y*${255 / blurRadius},(H-Y)*${
                    255 / blurRadius
                  }))))`,
                },
              },
            ],
            blackSource,
            blurMask,
            generatePadName
          )
        : []),
    ].filter(notEmpty),
    outputPad,
    outputFormat: null,
  };
};

const getDeshakeOpenClPipeline = ({
  stabilise,
  stabiliseRadius,
  debug,
  inputPad,
  outputPad,
  frameRate,
  generatePadName,
}: RenderOptions & {
  inputPad: string;
  outputPad: string;
  frameRate: number;
  generatePadName: () => string;
}): FilterPipeline => ({
  inputPad,
  inputFormat: debug ? null : "OPENCL",
  filters: connectFilters(
    [
      debug && {
        filter: "format",
        options: {
          pix_fmts: "bgr24", // Debug mode requires RGB
        },
      },
      debug && {
        filter: "hwupload",
        options: {},
      },
      {
        filter: "deshake_opencl",
        options: {
          tripod: stabilise === "fixed" ? 1 : 0,
          adaptive_crop: 0,
          smooth_window_multiplier: (stabiliseRadius * 2) / frameRate,
          debug: debug ? 1 : 0,
        },
      },
      debug && {
        filter: "hwdownload",
        options: {},
      },
      debug && {
        filter: "format",
        options: {
          pix_fmts: "gbrp",
        },
      },
    ].filter(notEmpty),
    inputPad,
    outputPad,
    generatePadName
  ),
  outputPad,
  outputFormat: debug ? null : "OPENCL",
});

const getRenderPipeline = (
  options: RenderOptions & {
    inputWidth: number;
    inputHeight: number;
    frameRate: number;
    destFileName: string;
    filterDeviceFormat: HardwarePixelFormat;
    openclMappedFromVaapi: boolean;
    inputPad: string;
    outputPad: string;
    generatePadName: () => string;
  }
) => {
  const {
    filter,
    filterDeviceFormat,
    openclMappedFromVaapi,
    inputPad,
    outputPad,
    generatePadName,
  } = options;
  switch (filter) {
    case "vidstab": {
      return combinePipelines({
        pipelines: [
          getVidStabPipeline(options),
          getDewobbleProjectionPipeline(options),
        ],
        filterDeviceFormat,
        openclMappedFromVaapi,
        inputPad,
        outputPad,
        generatePadName,
      });
    }
    case "deshake": {
      return combinePipelines({
        pipelines: [
          getDewobbleBufferPipeline(options),
          getDeshakePipeline(options),
          getDewobbleProjectionPipeline(options),
        ],
        filterDeviceFormat,
        openclMappedFromVaapi,
        inputPad,
        outputPad,
        generatePadName,
      });
    }
    case "deshake_opencl": {
      return combinePipelines({
        pipelines: [
          getBlurEdgesPipeline(options),
          getDewobbleBufferPipeline(options),
          getDeshakeOpenClPipeline(options),
          getDewobbleProjectionPipeline(options),
        ],
        filterDeviceFormat,
        openclMappedFromVaapi,
        inputPad,
        outputPad,
        generatePadName,
      });
    }
    case "dewobble": {
      return getDewobbleStabilisePipeline(options);
    }
    default: {
      throw new Error(`Unknown filter ${filter}`);
    }
  }
};

const analyse = (
  sourceFileName: string,
  destFileName: string,
  options: RenderOptions
) =>
  analyseQueue.add(
    () =>
      new Promise<void>(async (resolve, reject) => {
        const generatePadName = makeGeneratePadName();
        const inputConfiguration = getInputConfiguration({
          ...options,
          generatePadName,
        });
        const analysePipeline = getAnalysePipeline({
          destFileName,
          ...options,
          inputPad: generatePadName(),
          outputPad: "output",
          generatePadName,
        });
        if (
          analysePipeline.filters.length === 0 ||
          !analysePipeline.filters.some(({ filter }) => filter !== "null")
        ) {
          return resolve();
        }
        return Ffmpeg()
          .on("start", console.log)
          .on("codecData", console.log)
          .on("progress", ({ percent, currentFps }) =>
            console.log(`${Math.floor(percent)}% (${currentFps}fps)`)
          )
          .on("error", reject)
          .on("end", resolve)
          .input(sourceFileName)
          .inputOptions(inputConfiguration.inputOptions)
          .filterGraph(
            [
              ...inputConfiguration.filters,
              ...getConversionFilterSpecs({
                inputPad: inputConfiguration.outputPad,
                inputFormat: inputConfiguration.outputFormat,
                outputFormat: analysePipeline.inputFormat,
                outputPad: analysePipeline.inputPad,
                filterDeviceFormat: inputConfiguration.filterDeviceFormat,
                openclMappedFromVaapi: inputConfiguration.openclMappedFromVaapi,
                generatePadName,
              }),
              ...analysePipeline.filters,
            ].filter(notEmpty),
            analysePipeline.outputPad
          )
          .format("null")
          .output("-")
          .run();
      })
  );

const encode = async (
  sourceFileName: string,
  destFileName: string,
  options: RenderOptions
) =>
  encodeQueue.add(
    () =>
      new Promise(async (resolve, reject) => {
        const metadata = await getMetadata(sourceFileName);
        const videoStream = metadata.streams.find(
          (stream) => stream.codec_type === "video"
        );
        if (!videoStream) {
          throw new Error("Failed to find a video stream");
        }
        const {
          width: inputWidth,
          height: inputHeight,
          avg_frame_rate,
        } = videoStream;
        if (
          inputWidth === undefined ||
          inputHeight === undefined ||
          avg_frame_rate === undefined
        ) {
          throw new Error("Failed to find video dimensions");
        }
        const frameRateComponents = avg_frame_rate.split("/");
        const frameRate =
          parseNumber(frameRateComponents[0]) /
          parseNumber(frameRateComponents[1]);

        const generatePadName = makeGeneratePadName();

        const inputConfiguration = getInputConfiguration({
          ...options,
          generatePadName,
        });

        const stabilisePipeline = getRenderPipeline({
          ...options,
          inputPad: generatePadName(),
          outputPad: generatePadName(),
          inputWidth,
          inputHeight,
          destFileName,
          filterDeviceFormat: inputConfiguration.filterDeviceFormat,
          openclMappedFromVaapi: inputConfiguration.openclMappedFromVaapi,
          generatePadName,
          frameRate,
        });

        const outputConfiguration = getOutputConfiguration({
          ...options,
          generatePadName,
        });

        return Ffmpeg()
          .on("start", console.log)
          .on("codecData", console.log)
          .on("progress", ({ percent, currentFps }) =>
            console.log(`${Math.floor(percent)}% (${currentFps}fps)`)
          )
          .on("error", reject)
          .on("stderr", console.log)
          .on("end", resolve)
          .input(sourceFileName)
          .inputOptions(inputConfiguration.inputOptions)
          .complexFilter(
            connectPipelines({
              input: inputConfiguration,
              pipelines: [stabilisePipeline],
              output: outputConfiguration,
              filterDeviceFormat: inputConfiguration.filterDeviceFormat,
              openclMappedFromVaapi: inputConfiguration.openclMappedFromVaapi,
              generatePadName,
            }),
            outputConfiguration.outputPad
          )
          .output(destFileName)
          .outputOptions(outputConfiguration.options)
          .run();
      })
  );

export const render = async (
  sourceFileName: string,
  destFileName: string,
  options: RenderOptions
) => {
  const { encodeOnly, analyseOnly } = options;
  if (!encodeOnly) {
    await analyse(sourceFileName, destFileName, options);
  }
  if (!analyseOnly) {
    await encode(sourceFileName, destFileName, options);
  }
};
