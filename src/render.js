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
  upsample,
  preStabilise,
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
        upsample && '-hwaccel_output_format vaapi',
        start && !preStabilise && `-ss ${start}`,
        duration && !preStabilise && `-t ${duration}`,
        end && !preStabilise && `-to ${end}`,
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
            result: `${preStabilise ? sourceFileName : destFileName}.trf`,
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
  cropRight,
  cropBottom,
  upsample,
  stabilise,
  stabiliseBuffer,
  preStabilise,
  stabiliseFisheye,
  lensCorrect,
  projection,
  zoom,
  resolution,
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
      upsample && '-hwaccel_output_format vaapi',
      start && !preStabilise && `-ss ${start}`,
      duration && !preStabilise && `-t ${duration}`,
      end && !preStabilise && `-to ${end}`,
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
          input: `${preStabilise ? sourceFileName : destFileName}.trf`,
          optzoom: 0,
          zoom: -stabiliseBuffer,
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
          scale: zoom ? zoom / 100 : 1,
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
      (cropLeft || cropTop || cropRight || cropBottom) && {
        filter: `crop`,
        options: {
          x: `${cropLeft / 100}*iw`,
          y: `${cropTop / 100}*ih`,
          w: `${1 - (cropLeft + cropRight) / 100}*iw`,
          h: `${1 - (cropTop + cropBottom) / 100}*ih`,
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
      resolution && {
        filter: 'scale_vaapi',
        options: {
          w: `iw*${resolution}/ih`,
          h: `${resolution}`,
        },
      },
    ].filter(Boolean))
    .output(destFileName)
    .outputOptions([
      '-c:v hevc_vaapi',
      `-qp ${VAAPI_QP}`,
      start && preStabilise && `-ss ${start}`,
      duration && preStabilise && `-t ${duration}`,
      end && preStabilise && `-to ${end}`,
    ].filter(Boolean))
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
    'stabilise-buffer': stabiliseBuffer = 0,
    'lens-correct': lensCorrect,
    'zoom': zoom,
    'crop-left': cropLeft,
    'crop-top': cropTop,
    'crop-right': cropRight,
    'crop-bottom': cropBottom,
    projection = 'fisheye_stereographic',
    stabilise,
    'pre-stabilise': preStabilise,
    ...otherOptions
  } = options;
  const optionsWithDefaults = {
    stabiliseFisheye,
    stabiliseBuffer,
    stabilise,
    lensCorrect,
    projection,
    zoom,
    cropLeft,
    cropTop,
    cropRight,
    cropBottom,
    preStabilise,
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
