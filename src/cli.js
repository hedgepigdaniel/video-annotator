import 'source-map-support/register'

import Vorpal from 'vorpal';

const vorpal = Vorpal();

import { join } from './join';
import { render, analyse, encode } from './render';

const callbackify = (action) => (args, callback) =>
  new Promise((resolve, reject) => action(args)
    .then(() => callback(null))
    .catch(callback));

vorpal
  .command('join <code>')
  .description('Join the segments of a video together into a single file')
  .action(callbackify(join));

vorpal
  .command('render <source> <dest>')
  .description('Extract part of a source video and write it to a file')
  .option('-s, --start <time>', 'The starting point in the source')
  .option('-d, --duration <time>', 'The duration of the output')
  .option('-e, --end <time>', 'The end point in the source')
  .option('--rotate <angle>', 'Optional angle to rotate the source by')
  .option('--crop-left, -l <percent>', 'Cropped prportion from the left')
  .option('--crop-top, -t <percent>', 'Cropped prportion from the top')
  .option('--crop-right, -r <percent>', 'Cropped prportion from the right')
  .option('--crop-bottom, -b <percent>', 'Cropped proportion from the bottom')
  .option('--scale <percent>', 'Scale video before processing')
  .option('--stabilise', 'Apply stabilisation to remove camera shaking')
  .option('--pre-stabilise', 'Apply stabilisation to the entire input video')
  .option('--zoom-out, -z <percent>', 'Zoom out (and save regions streched out of the frame by lens correction)')
  .option(
    '--stabilise-fisheye',
    'Convert to the equidistant fisheye projection before doing stabilisation (marginally reduces warping)',
  )
  .option(
    '--stabilise-buffer <percent>',
    'Percentage to zoom out during stabilisation (so you can see where the camera shakes to)',
  )
  .option(
    '-L, --lens-correct',
    'Correct lens distortion',
  )
  .option(
    '-p, --projection <projection>',
    'Use the specified lens projection (default fisheye_stereographic). See lensfun docs for options.',
  )
  .option(
    '-c, --encode-only',
    'Skip analyze stage, use existing stabilisation data if applicable',
  )
  .option(
    '-a, --analyse-only',
    'Skip encode stage, generate stabilisation data only',
  )
  .action(callbackify(render));

vorpal.parse(process.argv);
