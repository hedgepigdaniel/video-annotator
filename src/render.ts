import Ffmpeg, { AudioVideoFilter, FfmpegCommand } from "fluent-ffmpeg";
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

type AnalyseOptions = {
  start?: string;
  duration?: string;
  end?: string;
  upsample?: number;
  vaapiDevice?: string;
};

const analyse = (
  sourceFileName: string,
  destFileName: string,
  { start, duration, end, upsample, vaapiDevice }: AnalyseOptions
) =>
  analyseQueue.add(
    () =>
      new Promise(async (resolve, reject) => {
        const metadata = await getMetadata(sourceFileName);
        return Ffmpeg()
          .on("start", console.log)
          .on("codecData", console.log)
          .on("progress", ({ percent, currentFps }) =>
            console.log(`${Math.floor(percent)}% (${currentFps}fps)`)
          )
          .on("error", reject)
          .on("end", resolve)
          .input(sourceFileName)
          .inputOptions(
            [
              vaapiDevice && `-vaapi_device ${vaapiDevice}`,
              vaapiDevice && "-hwaccel vaapi",
              vaapiDevice && "-hwaccel_output_format vaapi",
              start && `-ss ${start}`,
              duration && `-t ${duration}`,
              end && `-to ${end}`,
            ].filter(notEmpty)
          )
          .videoFilters(
            [
              upsample && {
                filter: vaapiDevice ? "scale_vaapi" : "scale",
                options: {
                  w: `iw*${(upsample || 100) / 100}`,
                  h: `ih*${(upsample || 100) / 100}`,
                },
              },
              vaapiDevice
                ? {
                    filter: "hwdownload",
                    options: {},
                  }
                : undefined,
              {
                filter: "format",
                options: {
                  pix_fmts: "nv12",
                },
              },
              {
                filter: "vidstabdetect",
                options: {
                  result: `${destFileName}.trf`,
                  shakiness: 10,
                  mincontrast: 0.2,
                  stepsize: 12,
                },
              },
            ].filter(notEmpty)
          )
          .format("null")
          .output("-")
          .run();
      })
  );

type EncodeOptions = {
  start?: string;
  duration?: string;
  end?: string;
  roll?: number;
  pitch?: number;
  yaw?: number;
  width?: number;
  height?: number;
  upsample?: number;
  stabilise?: boolean;
  stabiliseBuffer: number;
  projection?: string;
  zoom?: number;
  crop?: string;
  resolution?: string;
  vaapiDevice?: string;
  encoder: string;
};

const encode = async (
  sourceFileName: string,
  destFileName: string,
  {
    start,
    duration,
    end,
    roll,
    pitch,
    yaw,
    width,
    height,
    upsample,
    stabilise,
    stabiliseBuffer,
    projection,
    zoom,
    crop,
    resolution,
    vaapiDevice,
    encoder,
  }: EncodeOptions
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
        const useV360 = Boolean(
          projection && (projection !== "sg" || roll || pitch || yaw)
        );
        const isVaapiEncoder = encoder.indexOf("vaapi") != -1;
        const isAmfEncoder = encoder.indexOf("amf") != -1;
        const download = Boolean(vaapiDevice && (useV360 || stabilise));
        return Ffmpeg()
          .on("start", console.log)
          .on("codecData", console.log)
          .on("progress", ({ percent, currentFps }) =>
            console.log(`${Math.floor(percent)}% (${currentFps}fps)`)
          )
          .on("error", reject)
          .on("end", resolve)
          .input(sourceFileName)
          .inputOptions(
            [
              vaapiDevice && `-vaapi_device ${vaapiDevice}`,
              vaapiDevice && "-hwaccel vaapi",
              vaapiDevice &&
                (download || isVaapiEncoder) &&
                "-hwaccel_output_format vaapi",
              start && `-ss ${start}`,
              duration && `-t ${duration}`,
              end && `-to ${end}`,
            ].filter(notEmpty)
          )
          .videoFilters(
            [
              upsample && {
                filter: vaapiDevice ? "scale_vaapi" : "scale",
                options: {
                  w: `iw*${upsample / 100}`,
                  h: `ih*${upsample / 100}`,
                  mode: "hq",
                },
              },
              download && {
                filter: "hwdownload",
                options: {},
              },
              download && {
                filter: "format",
                options: {
                  pix_fmts: "nv12",
                },
              },
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
                  input: "sg",
                  // v360 has incorrect calculation of horizontal/vertical FOV from vertical FOV
                  ih_fov: 122.6,
                  iv_fov: 94.4,

                  // Output
                  output: projection,
                  // Diagonal FOV preserves the entire input in stereographic => rectilinear
                  d_fov: (149.2 * 100) / (100 + (zoom || 0)),
                  w: width || (inputWidth * (upsample || 100)) / 100,
                  h: height || (inputHeight * (upsample || 100)) / 100,

                  pitch: (((pitch || 0) - 90 + 180) % 360) - 180,
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
              (!vaapiDevice || download) &&
                isVaapiEncoder && {
                  filter: "hwupload",
                  options: {},
                },
              crop
                ? {
                    filter: `crop=${crop}`,
                    options: {},
                  }
                : undefined,
              resolution || crop
                ? {
                    filter:
                      isVaapiEncoder ||
                      (isAmfEncoder && vaapiDevice && !download)
                        ? "scale_vaapi"
                        : "scale",
                    options: resolution
                      ? {
                          w: `iw*${resolution}/ih`,
                          h: `${resolution}`,
                        }
                      : {},
                  }
                : undefined,
              isAmfEncoder && vaapiDevice && download
                ? {
                    filter: "hwmap",
                    options: {},
                  }
                : undefined,
            ].filter(notEmpty)
          )
          .output(destFileName)
          .outputOptions([`-c:v ${encoder}`, `-qp ${VAAPI_QP}`].filter(Boolean))
          .run();
      })
  );

type RenderOptions = AnalyseOptions &
  EncodeOptions & {
    encodeOnly?: boolean;
    analyseOnly?: boolean;
  };

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
