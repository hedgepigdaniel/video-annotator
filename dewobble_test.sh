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
	OUT_P=fish
	OUT_FL=1623
	WIDTH=2704
	HEIGHT=1520
	FIXED=false
	FRAME_RATE=60
	SMOOTH_MULTIPLIER=3

	# Calculated
	OUT_SIZE="out_w=$(( $WIDTH / 2 )):out_h=$(( $HEIGHT / 2 ))"
	OUT_CAMERA="out_p=$OUT_P:out_fl=$(( $OUT_FL )):out_fx=$(( $WIDTH / 4 )):out_fy=$(( $HEIGHT / 4 )):border=replicate"
	SMOOTHING=$(( $FRAME_RATE * $SMOOTH_MULTIPLIER / 2 ))
	if $FIXED; then
		STAB=fixed
		TRIPOD=1
	else
		STAB=sg
		TRIPOD=0
	fi
	OUTPUT_FLAGS=(
		-filter_complex
"[0:v]scale=w=$(( $WIDTH / 2)):h=$(( $HEIGHT / 2 )),format=nv12,split[scaled1][scaled2];"\
"[scaled1]hwupload,split=outputs=3[hw1][hw2][hw3];"\
"[hw1]dewobble_opencl=$IN_CAMERA:$OUT_CAMERA:out_w=$WIDTH:out_h=$HEIGHT:stab=none[base];"\
"[hw2]dewobble_opencl=$IN_CAMERA:$OUT_CAMERA:$OUT_SIZE:stab=$STAB:stab_r=$SMOOTHING[topright];"\
"[scaled2]vidstabtransform=smoothing=$SMOOTHING:optzoom=0:crop=black:tripod=$TRIPOD,format=nv12,hwupload,dewobble_opencl=$IN_CAMERA:$OUT_CAMERA:$OUT_SIZE:stab=none[bottomleft];"\
"[hw3]deshake_opencl=tripod=$TRIPOD:adaptive_crop=0:smooth_window_multiplier=$SMOOTH_MULTIPLIER,dewobble_opencl=$IN_CAMERA:$OUT_CAMERA:$OUT_SIZE:stab=none[bottomright];"\
"[base][topright]overlay_opencl=x=$(( $WIDTH / 2 ))[base2];"\
"[base2][bottomleft]overlay_opencl=y=$(( $HEIGHT / 2 ))[base3];"\
"[base3][bottomright]overlay_opencl=x=$(( $WIDTH / 2 )):y=$(( $HEIGHT / 2 ))[outhw];"\
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
	exit 1
fi

if [ $GPU = "intelcomp" ]; then
	"$FFMPEG" "${INPUT_FLAGS[@]}" -i "$INPUT" -vf "vidstabdetect=tripod=$TRIPOD" -f null -
fi

# gdb --args \
"$FFMPEG" "${INPUT_FLAGS[@]}" -i "$INPUT" "${OUTPUT_FLAGS[@]}" -y "${FLAGS[@]}"

