import Ffmpeg from 'fluent-ffmpeg';

export const getMetadata = async (fileName) => new Promise((resolve, reject) =>
  Ffmpeg.ffprobe(fileName, (err, metadata) => {
    if (err) {
      return reject(err);
    }
    return resolve(metadata);
  }));
