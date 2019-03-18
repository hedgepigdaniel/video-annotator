import Ffmpeg from 'fluent-ffmpeg';

const VAAPI_DEVICE = '/dev/dri/renderD128';
const VAAPI_QP = 18;

const analyse = (sourceFileName, destFileName, { start, duration }) => {
  return new Promise((resolve, reject) => Ffmpeg()
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
    ].filter(Boolean))
    .videoFilters([
      {
        filter: 'vidstabdetect',
        options: {
          result: `${destFileName}.trf`,
        },
      },
    ])
    .format('null')
    .output('-')
    .run());
};

const encode = async (sourceFileName, destFileName, { start, duration }) => {
  return new Promise((resolve, reject) => Ffmpeg()
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
    ].filter(Boolean))
    .videoFilters([
      {
        filter: 'vidstabtransform',
        options: {
          input: `${destFileName}.trf`,
          optzoom: 0,
          interpol: 'bicubic',
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
      // {
      //   filter: 'lenscorrection',
      //   options: {
      //     cx: 0.5,
      //     cy: 0.5,
      //     k1: -0.25,
      //     k2: 0.022,
      //   },
      // },
      {
        filter: 'lensfun',
        options: {
          make: 'GoPro',
          model: 'HERO5 Black',
          lens_model: 'fixed lens',
          mode: 'geometry',
          // interpolation: 'lanczos' // Slow
        },
      },
      {
        filter: 'rotate',
        options: {
          angle: `2*PI/9`,
          out_w: `hypot(iw,ih)`,
          out_h: `out_w`,
        },
      },
      {
        filter: 'crop',
        options: {
          out_w: 1900,
          out_h: 1600,
          x: 200,
          y: 700,
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
    ])
    .output(destFileName)
    .outputOptions([
      '-c:v h264_vaapi',
      `-qp ${VAAPI_QP}`,
    ])
    .run());
}

export const render = async ({
  source: sourceFileName,
  dest: destFileName,
  options,
}) => {
  const {
    start,
    duration,
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
