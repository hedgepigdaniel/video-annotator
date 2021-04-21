#!/usr/bin/env bash

if [ $# != 4 ]; then
	echo "Usage: $0 <opencl filter> <input> <output>"
	exit 1
fi

FILTER=$1
GPU=$2
INPUT=$3
OUTPUT=$4

FFMPEG=~/code/oss/FFmpeg/ffmpeg


if [ $GPU = "intel" ]; then
	INPUT_FLAGS=(
		-init_hw_device
		vaapi=intel_vaapi:/dev/dri/renderD128
		-hwaccel
		vaapi
		-hwaccel_device
		intel_vaapi
		-hwaccel_output_format
		vaapi
		-init_hw_device
		opencl=intel_opencl@intel_vaapi
		-filter_hw_device
		intel_opencl
	)
	OUTPUT_FLAGS=(
		-vf
		"hwmap,$FILTER,hwmap=derive_device=vaapi:reverse=1"
		-c:v h264_vaapi
	)
elif [ $GPU = "nvidia" ]; then
	INPUT_FLAGS=(
		-init_hw_device
		opencl=nvidia_opencl:1.0
		-filter_hw_device
		nvidia_opencl
		-hwaccel
		nvdec
	)
	OUTPUT_FLAGS=(
		-vf
		"hwupload,$FILTER,hwdownload,format=nv12"
		-c:v
		h264_nvenc
		-qp
		19
		"$OUTPUT"
	)
fi

$FFMPEG "${INPUT_FLAGS[@]}" -i "$INPUT" "${OUTPUT_FLAGS[@]}" -y "$OUTPUT"

