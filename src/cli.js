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
  .option('-t, --duration <time>', 'The duration of the output')
  .option('-e, --encode-only', 'Skip analyze stage, use existing stabilisation data if applicable')
  .option('-e, --analyse-only', 'Skip encode stage, generate stabilisation data only')
  .action(callbackify(render));

vorpal.parse(process.argv);
