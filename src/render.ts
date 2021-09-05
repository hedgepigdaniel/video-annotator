import Ffmpeg, { AudioVideoFilter } from "fluent-ffmpeg";
import Queue from "promise-queue";

import { getMetadata, notEmpty } from "./utils";

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
  stabilise: boolean;
  stabiliseBuffer: number;
  projection: string | null;
  zoom: number;
  crop: string | null;
  hwAccel: HwAccel;
  vaapiVendor: VaapiVendor;
  openClPlatform: number | null;
  mapOpenClFromVaapi: boolean;
  encoder: string;
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
} => {
  const vaapiConfig = getVaapiDeviceConfig(vaapiVendor);
  const openClConfig = getOpenClDeviceConfig({
    vaapiVendor: mapOpenClFromVaapi ? vaapiVendor : null,
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
}: {
  inputFormat: HardwarePixelFormat;
  outputFormat: HardwarePixelFormat;
  filterDeviceFormat: HardwarePixelFormat;
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
    if (inputFormat === null && filterDeviceFormat === "OPENCL") {
      return [
        {
          filter: "format",
          options: {
            pix_fmts: "nv12",
          },
        },
        { filter: "hwupload", options: {} },
      ];
    }
    if (inputFormat === "VAAPI" && filterDeviceFormat === "OPENCL") {
      return [{ filter: "hwmap", options: {} }];
    }
  }
  if (outputFormat === "VAAPI") {
    if (inputFormat === "OPENCL" && filterDeviceFormat === "OPENCL") {
      return [
        { filter: "hwmap", options: { derive_device: "vaapi", reverse: 1 } },
      ];
    }
    if (inputFormat === null && filterDeviceFormat === "VAAPI") {
      return [
        {
          filter: "format",
          options: {
            pix_fmts: "nv12",
          },
        },
        { filter: "hwupload", options: {} },
      ];
    }
  }
  throw Error(`Cannot convert between ${inputFormat} and ${outputFormat}`);
};

const getAnalysePipeline = ({
  destFileName,
}: {
  destFileName: string;
}): FilterPipeline => {
  return {
    inputFormat: null,
    filters: [
      {
        filter: "vidstabdetect",
        options: {
          result: `${destFileName}.trf`,
          shakiness: 10,
          mincontrast: 0.2,
          stepsize: 12,
        },
      },
    ],
    outputFormat: null,
  };
};

const combinePipelines = ({
  input,
  pipelines,
  output,
  filterDeviceFormat,
}: {
  input: InputConfiguration;
  pipelines: FilterPipeline[];
  output: OutputConfiguration;
  filterDeviceFormat: HardwarePixelFormat;
}): AudioVideoFilter[] => {
  const pipelineFilters = pipelines.reduce(
    (filters: AudioVideoFilter[], pipeline, index) => {
      return filters.concat(
        pipeline.filters,
        index < pipelines.length - 1
          ? getConversionFilters({
              inputFormat: pipeline.outputFormat,
              outputFormat: pipelines[index + 1].inputFormat,
              filterDeviceFormat,
            })
          : []
      );
    },
    []
  );
  if (pipelineFilters.length === 0) {
    return input.filters.concat(
      getConversionFilters({
        inputFormat: input.outputFormat,
        outputFormat: output.inputFormat,
        filterDeviceFormat,
      }),
      output.filters
    );
  }
  return input.filters.concat(
    getConversionFilters({
      inputFormat: input.outputFormat,
      outputFormat: pipelines[0].inputFormat,
      filterDeviceFormat,
    }),
    pipelineFilters,
    getConversionFilters({
      inputFormat: pipelines[pipelines.length - 1].outputFormat,
      outputFormat: output.inputFormat,
      filterDeviceFormat,
    }),
    output.filters
  );
};

const getStabilisePipeline = ({
  destFileName,
  stabilise,
  upsample,
  stabiliseBuffer,
  projection,
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
  destFileName: string;
}): FilterPipeline => {
  const useV360 = Boolean(
    projection && (projection !== "sg" || roll || pitch || yaw)
  );
  return {
    inputFormat: null,
    filters: [
      stabilise && {
        filter: "vidstabtransform",
        options: {
          input: `${destFileName}.trf`,
          optzoom: 0,
          zoom: -stabiliseBuffer,
          interpol: "bicubic",
          smoothing: 30,
          crop: "black",
        },
      },
      useV360 && {
        filter: "v360",
        options: {
          // Input: GoPro HERO5 Black
          input: "fisheye",
          id_fov: 145.8,

          // Output
          output: projection,
          // Diagonal FOV preserves the entire input in stereographic => rectilinear
          d_fov: (145.8 * 100) / (100 + (zoom || 0)),
          w: width || (inputWidth * (upsample || 100)) / 100,
          h: height || (inputHeight * (upsample || 100)) / 100,

          pitch: (((pitch || 0) + 180) % 360) - 180,
          yaw: (((yaw || 0) + 180) % 360) - 180,
          roll: (((roll || 0) + 180) % 360) - 180,

          interp: "lanc",
        },
      },
      (stabilise || useV360) && {
        filter: "format",
        options: {
          pix_fmts: "nv12",
        },
      },
    ].filter(notEmpty),
    outputFormat: null,
  };
};

const analyse = (
  sourceFileName: string,
  destFileName: string,
  options: RenderOptions
) =>
  analyseQueue.add(
    () =>
      new Promise(async (resolve, reject) => {
        const inputConfiguration = getInputConfiguration(options);
        const analysePipeline = getAnalysePipeline({ destFileName });
        if (analysePipeline.filters.length === 0) {
          return;
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
        const { width: inputWidth, height: inputHeight } = videoStream;
        if (inputWidth === undefined || inputHeight === undefined) {
          throw new Error("Failed to find video dimensions");
        }

        const inputConfiguration = getInputConfiguration(options);

        const stabilisePipeline = getStabilisePipeline({
          ...options,
          inputWidth,
          inputHeight,
          destFileName,
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
            combinePipelines({
              input: inputConfiguration,
              pipelines: [stabilisePipeline],
              output: outputConfiguration,
              filterDeviceFormat: inputConfiguration.filterDeviceFormat,
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
  const { encodeOnly, analyseOnly, stabilise } = options;
  if (!encodeOnly && stabilise) {
    await analyse(sourceFileName, destFileName, options);
  }
  if (!analyseOnly) {
    await encode(sourceFileName, destFileName, options);
  }
};
