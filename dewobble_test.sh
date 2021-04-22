#!/usr/bin/env bash

set -x

if [ $# -lt 4 ]; then
	echo "Usage: $0 <opencl filter> <intel|amd|nvidia> <input> <output>"
	exit 1
fi

FILTER=$1
GPU=$2
INPUT=$3
OUTPUT=$4
ARGV=("$@")
FLAGS=("${@:5}")

FFMPEG=./ffmpeg


if [ $GPU = "intel" ]; then
	INPUT_FLAGS=(
		-init_hw_device vaapi=intel_vaapi:,driver=iHD,kernel_driver=i915
		-hwaccel vaapi -hwaccel_device intel_vaapi -hwaccel_output_format vaapi
	)
	OUTPUT_FLAGS=(
		-vf "hwmap=derive_device=opencl,$FILTER,hwmap=reverse=1"
		-c:v h264_vaapi
	)
elif [ $GPU = "nvidia" ]; then
	INPUT_FLAGS=(
		-init_hw_device opencl=nvidia_opencl:1.0 -filter_hw_device nvidia_opencl
		-hwaccel nvdec
	)
	OUTPUT_FLAGS=(
		-vf "hwupload,$FILTER,hwdownload,format=nv12"
		-c:v h264_nvenc -qp 19
	)
elif [ $GPU = "amd" ]; then
	VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/amd_pro_x86_64.icd
	INPUT_FLAGS=(
		-init_hw_device opencl=amd_opencl:2.0 -filter_hw_device amd_opencl
		-init_hw_device vaapi=amd_vaapi:,driver=radeonsi,kernel_driver=amdgpu
		-hwaccel vaapi -hwaccel_device amd_vaapi
	)
	OUTPUT_FLAGS=(
		-vf "hwupload,$FILTER,hwdownload,format=nv12"
		-c:v h264_amf -qp_i 21 -qp_b 21 -qp_p 21 -quality quality
	)
else
	echo Unsupported GPU: $GPU
fi

"$FFMPEG" "${INPUT_FLAGS[@]}" -i "$INPUT" "${OUTPUT_FLAGS[@]}" -y "${FLAGS[@]}" "$OUTPUT"

