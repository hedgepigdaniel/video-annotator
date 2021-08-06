#!/usr/bin/env bash

set -x -e

if [ $# -lt 4 ]; then
	>&2 echo "Usage: $0 transcode|decode <intel|amd|nvidia> <opencl filter> <input> [...flags] [output]"
	exit 1
fi

ACTION=$1
GPU=$2
FILTER=$3
INPUT=$4
FLAGS=("${@:5}")

FFMPEG=./ffmpeg


if [ $GPU = "comp" ]; then
	INPUT_FLAGS=(
		-init_hw_device vaapi=intel_vaapi:,driver=iHD,kernel_driver=i915
		#-hwaccel vaapi -hwaccel_device intel_vaapi
		-hwaccel nvdec
		#-init_hw_device opencl=intel_opencl@intel_vaapi -filter_hw_device intel_opencl
		-init_hw_device opencl=nvidia_opencl:1.0 -filter_hw_device nvidia_opencl
	)
	IN_CAMERA="in_p=fish:in_fl=942"
	OUT_P=fish
	OUT_FL=942
	WIDTH=1920
	HEIGHT=1440
	FIXED=false
	FRAME_RATE=60
	SMOOTH_MULTIPLIER=2

	# Calculated
	OUT_SIZE="out_w=$(( $WIDTH / 2 )):out_h=$(( $HEIGHT / 2 ))"
	OUT_CAMERA="out_p=$OUT_P:out_fl=$(( $OUT_FL )):out_fx=$(( $WIDTH / 4 )):out_fy=$(( $HEIGHT / 4 ))"
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
elif [ $GPU = "intel" ]; then
	INPUT_FLAGS=(
		-init_hw_device vaapi=intel_vaapi:,driver=iHD,kernel_driver=i915
		-hwaccel vaapi -hwaccel_device intel_vaapi -hwaccel_output_format vaapi
		-init_hw_device opencl=intel_opencl@intel_vaapi -filter_hw_device intel_opencl
	)
	PREFILTER="hwmap,"
	POSTFILTER=",hwmap=derive_device=vaapi:reverse=1"
	OUTPUT_FLAGS=(
		-c:v h264_vaapi
	)
elif [ $GPU = "nvidia" ]; then
	INPUT_FLAGS=(
		-init_hw_device opencl=nvidia_opencl:1.0 -filter_hw_device nvidia_opencl
		-hwaccel nvdec
	)
	PREFILTER="hwupload,"
	POSTFILTER=",hwdownload,format=nv12"
	OUTPUT_FLAGS=(
		-c:v h264_nvenc -qp 19
	)
elif [ $GPU = "amd" ]; then
	VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/amd_pro_x86_64.icd
	INPUT_FLAGS=(
		-init_hw_device opencl=amd_opencl:2.0 -filter_hw_device amd_opencl
		-init_hw_device vaapi=amd_vaapi:,driver=radeonsi,kernel_driver=amdgpu
		-hwaccel vaapi -hwaccel_device amd_vaapi
	)
	PREFILTER="hwupload,"
	POSTFILTER=",hwdownload,format=nv12"
	OUTPUT_FLAGS=(
		-c:v h264_amf -qp_i 21 -qp_b 21 -qp_p 21 -quality quality
	)
else
	>&2 echo Unsupported GPU: $GPU
	exit 1
fi

if [ $GPU = "comp" ]; then
	"$FFMPEG" "${INPUT_FLAGS[@]}" -hwaccel vaapi -i "$INPUT" -vf "scale=w=$(( $WIDTH / 2)):h=$(( $HEIGHT / 2 )),vidstabdetect=tripod=$TRIPOD" -f null -
fi

if [ "$ACTION" = "decode" ]; then
	echo "$FFMPEG" "${INPUT_FLAGS[@]}" -i "$INPUT" \
		-vf "$PREFILTER$FILTER" -f null - "${FLAGS[@]}"
elif [ "$ACTION" = "transcode" ]; then
	echo "$FFMPEG" "${INPUT_FLAGS[@]}" -i "$INPUT" \
		-vf "$PREFILTER$FILTER$POSTFILTER" "${OUTPUT_FLAGS[@]}" "${FLAGS[@]}"
fi

# gdb --args \
# perf record --call-graph dwarf \
# perf timechart record -g \
# valgrind --tool=callgrind --separate-threads=yes \
# LD_PRELOAD=/usr/lib/libprofiler.so CPUPROFILE=test.prof CPUPROFILE_REALTIME=1 \

