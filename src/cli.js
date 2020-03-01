#!/usr/bin/env node

import 'source-map-support/register';
import "core-js/stable";
import "regenerator-runtime/runtime";

import commander from 'commander';

import { join } from './join';
import { render } from './render';

const wrapError = (action) => async () => {
  try {
    const result = await action(...commander.args);
  } catch (e) {
    console.error(e);
  }
};

const parseNumber = (input) => parseInt(input, 10);

commander
  .command('join <code>')
  .description('Join the segments of a video together into a single file')
  .option('-o, --output <output>', 'Path of resulting video')
  .action(wrapError(join));

commander
  .command('render <source> <dest>')
  .description('Extract part of a source video and write it to a file')
  .option('-s, --start <time>', 'The starting point in the source')
  .option('-d, --duration <time>', 'The duration of the output')
  .option('-e, --end <time>', 'The end point in the source')
  .option('-w, --width <pixels>', 'Output width (pixels)', parseNumber)
  .option('-h, --height <pixels>', 'Output height (pixels)', parseNumber)
  .option('-r, --roll <angle>', 'Turn camera clockwise by <degrees>', parseNumber)
  .option('-p, --pitch <degrees>', 'Turn camera up by <degrees>', parseNumber)
  .option('-y, --yaw <degrees>', 'Turn camera left by <degrees>', parseNumber)
  .option('-z, --zoom <percent>', 'Zoom camera by <percent>', parseNumber)
  .option('-u, --upsample <percent>', 'Scale video before processing', parseNumber)
  .option('--stabilise', 'Apply stabilisation to remove camera shaking')
  .option(
    '--stabilise-fisheye',
    'Convert to the equidistant fisheye projection before doing stabilisation (marginally reduces warping)',
  )
  .option(
    '--stabilise-buffer <percent>',
    'Percentage to zoom out during stabilisation (so you can see where the camera shakes to)',
    parseNumber,
    0,
  )
  .option(
    '-l, --lens-correct',
    'Correct lens distortion (from closest well known projection)',
  )
  .option(
    '--projection <projection>',
    'Use the specified lens projection (default rectilinear). See v360 filter docs for options.',
    'rectilinear',
  )
  .option(
    '-c, --encode-only',
    'Skip analyze stage, use existing stabilisation data if applicable',
  )
  .option(
    '-a, --analyse-only',
    'Skip encode stage, generate stabilisation data only',
  )
  .action(wrapError(render));

commander.parse(process.argv);
