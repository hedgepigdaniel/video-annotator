import Ffmpeg from 'fluent-ffmpeg';
import Queue from 'promise-queue';

import { getMetadata } from './utils';


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

const analyse = (sourceFileName, destFileName, {
  start,
  duration,
  end,
  lensCorrect,
  stabiliseFisheye,
  upsample,
  width,
  height,
  vaapiDevice,
}) =>
  analyseQueue.add(
    () => new Promise(async (resolve, reject) => {
      const metadata = await getMetadata(sourceFileName);
      const { width: inputWidth, height: inputHeight } = metadata.streams.find(
        (stream) => stream.codec_type === 'video',
      );
      return Ffmpeg()
        .on('start', console.log)
        .on('codecData', console.log)
        .on('progress', ({ percent, currentFps }) =>
          console.log(`${Math.floor(percent)}% (${currentFps}fps)`))
        .on('error', reject)
        .on('end', resolve)
        .input(sourceFileName)
        .inputOptions([
          `-vaapi_device ${vaapiDevice}`,
          '-hwaccel vaapi',
          start  && `-ss ${start}`,
          duration && `-t ${duration}`,
          end && `-to ${end}`,
        ].filter(Boolean))
        .videoFilters([
          upsample && {
            filter: 'scale_vaapi',
            options: {
              w: `iw*${upsample / 100}`,
              h: `ih*${upsample / 100}`,
            },
          },
          upsample && {
            filter: 'hwdownload',
          },
          upsample && {
            filter: 'format',
            options: {
              pix_fmts: 'nv12',
            },
          },
          lensCorrect && {
            filter: 'lenscorrection',
            options: {
              k1: -0.03012,
            },
          },
          stabiliseFisheye && {
            filter: 'v360',
            options: {
              // Input: GoPro HERO5 Black
              input: 'sg',
              // v360 has incorrect calculation of horizontal/vertical FOV from vertical FOV
              ih_fov: 122.6,
              iv_fov: 94.4,

              // Output
              output: 'fisheye',
              d_fov: 149.2,
              // Diagonal FOV preserves the entire input in stereographic => rectilinear
              w: width || inputWidth * (upsample || 100) / 100,
              h: height || inputHeight * (upsample || 100) / 100,
              pitch: -90,

            },
          },
          stabiliseFisheye && {
            filter: 'format',
            options: {
              pix_fmts: 'nv12',
            },
          },
          {
            filter: 'vidstabdetect',
            options: {
              result: `${destFileName}.trf`,
              shakiness: 10,
              mincontrast: 0.2,
              stepsize: 12,
            },
          },
        ].filter(Boolean))
        .format('null')
        .output('-')
        .run();
      })
  );

const encode = async (sourceFileName, destFileName, {
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
  stabiliseFisheye,
  stabiliseBuffer,
  lensCorrect,
  projection,
  zoom,
  crop,
  resolution,
  vaapiDevice,
}) =>
  encodeQueue.add(() => new Promise(async (resolve, reject) => {
    const metadata = await getMetadata(sourceFileName);
    const { width: inputWidth, height: inputHeight } = metadata.streams.find(
      (stream) => stream.codec_type === 'video',
    );
    const useV360 = (
      (projection && projection !== (stabiliseFisheye ? 'fisheye' : 'sg'))
      || roll || pitch || yaw
    );
    const download = useV360 || lensCorrect || stabilise;
    return Ffmpeg()
      .on('start', console.log)
      .on('codecData', console.log)
      .on('progress', ({ percent, currentFps }) =>
        console.log(`${Math.floor(percent)}% (${currentFps}fps)`))
      .on('error', reject)
      .on('end', resolve)
      .input(sourceFileName)
      .inputOptions([
        `-vaapi_device ${vaapiDevice}`,
        '-hwaccel vaapi',
        '-hwaccel_output_format vaapi',
        start && `-ss ${start}`,
        duration && `-t ${duration}`,
        end && `-to ${end}`,
      ].filter(Boolean))
      .videoFilters([
        upsample && {
          filter: 'scale_vaapi',
          options: {
            w: `iw*${upsample / 100}`,
            h: `ih*${upsample / 100}`,
            mode: 'hq',
          },
        },
        download && {
          filter: 'hwdownload',
        },
        download && {
          filter: 'format',
          options: {
            pix_fmts: 'nv12',
          },
        },
        lensCorrect && {
          filter: 'lenscorrection',
          options: {
            k1: -0.03012,
          },
        },
        stabiliseFisheye && {
          filter: 'v360',
          options: {
            // Input: GoPro HERO5 Black
            input: 'sg',
            // v360 has incorrect calculation of horizontal/vertical FOV from vertical FOV
            ih_fov: 122.6,
            iv_fov: 94.4,

            // Output
            output: 'fisheye',
            d_fov: 149.2,
            // Diagonal FOV preserves the entire input in stereographic => rectilinear
            w: width || inputWidth,
            h: height || inputHeight,
            pitch: -90,

            interp: upsample ? 'linear' : 'lanc',
          },
        },
        stabilise && {
          filter: 'vidstabtransform',
          options: {
            input: `${destFileName}.trf`,
            optzoom: 0,
            zoom: -stabiliseBuffer,
            interpol: 'bicubic',
            smoothing: 30,
            crop: 'black',
          },
        },
        useV360 && {
          filter: 'v360',
          options: {
            ...(stabiliseFisheye ? {
              // Input: corrected fisheye projection
              input: 'fisheye',
              // This makes sense because the fisheye projection is linear
              id_fov: 149.2 * (1 + (stabiliseBuffer || 0) / 100),
            } : {
              // Input: GoPro HERO5 Black
              input: 'sg',
              // v360 has incorrect calculation of horizontal/vertical FOV from vertical FOV
              ih_fov: 122.6,
              iv_fov: 94.4,
            }),

            // Output
            output: projection,
            // Diagonal FOV preserves the entire input in stereographic => rectilinear
            d_fov: 149.2 * 100 / (100 + (zoom || 0)),
            w: width || inputWidth * (upsample || 100) / 100,
            h: height || inputHeight * (upsample || 100) / 100,

            pitch: ((pitch || 0) - (stabiliseFisheye ? 0 : 90) + 180) % 360 - 180,
            yaw: ((yaw || 0) + 180) % 360 - 180,
            roll : ((roll || 0) + 180) % 360 - 180,

            interp: 'lanc',
          },
        },
        (stabilise || useV360) && {
          filter: 'format',
          options: {
            pix_fmts: 'nv12',
          },
        },
        download && {
          filter: 'hwupload',
        },
        crop && `crop=${crop}`,
        (resolution || crop) && {
          filter: 'scale_vaapi',
          options: resolution ? {
            w: `iw*${resolution}/ih`,
            h: `${resolution}`,
          } : {},
        },
      ].filter(Boolean))
      .output(destFileName)
      .outputOptions([
        '-c:v hevc_vaapi',
        `-qp ${VAAPI_QP}`,
      ].filter(Boolean))
      .run();
    }));


export const render = async (sourceFileName, destFileName, options) => {
  const { encodeOnly, analyseOnly, stabilise } = options;
  if (!encodeOnly && stabilise) {
    await analyse(sourceFileName, destFileName, options)
  }
  if (!analyseOnly) {
    await encode(sourceFileName, destFileName, options);
  }
};
