import Ffmpeg from 'fluent-ffmpeg';
import { promises as fs } from 'fs';
import { resolve, dirname } from 'path';

import { getMetadata } from './utils';

const findSourceSegments = async (code) => {
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

const findNumFrames = async (sourceSegments) => {
  const metadatas = await Promise.all(sourceSegments.map(getMetadata));
  return metadatas
    .map((metadata) => metadata.streams[0].nb_frames)
    .reduce((sum, frames) => sum + frames, 0);
};

export const join = async ({ code, options: { output } }) => {
  const outputFile = output || `${code}.mkv`;
  const sourceSegments = await findSourceSegments(code);
  console.log('Found source segments:\n', sourceSegments);
  const totalFrames = await findNumFrames(sourceSegments);
  const concatFileName = resolve(dirname(outputFile), `${code}.source.txt`);
  const concatFileContents = sourceSegments
    .map((segment) => `file '${resolve(segment)}'`)
    .join('\n');
  await fs.writeFile(concatFileName, concatFileContents)
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
      .outputOptions(['-c copy'])
      .run();
  });
};
