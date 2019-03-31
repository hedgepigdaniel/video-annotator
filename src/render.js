import Ffmpeg from 'fluent-ffmpeg';
import Queue from 'promise-queue';

import { getMetadata } from './utils';

const VAAPI_DEVICE = '/dev/dri/renderD128';

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
  stabilise,
  stabiliseFisheye,
  scale,
}) =>
  analyseQueue.add(
    () => new Promise((resolve, reject) => Ffmpeg()
      .on('start', console.log)
      .on('codecData', console.log)
      .on('progress', ({ percent, currentFps }) =>
        console.log(`${Math.floor(percent)}% (${currentFps}fps)`))
      .on('error', reject)
      .on('end', resolve)
      .input(sourceFileName)
      .inputOptions([
        `-vaapi_device ${VAAPI_DEVICE}`,
        '-hwaccel vaapi',
        scale && '-hwaccel_output_format vaapi',
        start && `-ss ${start}`,
        duration && `-t ${duration}`,
        end && `-to ${end}`,
      ].filter(Boolean))
      .videoFilters([
        scale && {
          filter: 'scale_vaapi',
          options: {
            w: `iw*${scale / 100}`,
            h: `ih*${scale / 100}`,
          },
        },
        scale && {
          filter: 'hwdownload',
        },
        scale && {
          filter: 'format',
          options: {
            pix_fmts: 'nv12',
          },
        },
        stabiliseFisheye && {
          filter: 'lensfun',
          options: {
            make: 'GoPro',
            model: 'HERO5 Black',
            lens_model: 'fixed lens',
            mode: 'geometry',
            target_geometry: 'fisheye',
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
      .run())
  );

const encode = async (sourceFileName, destFileName, {
  start,
  duration,
  end,
  rotate,
  cropLeft,
  cropTop,
  cropWidth,
  cropHeight,
  scale,
  stabilise,
  stabiliseFisheye,
  lensCorrect,
  projection,
  zoomOut,
}) =>
  encodeQueue.add(() => new Promise((resolve, reject) => Ffmpeg()
    .on('start', console.log)
    .on('codecData', console.log)
    .on('progress', ({ percent, currentFps }) =>
      console.log(`${Math.floor(percent)}% (${currentFps}fps)`))
    .on('error', reject)
    .on('end', resolve)
    .input(sourceFileName)
    .inputOptions([
      `-vaapi_device ${VAAPI_DEVICE}`,
      '-hwaccel vaapi',
      scale && '-hwaccel_output_format vaapi',
      start && `-ss ${start}`,
      duration && `-t ${duration}`,
      end && `-to ${end}`,
    ].filter(Boolean))
    .videoFilters([
      scale && {
        filter: 'scale_vaapi',
        options: {
          w: `iw*${scale / 100}`,
          h: `ih*${scale / 100}`,
        },
      },
      scale && {
        filter: 'hwdownload',
      },
      scale && {
        filter: 'format',
        options: {
          pix_fmts: 'nv12',
        },
      },
      stabilise && stabiliseFisheye && {
        filter: 'lensfun',
        options: {
          make: 'GoPro',
          model: 'HERO5 Black',
          lens_model: 'fixed lens',
          mode: 'geometry',
          target_geometry: 'fisheye',
        },
      },
      stabilise && stabiliseFisheye && {
        filter: 'format',
        options: {
          pix_fmts: 'nv12',
        },
      },
      stabilise && {
        filter: 'vidstabtransform',
        options: {
          input: `${destFileName}.trf`,
          optzoom: 0,
          interpol: 'bicubic',
          smoothing: 30,
          crop: 'black',
        },
      },
      stabilise && stabiliseFisheye && projection !== 'fisheye' && {
        filter: 'format',
        options: {
          pix_fmts: 'nv12',
        },
      },
      stabilise && stabiliseFisheye && projection !== 'fisheye' && {
        filter: 'lensfun',
        options: {
          make: 'GoPro',
          model: 'HERO5 Black',
          lens_model: 'fixed lens',
          mode: 'geometry',
          target_geometry: 'fisheye',
          reverse: 1,
        },
      },
      lensCorrect && projection && ((stabilise && !stabiliseFisheye) || projection !== 'fisheye') && {
        filter: 'format',
        options: {
          pix_fmts: 'nv12',
        },
      },
      lensCorrect && projection && ((stabilise && !stabiliseFisheye) || projection !== 'fisheye') && {
        filter: 'lensfun',
        options: {
          make: 'GoPro',
          model: 'HERO5 Black',
          lens_model: 'fixed lens',
          mode: 'geometry',
          target_geometry: projection,
          scale: zoomOut ? (1 - zoomOut / 100) : 1,
        },
      },
      rotate && {
        filter: 'rotate',
        options: {
          angle: rotate,
          out_w: `rotw(${rotate})`,
          out_h: `roth(${rotate})`,
        },
      },
      (cropLeft || cropTop || cropWidth || cropHeight) && {
        filter: `crop`,
        options: {
          x: `${cropLeft / 100}*iw`,
          y: `${cropTop / 100}*ih`,
          w: `${cropWidth / 100}*iw`,
          h: `${cropHeight / 100}*ih`,
        },
      },
      stabilise && {
        filter: 'format',
        options: {
          pix_fmts: 'nv12',
        },
      },
      stabilise && {
        filter: 'unsharp',
        options: {
          luma_msize_x: 5,
          luma_msize_y: 5,
          luma_amount: 0.8,
          chroma_msize_x: 5,
          chroma_msize_y: 5,
          chroma_amount: 0.4,
        },
      },
      {
        filter: 'format',
        options: {
          pix_fmts: 'nv12',
        },
      },
      {
        filter: 'hwupload',
      },
    ].filter(Boolean))
    .output(destFileName)
    .outputOptions([
      '-c:v hevc_vaapi',
      `-qp ${VAAPI_QP}`,
    ])
    .run()));


export const render = async ({
  source: sourceFileName,
  dest: destFileName,
  options,
}) => {
  const {
    'encode-only': encodeOnly,
    'analyse-only': analyseOnly,
    'stabilise-fisheye': stabiliseFisheye,
    'lens-correct': lensCorrect,
    'zoom-out': zoomOut,
    'crop-left': cropLeft,
    'crop-top': cropTop,
    'crop-width': cropWidth,
    'crop-height': cropHeight,
    projection = 'fisheye_stereographic',
    stabilise,
    ...otherOptions
  } = options;
  const optionsWithDefaults = {
    stabiliseFisheye,
    stabilise,
    lensCorrect,
    projection,
    zoomOut,
    zoomOut,
    cropLeft,
    cropTop,
    cropWidth,
    cropHeight,
    ...otherOptions
  };
  console.log(optionsWithDefaults);
  if (!encodeOnly && stabilise) {
    await analyse(sourceFileName, destFileName, optionsWithDefaults)
  }
  if (!analyseOnly) {
    await encode(sourceFileName, destFileName, optionsWithDefaults);
  }
};
