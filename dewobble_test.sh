#!/usr/bin/env bash

set -x -e

if [ $# -lt 3 ]; then
	echo "Usage: $0 <opencl filter> <intel|amd|nvidia> <input> <output>"
	exit 1
fi

FILTER=$1
GPU=$2
INPUT=$3
FLAGS=("${@:4}")

FFMPEG=./ffmpeg


if [ $GPU = "intel" ]; then
	INPUT_FLAGS=(
		-init_hw_device vaapi=intel_vaapi:,driver=iHD,kernel_driver=i915
		-hwaccel vaapi -hwaccel_device intel_vaapi -hwaccel_output_format vaapi
		-init_hw_device opencl=intel_opencl@intel_vaapi -filter_hw_device intel_opencl
	)
	OUTPUT_FLAGS=(
		-vf "hwmap,$FILTER,hwmap=derive_device=vaapi:reverse=1"
		-c:v h264_vaapi
	)
elif [ $GPU = "intelcomp" ]; then
	INPUT_FLAGS=(
		-init_hw_device vaapi=intel_vaapi:,driver=iHD,kernel_driver=i915
		#-hwaccel vaapi -hwaccel_device intel_vaapi
		-hwaccel nvdec
		#-init_hw_device opencl=intel_opencl@intel_vaapi -filter_hw_device intel_opencl
		-init_hw_device opencl=nvidia_opencl:1.0 -filter_hw_device nvidia_opencl
	)
	IN_CAMERA="in_p=fish:in_fl=1623"
	OUT_CAMERA="out_p=rect:out_fl=1100"
	HEIGHT=1520
	BAND=160
	FIXED=false
	SMOOTHING=90
	if $FIXED; then
		STAB=fixed
		TRIPOD=1
	else
		STAB=sg
		TRIPOD=0
	fi
	OUTPUT_FLAGS=(
		-filter_complex
"[0:v]hwupload,split=outputs=3[hw1][hw2][hw3];"\
"[hw2]dewobble_opencl=$IN_CAMERA:$OUT_CAMERA:out_h=$BAND:stab=none[middle];"\
"[hw3]dewobble_opencl=$IN_CAMERA:out_h=$(($HEIGHT / 2 - $BAND / 2)):$OUT_CAMERA:out_fy=$(( $HEIGHT / 2 )):stab=$STAB:stab_r=$SMOOTHING[top];"\
"[0:v]vidstabtransform=smoothing=$SMOOTHING:optzoom=0:crop=black:tripod=$TRIPOD,format=nv12,hwupload,dewobble_opencl=$IN_CAMERA:out_h=$(( $HEIGHT / 2 - $BAND / 2)):$OUT_CAMERA:out_fy=$(( -$BAND / 2)):stab=none[bottom];"\
"[hw1][middle]overlay_opencl=y=$(( $HEIGHT / 2 - $BAND / 2 ))[base1];"\
"[base1][top]overlay_opencl[base2];"\
"[base2][bottom]overlay_opencl=y=$(( $HEIGHT / 2 + $BAND / 2 ))[outhw];"\
"[outhw]hwdownload,format=nv12[out]"
		-map "[out]"
		-map "0:a"
		-c:v h264_nvenc -qp 19
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

if [ $GPU = "intelcomp" ]; then
	echo #$FFMPEG" "${INPUT_FLAGS[@]}" -i "$INPUT" -vf "vidstabdetect=tripod=$TRIPOD" -f null -
fi

# gdb --args \
"$FFMPEG" "${INPUT_FLAGS[@]}" -i "$INPUT" "${OUTPUT_FLAGS[@]}" -y "${FLAGS[@]}"

