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
  .option('--rotate <angle>', 'Optional angle to rotate the source by')
  .option('-l, --crop-left <percent>', 'Cropped prportion from the left', parseNumber)
  .option('-t, --crop-top <percent>', 'Cropped prportion from the top', parseNumber)
  .option('-r, --crop-right <percent>', 'Cropped prportion from the right', parseNumber)
  .option('-b, --crop-bottom <percent>', 'Cropped proportion from the bottom', parseNumber)
  .option('--upsample <percent>', 'Scale video before processing', parseNumber)
  .option('--resolution <resolution>', 'Optional height of output in pixels', parseNumber)
  .option('--stabilise', 'Apply stabilisation to remove camera shaking')
  .option('--pre-stabilise', 'Apply stabilisation to the entire input video')
  .option('-z, --zoom <percent>', 'Zoom (save regions streched out of the frame by lens correction, or zoom to the centre)', parseNumber)
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
    '-L, --lens-correct',
    'Correct lens distortion',
  )
  .option(
    '-p, --projection <projection>',
    'Use the specified lens projection (default fisheye_stereographic). See lensfun docs for options.',
    'fisheye_stereographic',
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
