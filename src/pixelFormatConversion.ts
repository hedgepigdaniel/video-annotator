import { AudioVideoFilter } from "fluent-ffmpeg";
import { connectFilters } from "./connectFilters";
import { HardwarePixelFormat } from "./pixelFormats";
import { notEmpty } from "./utils";

const getConversionFilters = ({
  inputFormat,
  outputFormat,
  filterDeviceFormat,
  openclMappedFromVaapi,
}: {
  inputFormat: HardwarePixelFormat;
  outputFormat: HardwarePixelFormat;
  filterDeviceFormat: HardwarePixelFormat;
  openclMappedFromVaapi: boolean;
}): AudioVideoFilter[] => {
  if (inputFormat === outputFormat) {
    return [];
  }
  if (outputFormat === null) {
    return [
      { filter: "hwdownload", options: {} },
      {
        filter: "format",
        options: {
          pix_fmts: "nv12",
        },
      },
    ];
  }
  if (outputFormat === "OPENCL") {
    if (inputFormat === "VAAPI" && openclMappedFromVaapi) {
      if (filterDeviceFormat === "OPENCL") {
        return [{ filter: "hwmap", options: {} }];
      }
      if (filterDeviceFormat === "VAAPI") {
        return [{ filter: "hwmap", options: { derive_device: "vaapi" } }];
      }
    }
    if (filterDeviceFormat === "OPENCL") {
      return [
        inputFormat !== null && { filter: "hwdownload", options: {} },
        {
          filter: "format",
          options: {
            pix_fmts: "nv12",
          },
        },
        { filter: "hwupload", options: {} },
      ].filter(notEmpty);
    }
  }
  if (outputFormat === "VAAPI") {
    if (inputFormat === "OPENCL" && openclMappedFromVaapi) {
      return [
        { filter: "hwmap", options: { derive_device: "vaapi", reverse: 1 } },
      ];
    }
    if (filterDeviceFormat === "VAAPI") {
      return [
        inputFormat !== null && {
          filter: "hwdownload",
          options: {},
        },
        {
          filter: "format",
          options: {
            pix_fmts: "nv12",
          },
        },
        { filter: "hwupload", options: {} },
      ].filter(notEmpty);
    }
  }
  throw Error(`Cannot convert between ${inputFormat} and ${outputFormat}`);
};

export const getConversionFilterSpecs = ({
  generatePadName,
  inputPad,
  outputPad,
  inputFormat,
  outputFormat,
  filterDeviceFormat,
  openclMappedFromVaapi,
}: {
  generatePadName: () => string;
  inputPad: string;
  outputPad: string;
  inputFormat: HardwarePixelFormat;
  outputFormat: HardwarePixelFormat;
  filterDeviceFormat: HardwarePixelFormat;
  openclMappedFromVaapi: boolean;
}) =>
  connectFilters(
    getConversionFilters({
      inputFormat,
      outputFormat,
      filterDeviceFormat,
      openclMappedFromVaapi,
    }),
    inputPad,
    outputPad,
    generatePadName
  );
