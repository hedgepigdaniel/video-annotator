#!/usr/bin/env node

import "source-map-support/register";
import "core-js/stable";
import "regenerator-runtime/runtime";

import { Command } from "commander";

import { join } from "./join";
import { render } from "./render";
import { identity, parseArray, parseNumber } from "./utils";

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
  .option(
    "--filter <filter>",
    "Choose the FFmpeg filter to use (vidstab, deshake, deshake_opencl, dewobble)",
    "dewobble"
  )
  .option(
    "--stabilise <type>",
    "Apply stabilisation to remove camera shaking (none, fixed, smooth)",
    "none"
  )
  .option(
    "--stabilise-radius <number>",
    "the number of frames to look ahead and behind for stabilisation",
    parseNumber,
    90
  )
  .option(
    "--interpolate-radius <number>",
    "the number of frames to look behind to interpolate camera position",
    parseNumber,
    30
  )
  .option(
    "--stabilise-buffer <percent>",
    "Buffer space to add around input during stabilisation to avoid cropping",
    parseNumber,
    20
  )
  .option(
    "--input-dfov <degrees>",
    "Diagonal field of view of the input camera",
    parseNumber,
    145.8
  )
  .option(
    "--output-dfov <degrees>",
    "Diagonal field of view of the input camera",
    parseNumber,
    null
  )
  .option(
    "--projection <projection>",
    "Use the specified lens projection. See v360 filter docs for options.",
    identity,
    "rect"
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
    "--copy-vaapi-frames",
    "Copy input VAAPI frames (useful to avoid hardware frame pool limit)",
    false
  )
  .option(
    "--encoder <encoder>",
    "The encoder used for the output video",
    "libx264"
  )
  .option(
    "--frame-rate <fps>",
    "Speed up or slow down output video by setting frame rate",
    parseNumber,
    null
  )
  .option("--compare <filters>", "Compare multiple filters", parseArray, null)
  .option("--debug", "Include debugging information in the output", false)
  .option("-v, --verbosity <level>", "FFmpeg logging verbosity", identity, null)
  .action(wrapError(render));

program.parse(process.argv);
