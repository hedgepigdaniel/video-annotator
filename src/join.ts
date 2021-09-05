import Ffmpeg from 'fluent-ffmpeg';
import { promises as fs } from 'fs';
import { resolve, dirname } from 'path';

import { getMetadata, notEmpty, parseNumber } from './utils';

const findSourceSegments = async (code: string) => {
  const segments = [`GOPR${code}.MP4`];
  let initialStat;
  try {
    initialStat = await fs.stat(segments[0]);
  } catch {
    throw Error(`${segments[0]} does not exist!`);
  }
  if (!initialStat.isFile()) {
    throw Error(`Failed to find initial segment ${segments[0]}`)
  }
  while (true) {
    const filename = `GP${segments.length.toString().padStart(2, '0')}${code}.MP4`;
    let stat;
    try {
      stat = await fs.stat(filename);
    } catch {
      break;
    }
    if (!stat.isFile()) {
      break;
    }
    segments.push(filename);
  }
  return segments;
}

const findNumFrames = async (sourceSegments: string[]) => {
  const metadatas = await Promise.all(sourceSegments.map(getMetadata));
  return metadatas
    .map((metadata) => metadata.streams[0].nb_frames)
    .reduce((sum: number, frames) => sum + parseNumber(frames || '0'), 0);
};

const isTruthy = (val: unknown): val is true | string => Boolean(val)

export const join = async (code: string, { output }: { output: string }) => {
  const outputFile = output || `${code}.mp4`;
  const sourceSegments = await findSourceSegments(code);
  console.log('Found source segments:\n', sourceSegments);
  const totalFrames = await findNumFrames(sourceSegments);
  const concatFileName = resolve(dirname(outputFile), `${code}.source.txt`);
  const concatFileContents = sourceSegments
    .map((segment) => `file '${resolve(segment)}'`)
    .join('\n');
  await fs.writeFile(concatFileName, concatFileContents)
  const { streams } = await getMetadata(sourceSegments[0]);
  const gpmdStreamIndex = streams.findIndex(
    (stream) => (stream.tags || {}).handler_name === '\tGoPro MET',
  );
  return new Promise((resolve, reject) => {
    Ffmpeg()
      .on('start', console.log)
      .on('codecData', console.log)
      .on('progress', ({ frames, currentFps }) =>
        console.log(`${Math.floor(100.0 * frames / totalFrames)}% (${currentFps}fps)`))
      .on('error', reject)
      .on('end', resolve)
      .input(concatFileName)
      .inputOptions(['-f concat', '-safe 0'])
      .output(outputFile)
      .outputOptions([
        '-c copy',
        '-map 0:v',
        '-map 0:a',
        gpmdStreamIndex !== -1 && `-map 0:${gpmdStreamIndex}`,
      ].filter(notEmpty))
      .run();
  });
};
