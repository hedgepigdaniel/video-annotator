import Ffmpeg, { AudioVideoFilter, Filter } from "fluent-ffmpeg";
import Queue from "promise-queue";

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

type HardwarePixelFormat = "VAAPI" | "OPENCL" | "AMF" | null;

type FilterPipeline = {
  inputFormat: HardwarePixelFormat;
  filters: AudioVideoFilter[];
  outputFormat: HardwarePixelFormat;
};

type InputConfiguration = {
  inputOptions: string[];
  filters: AudioVideoFilter[];
  outputFormat: HardwarePixelFormat;
  filterDeviceFormat: HardwarePixelFormat;
  openclMappedFromVaapi: boolean;
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
}: RenderOptions): InputConfiguration => {
  const hardwareOptions = getHardwareConfiguration({
    hwAccel,
    vaapiVendor,
    openClPlatform,
    mapOpenClFromVaapi,
  });
  return {
    inputOptions: [
      ...hardwareOptions.options,
      start && `-ss ${start}`,
      duration && `-t ${duration}`,
      end && `-to ${end}`,
    ].filter(notEmpty),
    filters: [
      upsample && {
        filter: hwAccel == "VAAPI" ? "scale_vaapi" : "scale",
        options: {
          w: `iw*${(upsample || 100) / 100}`,
          h: `ih*${(upsample || 100) / 100}`,
        },
      },
    ].filter(notEmpty),
    outputFormat: hardwareOptions.outputFormat,
    filterDeviceFormat: hardwareOptions.filterDeviceFormat,
    openclMappedFromVaapi: hardwareOptions.openclMappedFromVaapi,
  };
};

type OutputConfiguration = {
  inputFormat: HardwarePixelFormat;
  filters: AudioVideoFilter[];
  options: string[];
};

