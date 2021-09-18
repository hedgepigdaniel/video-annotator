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

type StabilisationFilter =
  | "vidstab"
  | "deshake"
  | "deshake_opencl"
  | "dewobble";

type Projection = "flat" | "fisheye";

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
  filter: StabilisationFilter;
  stabilise: "none" | "fixed" | "smooth";
  stabiliseRadius: number;
  interpolateRadius: number;
  stabiliseBuffer: number;
  inputDfov: number;
  outputDfov: number | null;
  projection: Projection;
  crop: string | null;
  hwAccel: HwAccel;
  vaapiVendor: VaapiVendor;
  openClPlatform: number | null;
  mapOpenClFromVaapi: boolean;
  copyVaapiFrames: boolean;
  encoder: string;
  frameRate: number | null;
  debug: boolean;
  compare: StabilisationFilter[] | null;
  verbosity: string | null;
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
  copyVaapiFrames,
  start,
  duration,
  end,
  upsample,
  generatePadName,
  stabiliseRadius,
  interpolateRadius,
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
      hardwareOptions.outputFormat === "VAAPI" &&
        hardwareOptions.openclMappedFromVaapi &&
        !copyVaapiFrames &&
        `-extra_hw_frames ${stabiliseRadius + interpolateRadius}`,
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
        hardwareOptions.outputFormat === "VAAPI" &&
          copyVaapiFrames && {
            filter: "hwdownload",
            options: {},
          },
      ].filter(notEmpty),
      "0:v",
      outputPad,
      generatePadName
    ),
    outputFormat:
      hardwareOptions.outputFormat === "VAAPI" && copyVaapiFrames
        ? null
        : hardwareOptions.outputFormat,
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
  frameRate,
  inputFrameRate,
  specifiedFrameRate,
  generatePadName,
}: RenderOptions & {
  generatePadName: () => string;
  inputFrameRate: number;
  specifiedFrameRate: number | null;
}): OutputConfiguration => {
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
        frameRate
          ? {
              filter: "setpts",
              options: {
                expr: `PTS*${inputFrameRate / frameRate}`,
              },
            }
          : undefined,
      ].filter(notEmpty),
      inputPad,
      outputPad,
      generatePadName
    ),
    options: [
      `-c:v ${encoder}`,
      `-qp ${VAAPI_QP}`,
      frameRate && specifiedFrameRate && `-r ${frameRate}`,
    ].filter(notEmpty),
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
  compare,
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
        (filter === "vidstab" || compare) &&
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
  outputDfov,
  width,
  height,
  inputWidth,
  inputHeight,
  roll,
  pitch,
  yaw,
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
            d_fov: outputDfov,
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
  outputDfov,
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
            out_dfov: outputDfov || inputDfov,
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
  inputWidth,
  inputHeight,
  outputWidth,
  outputHeight,
  focalPointX,
  focalPointY,
  outputDfov,
  inputPad,
  outputPad,
  generatePadName,
}: RenderOptions & {
  inputPad: string;
  outputPad: string;
  outputWidth?: number;
  outputHeight?: number;
  focalPointX?: number;
  focalPointY?: number;
  outputDfov: number | null;
  inputWidth: number;
  inputHeight: number;
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
            out_dfov: outputDfov || inputDfov,
            stab: stabilise === "smooth" ? "sg" : stabilise,
            stab_r: stabiliseRadius,
            debug: debug ? 1 : 0,
            out_w: outputWidth || inputWidth,
            out_h: outputHeight || inputHeight,
            out_fx: focalPointX || (outputWidth || inputWidth) / 2,
            out_fy: focalPointY || (outputHeight || inputHeight) / 2,
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
            edge: stabiliseBuffer ? "mirror" : "blank",
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
  inputFrameRate,
  generatePadName,
}: RenderOptions & {
  inputPad: string;
  outputPad: string;
  inputFrameRate: number;
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
          smooth_window_multiplier: (stabiliseRadius * 2) / inputFrameRate,
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
    outputWidth?: number;
    outputHeight?: number;
    focalPointX?: number;
    focalPointY?: number;
    outputDfov: number | null;
    inputFrameRate: number;
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

export const multiplyDimensions = (
  projection: Projection,
  fov: number,
  multiple: number
) => {
  switch (projection) {
    case "fisheye": {
      return fov * multiple;
    }
    case "flat": {
      return (
        2 *
        (180 / Math.PI) *
        Math.atan(multiple * Math.tan(((fov / 2) * Math.PI) / 180))
      );
    }
    default: {
      throw new Error(`unknown projection ${projection}`);
    }
  }
};

const getComparisonGridSize = (
  numStreams: number,
  preferredRatio: number
): [width: number, height: number] => {
  const landscapeRatio =
    preferredRatio > 1 ? 1 / preferredRatio : preferredRatio;

  const getRatioInfo = (shortSide: number) => {
    const longSide = Math.ceil(numStreams / shortSide);
    const gridArea = longSide * shortSide;
    const preferredArea =
      Math.max(longSide, shortSide / landscapeRatio) *
      Math.max(shortSide, longSide * landscapeRatio);
    const areaUsed =
      shortSide / longSide < landscapeRatio
        ? (shortSide / longSide) * landscapeRatio
        : (longSide / shortSide) * landscapeRatio;
    return {
      shortSide,
      longSide,
      areaUsed: gridArea / preferredArea,
    };
  };

  let bestRatio = getRatioInfo(Math.ceil(Math.sqrt(numStreams)));

  while (bestRatio.shortSide > 1) {
    const nextRatio = getRatioInfo(bestRatio.shortSide - 1);
    if (nextRatio.areaUsed > bestRatio.areaUsed) {
      bestRatio = nextRatio;
    } else {
      break;
    }
  }
  return preferredRatio > 1
    ? [bestRatio.longSide, bestRatio.shortSide]
    : [bestRatio.shortSide, bestRatio.longSide];
};

const getComparisonPipeline = (
  options: RenderOptions & {
    inputWidth: number;
    inputHeight: number;
    inputFrameRate: number;
    destFileName: string;
    filterDeviceFormat: HardwarePixelFormat;
    openclMappedFromVaapi: boolean;
    inputPad: string;
    outputPad: string;
    generatePadName: () => string;
  }
): FilterPipeline => {
  const {
    inputPad,
    outputPad,
    inputWidth,
    inputHeight,
    generatePadName,
    stabilise,
    inputDfov,
    outputDfov,
    projection,
    compare,
  } = options;

  if (stabilise === "none") {
    throw new Error(`Comparison not possible without stabilisation`);
  }

  if (compare === null) {
    throw new Error(`should not happen`);
  }

  const filters: StabilisationFilter[] = ["dewobble", ...compare];

  const [gridWidth, gridHeight] = getComparisonGridSize(
    filters?.length,
    16 / 10
  );

  const scale =
    gridWidth / inputWidth > gridHeight / inputHeight
      ? 1 / gridHeight
      : 1 / gridWidth;

  const scaledWidth = Math.floor(inputWidth * scale);
  const scaledHeight = Math.floor(inputHeight * scale);

  const dFovRatio =
    Math.sqrt(
      inputWidth * gridWidth * (inputWidth * gridWidth) +
        inputHeight * gridHeight * (inputHeight * gridHeight)
    ) / Math.sqrt(inputWidth * inputWidth + inputHeight * inputHeight);

  const inputPads = filters.map(() => generatePadName());
  const outputPads = filters.map(() => generatePadName());

  const pipelines = [
    getRenderPipeline({
      ...options,
      filter: filters[0],
      inputPad: inputPads[0],
      outputPad: outputPads[0],
      inputWidth: scaledWidth,
      inputHeight: scaledHeight,
      outputWidth: scaledWidth * gridWidth,
      outputHeight: scaledHeight * gridHeight,
      focalPointX: scaledWidth / 2,
      focalPointY: scaledHeight / 2,
      outputDfov: multiplyDimensions(
        projection,
        outputDfov || inputDfov,
        dFovRatio
      ),
      stabilise: "none",
    }),
  ].concat(
    filters.slice(1).map((filter, index) =>
      getRenderPipeline({
        ...options,
        filter,
        inputPad: inputPads[index + 1],
        outputPad: outputPads[index + 1],
        inputWidth: scaledWidth,
        inputHeight: scaledHeight,
      })
    )
  );

  const inputCpuPads = inputPads.filter(
    (pad, index) => pipelines[index].inputFormat === null
  );
  const inputOpenClPads = inputPads.filter(
    (pad, index) => pipelines[index].inputFormat === "OPENCL"
  );
  const uploadPad = generatePadName();

  return {
    inputPad,
    inputFormat: null,
    filters: [
      ...connectFilters(
        [
          {
            filter: "scale",
            options: { w: scaledWidth, h: scaledHeight },
          },
          {
            filter: "format",
            options: { pix_fmts: "nv12" },
          },
          {
            filter: "split",
            options: { outputs: inputCpuPads.length + 1 },
          },
        ],
        inputPad,
        [uploadPad, ...inputCpuPads],
        generatePadName
      ),
      ...connectFilters(
        [
          {
            filter: "hwupload",
            options: {},
          },
          {
            filter: "split",
            options: { outputs: inputOpenClPads.length },
          },
        ],
        uploadPad,
        inputOpenClPads,
        generatePadName
      ),
      ...pipelines.flatMap((pipeline) => pipeline.filters),
      ...outputPads.slice(2).reduce(
        (filters: FilterSpecification[], overlayPad, index) => {
          const lastFilter = filters[filters.length - 1];
          const intermediatePad = generatePadName();
          return [
            ...filters.slice(0, filters.length - 1),
            { ...lastFilter, outputs: [intermediatePad] },
            {
              inputs: [intermediatePad, overlayPad],
              filter: "overlay_opencl",
              options: {
                x: scaledWidth * ((index + 2) % gridWidth),
                y: scaledHeight * Math.floor((index + 2) / gridWidth),
              },
              outputs: [outputPad],
            },
          ];
        },
        [
          {
            inputs: [outputPads[0], outputPads[1]],
            filter: "overlay_opencl",
            options: {
              x: scaledWidth * (1 % gridWidth),
              y: scaledHeight * Math.floor(1 / gridWidth),
            },
            outputs: [outputPad],
          },
        ]
      ),
    ],
    outputPad,
    outputFormat: "OPENCL",
  };
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

const parseFrameRate = (rateString: string) => {
  const frameRateComponents = rateString.split("/");
  return (
    parseNumber(frameRateComponents[0]) / parseNumber(frameRateComponents[1])
  );
};

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
          r_frame_rate,
        } = videoStream;
        if (
          inputWidth === undefined ||
          inputHeight === undefined ||
          avg_frame_rate === undefined
        ) {
          throw new Error("Failed to find video dimensions");
        }
        const frameRateComponents = avg_frame_rate.split("/");
        const inputFrameRate = parseFrameRate(avg_frame_rate);
        const specifiedFrameRate = r_frame_rate
          ? parseFrameRate(r_frame_rate)
          : null;

        const generatePadName = makeGeneratePadName();

        const inputConfiguration = getInputConfiguration({
          ...options,
          generatePadName,
        });

        const pipelineCreator = options.compare
          ? getComparisonPipeline
          : getRenderPipeline;
        const stabilisePipeline = pipelineCreator({
          ...options,
          inputPad: generatePadName(),
          outputPad: generatePadName(),
          inputWidth,
          inputHeight,
          destFileName,
          filterDeviceFormat: inputConfiguration.filterDeviceFormat,
          openclMappedFromVaapi: inputConfiguration.openclMappedFromVaapi,
          generatePadName,
          inputFrameRate,
        });

        const outputConfiguration = getOutputConfiguration({
          ...options,
          generatePadName,
          inputFrameRate,
          specifiedFrameRate,
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
          .outputOptions(
            [
              ...outputConfiguration.options,
              options.verbosity && `-v ${options.verbosity}`,
            ].filter(notEmpty)
          )
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
