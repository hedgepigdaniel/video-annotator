import { AudioVideoFilter, FilterSpecification } from "fluent-ffmpeg";

export const connectFilters = (
  filters: AudioVideoFilter[],
  inputPads: string | string[],
  outputPads: string | string[],
  generatePadName: () => string
): FilterSpecification[] =>
  filters.length === 0
    ? [{ inputs: inputPads, filter: "null", options: {}, outputs: outputPads }]
    : filters.reduce(
        (specs: FilterSpecification[], { filter, options }, index) =>
          specs.concat([
            {
              inputs: index === 0 ? inputPads : specs[index - 1].outputs,
              filter,
              options,
              outputs:
                index === filters.length - 1 ? outputPads : [generatePadName()],
            },
          ]),
        []
      );