const getOutputConfiguration = ({
  encoder,
  crop,
}: RenderOptions): OutputConfiguration => {
  const isVaapiEncoder = encoder.indexOf("vaapi") != -1;
  const isAmfEncoder = encoder.indexOf("amf") != -1;
  const inputFormat = isVaapiEncoder || isAmfEncoder ? "VAAPI" : null;
  return {
    inputFormat,
    filters: [
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
    options: [`-c:v ${encoder}`, `-qp ${VAAPI_QP}`].filter(Boolean),
  };
};

const getConversionFilters = ({
  inputFormat,
  outputFormat,
  filterDeviceFormat,
  openclMappedFromVaapi,
}: {
  inputFormat: HardwarePixelFormat;
  outputFormat: HardwarePixelFormat;
  filterDeviceFormat: HardwarePixelFormat;
  openclMappedFromVaapi: boolean;
}): AudioVideoFilter[] => {
  if (inputFormat === outputFormat) {
    return [];
  }
  if (outputFormat === null) {
    return [
      { filter: "hwdownload", options: {} },
      {
        filter: "format",
        options: {
          pix_fmts: "nv12",
        },
      },
    ];
  }
  if (outputFormat === "OPENCL") {
    if (inputFormat === "VAAPI" && openclMappedFromVaapi) {
      if (filterDeviceFormat === "OPENCL") {
        return [{ filter: "hwmap", options: {} }];
      }
      if (filterDeviceFormat === "VAAPI") {
        return [{ filter: "hwmap", options: { derive_device: "vaapi" } }];
      }
    }
    if (filterDeviceFormat === "OPENCL") {
      return [
        inputFormat !== null && { filter: "hwdownload", options: {} },
        {
          filter: "format",
          options: {
            pix_fmts: "nv12",
          },
        },
        { filter: "hwupload", options: {} },
      ].filter(notEmpty);
    }
  }
  if (outputFormat === "VAAPI") {
    if (inputFormat === "OPENCL" && openclMappedFromVaapi) {
      return [
        { filter: "hwmap", options: { derive_device: "vaapi", reverse: 1 } },
      ];
    }
    if (filterDeviceFormat === "VAAPI") {
      return [
        inputFormat !== null && {
          filter: "hwdownload",
          options: {},
        },
        {
          filter: "format",
          options: {
            pix_fmts: "nv12",
          },
        },
        { filter: "hwupload", options: {} },
      ].filter(notEmpty);
    }
  }
  throw Error(`Cannot convert between ${inputFormat} and ${outputFormat}`);
};

const getAnalysePipeline = ({
  destFileName,
  stabilise,
  filter,
}: RenderOptions & {
  destFileName: string;
}): FilterPipeline => {
  return {
    inputFormat: null,
    filters: [
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
    outputFormat: null,
  };
};

const combinePipelines = ({
  pipelines,
  filterDeviceFormat,
  openclMappedFromVaapi,
}: {
  pipelines: FilterPipeline[];
  filterDeviceFormat: HardwarePixelFormat;
  openclMappedFromVaapi: boolean;
}): FilterPipeline => {
  const nonEmptyPipelines = pipelines.filter(
    (pipeline) => pipeline.filters.length > 0
  );
  if (nonEmptyPipelines.length === 0) {
    return {
      inputFormat: null,
      filters: [],
      outputFormat: null,
    };
  }
  const pipelineFilters = nonEmptyPipelines.reduce(
    (filters: AudioVideoFilter[], pipeline, index) => {
      return filters.concat(
        pipeline.filters,
        index < nonEmptyPipelines.length - 1
          ? getConversionFilters({
              inputFormat: pipeline.outputFormat,
              outputFormat: nonEmptyPipelines[index + 1].inputFormat,
              filterDeviceFormat,
              openclMappedFromVaapi,
            })
          : []
      );
    },
    []
  );
  return {
    inputFormat: nonEmptyPipelines[0].inputFormat,
    filters: pipelineFilters,
    outputFormat: nonEmptyPipelines[nonEmptyPipelines.length - 1].outputFormat,
  };
};

const connectPipelines = ({
  input,
  pipelines,
  output,
  filterDeviceFormat,
  openclMappedFromVaapi,
}: {
  input: InputConfiguration;
  pipelines: FilterPipeline[];
  output: OutputConfiguration;
  filterDeviceFormat: HardwarePixelFormat;
  openclMappedFromVaapi: boolean;
}): AudioVideoFilter[] => {
  const combinedPipeline = combinePipelines({
    pipelines,
    filterDeviceFormat,
    openclMappedFromVaapi,
  });
  if (combinedPipeline.filters.length === 0) {
    return input.filters.concat(
      getConversionFilters({
        inputFormat: input.outputFormat,
        outputFormat: output.inputFormat,
        filterDeviceFormat,
        openclMappedFromVaapi,
      }),
      output.filters
    );
  }
  return input.filters.concat(
    getConversionFilters({
      inputFormat: input.outputFormat,
      outputFormat: combinedPipeline.inputFormat,
      filterDeviceFormat,
      openclMappedFromVaapi,
    }),
    combinedPipeline.filters,
    getConversionFilters({
      inputFormat: combinedPipeline.outputFormat,
      outputFormat: output.inputFormat,
      filterDeviceFormat,
      openclMappedFromVaapi,
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
}: RenderOptions & {
  inputWidth: number;
  inputHeight: number;
}): FilterPipeline => {
  return {
    inputFormat: null,
    filters: [
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
    outputFormat: null,
  };
};

const getVidStabPipeline = ({
  destFileName,
  stabilise,
  stabiliseBuffer,
  stabiliseRadius,
}: RenderOptions & {
  destFileName: string;
}): FilterPipeline => {
  return {
    inputFormat: null,
    filters: [
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
    outputFormat: null,
  };
};

const getDewobbleProjectionPipeline = ({
  projection,
  stabiliseBuffer,
  inputDfov,
}: RenderOptions): FilterPipeline => {
  const outputProjection =
    projection === "fisheye" ? "fish" : projection === "flat" ? "rect" : null;
  if (outputProjection === null) {
    throw new Error(`Projection ${projection} not supported by Dewobble`);
  }
  return {
    inputFormat: "OPENCL",
    filters: [
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
    outputFormat: "OPENCL",
  };
};

const getDewobbleStabilisePipeline = ({
  projection,
  stabilise,
  inputDfov,
  stabiliseRadius,
  debug,
}: RenderOptions): FilterPipeline => {
  const outputProjection =
    projection === "fisheye" ? "fish" : projection === "flat" ? "rect" : null;
  if (outputProjection === null) {
    throw new Error(`Projection ${projection} not supported by Dewobble`);
  }
  return {
    inputFormat: "OPENCL",
    filters: [
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
    outputFormat: "OPENCL",
  };
};

const getDewobbleBufferPipeline = ({
  stabiliseBuffer,
  inputDfov,
}: RenderOptions): FilterPipeline => {
  return {
    inputFormat: "OPENCL",
    filters: [
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
    outputFormat: "OPENCL",
  };
};

const getDeshakePipeline = ({
  stabilise,
  stabiliseBuffer,
  inputWidth,
  inputHeight,
}: RenderOptions & {
  inputWidth: number;
  inputHeight: number;
}): FilterPipeline => {
  const borderSize = stabiliseBuffer / (2 * (100 + stabiliseBuffer));
  return {
    inputFormat: null,
    filters: [
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
    outputFormat: null,
  };
};

const getDeshakeOpenClPipeline = ({
  stabilise,
  stabiliseRadius,
  frameRate,
  debug,
}: RenderOptions & { frameRate: number; debug: boolean }): FilterPipeline => {
  return {
    inputFormat: debug ? null : "OPENCL",
    filters: [
      debug && {
        filter: "format",
        options: {
          pix_fmts: "rgba",
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
          debug: 1,
        },
      },
      debug && {
        filter: "hwdownload",
        options: {},
      },
      debug && {
        filter: "format",
        options: {
          pix_fmts: "rgba",
        },
      },
    ].filter(notEmpty),
    outputFormat: debug ? null : "OPENCL",
  };
};

const getRenderPipeline = (
  options: RenderOptions & {
    inputWidth: number;
    inputHeight: number;
    frameRate: number;
    destFileName: string;
    filterDeviceFormat: HardwarePixelFormat;
    openclMappedFromVaapi: boolean;
  }
) => {
  const { filter, filterDeviceFormat, openclMappedFromVaapi } = options;
  switch (filter) {
    case "vidstab": {
      return combinePipelines({
        pipelines: [
          getVidStabPipeline(options),
          getDewobbleProjectionPipeline(options),
        ],
        filterDeviceFormat,
        openclMappedFromVaapi,
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
      });
    }
    case "deshake_opencl": {
      return combinePipelines({
        pipelines: [
          getDewobbleBufferPipeline(options),
          getDeshakeOpenClPipeline(options),
          getDewobbleProjectionPipeline(options),
        ],
        filterDeviceFormat,
        openclMappedFromVaapi,
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
        const inputConfiguration = getInputConfiguration(options);
        const analysePipeline = getAnalysePipeline({
          destFileName,
          ...options,
        });
        if (analysePipeline.filters.length === 0) {
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
          .videoFilters(
            [
              ...inputConfiguration.filters,
              ...getConversionFilters({
                inputFormat: inputConfiguration.outputFormat,
                outputFormat: analysePipeline.inputFormat,
                filterDeviceFormat: inputConfiguration.filterDeviceFormat,
                openclMappedFromVaapi: inputConfiguration.openclMappedFromVaapi,
              }),
              ...analysePipeline.filters,
            ].filter(notEmpty)
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

        const inputConfiguration = getInputConfiguration(options);

        const stabilisePipeline = getRenderPipeline({
          ...options,
          inputWidth,
          inputHeight,
          destFileName,
          filterDeviceFormat: inputConfiguration.filterDeviceFormat,
          openclMappedFromVaapi: inputConfiguration.openclMappedFromVaapi,
          frameRate,
        });

        const outputConfiguration = getOutputConfiguration(options);

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
          .videoFilters(
            connectPipelines({
              input: inputConfiguration,
              pipelines: [stabilisePipeline],
              output: outputConfiguration,
              filterDeviceFormat: inputConfiguration.filterDeviceFormat,
              openclMappedFromVaapi: inputConfiguration.openclMappedFromVaapi,
            })
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
