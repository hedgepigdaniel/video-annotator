import Ffmpeg, { FfprobeData } from 'fluent-ffmpeg';

export const getMetadata = async (fileName: string): Promise<FfprobeData> => new Promise((resolve, reject) =>
  Ffmpeg.ffprobe(fileName, (err: Error, metadata) => {
    if (err) {
      return reject(err);
    }
    return resolve(metadata);
  }));

export const parseNumber = (input: string) => {
  const result = parseInt(input, 10);
  if (result === NaN) {
    throw new Error(`Failed to parse number: ${input}`);
  }
  return result;
}

export const notEmpty =<Value>(value: Value | null | undefined | false | 0): value is Value => Boolean(value);