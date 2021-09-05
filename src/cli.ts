#!/usr/bin/env node

import "source-map-support/register";
import "core-js/stable";
import "regenerator-runtime/runtime";

import { Command } from "commander";

import { join } from "./join";
import { render } from "./render";
import { identity, parseNumber } from "./utils";

const wrapError =
  (action: (...args: any[]) => Promise<unknown>) =>
  async (...args: unknown[]) => {
    console.log(args[2]);
    try {
      await action(...args);
    } catch (e) {
      console.error(e);
      process.exit(1);
    }
  };

const program = new Command();

program
  .command("join <code>")
  .description("Join the segments of a video together into a single file")
  .requiredOption("-o, --output <output>", "Path of resulting video")
  .action(wrapError(join));

program
  .command("render <source> <dest>")
  .description("Extract part of a source video and write it to a file")
  .option(
    "-s, --start <time>",
    "The starting point in the source",
    identity,
    null
  )
  .option("-d, --duration <time>", "The duration of the output", identity, null)
  .option("-e, --end <time>", "The end point in the source", identity, null)
  .option("-w, --width <pixels>", "Output width (pixels)", parseNumber, null)
  .option("-h, --height <pixels>", "Output height (pixels)", parseNumber, null)
  .option(
    "-r, --roll <angle>",
    "Turn camera clockwise by <degrees>",
    parseNumber,
    0
  )
  .option(
    "-p, --pitch <degrees>",
    "Turn camera up by <degrees>",
    parseNumber,
    0
  )
  .option(
    "-y, --yaw <degrees>",
    "Turn camera left by <degrees>",
    parseNumber,
    0
  )
  .option("-z, --zoom <percent>", "Zoom camera by <percent>", parseNumber, 0)
  .option(
    "-u, --upsample <percent>",
    "Scale video before processing",
    parseNumber,
    0
  )
  .option(
    "--crop <crop>",
    "Crop video (options for ffmpeg crop filter)",
    identity,
    null
  )
  .option("--stabilise", "Apply stabilisation to remove camera shaking", false)
  .option(
    "--stabilise-buffer <percent>",
    "Percentage to zoom out during stabilisation (so you can see where the camera shakes to)",
    parseNumber,
    0
  )
  .option(
    "--projection <projection>",
    "Use the specified lens projection (default rectilinear). See v360 filter docs for options.",
    identity,
    null
  )
  .option(
    "-c, --encode-only",
    "Skip analyze stage, use existing stabilisation data if applicable",
    false
  )
  .option(
    "-a, --analyse-only",
    "Skip encode stage, generate stabilisation data only",
    false
  )
  .option(
    "--hw-accel <hw-accel>",
    "Hardware decide acceleration type",
    identity,
    null
  )
  .option(
    "--vaapi-vendor <vendor>",
    "VAAPI device vendor (intel, amd)",
    identity,
    null
  )
  .option(
    "--open-cl-platform <platform>",
    "OpenCL platform number to use for filtering",
    identity,
    null
  )
  .option(
    "--no-map-open-cl-from-vaapi",
    "Use VAAPI device for OpenCL (Intel only)",
    false
  )
  .option(
    "--encoder <encoder>",
    "The encoder used for the output video",
    "libx264"
  )
  .action(wrapError(render));

program.parse(process.argv);
