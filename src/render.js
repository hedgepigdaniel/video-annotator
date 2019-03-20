import Ffmpeg from 'fluent-ffmpeg';
import Queue from 'promise-queue';

import { getMetadata } from './utils';

const VAAPI_DEVICE = '/dev/dri/renderD128';

/**
 * 19 - "visually lossless"
 * 23 - great
 * 25 - pretty good
 * 28 - a bit dodgy
 */
const VAAPI_QP = 23;

const analyseQueue = new Queue(2);
const encodeQueue = new Queue(4);

const analyse = (sourceFileName, destFileName, { start, duration, end }) =>
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
        start && `-ss ${start}`,
        duration && `-t ${duration}`,
        end && `-to ${end}`,
      ].filter(Boolean))
      .videoFilters([
        {
          filter: 'vidstabdetect',
          options: {
            result: `${destFileName}.trf`,
            shakiness: 10,
            mincontrast: 0.2,
            stepsize: 12,
          },
        },
      ])
      .format('null')
      .output('-')
      .run())
  );

const encode = async (sourceFileName, destFileName, { start, duration, end, rotate, crop }) =>
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
      start && `-ss ${start}`,
      duration && `-t ${duration}`,
      end && `-to ${end}`,
    ].filter(Boolean))
    .videoFilters([
      {
        filter: 'vidstabtransform',
        options: {
          input: `${destFileName}.trf`,
          optzoom: 0,
          interpol: 'bicubic',
          smoothing: 30,
          crop: 'black',
        },
      },
      {
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
        filter: 'lensfun',
        options: {
          make: 'GoPro',
          model: 'HERO5 Black',
          lens_model: 'fixed lens',
          mode: 'geometry',
          target_geometry: 'rectilinear',
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
      crop && {
        filter: `crop=${crop}`,
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
  } = options;
  if (!encodeOnly) {
    await analyse(sourceFileName, destFileName, options)
  }
  if (!analyseOnly) {
    await encode(sourceFileName, destFileName, options);
  }
};
