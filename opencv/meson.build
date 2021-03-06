project(
	'action-video-processor',
    'c',
	'cpp',
	version: '0.1',
	license: 'GPL',
	meson_version: '>=0.47.0',
	default_options: [
		'cpp_std=c++14',
        'c_std=c11',
		'werror=true',
	],
)

include_directories(['.'])

display_image_sources = [
    'DisplayImage.cpp',
    'hw_init.cpp',
    'FrameSourceWarp.cpp',
    'AvFrameSourceProfile.cpp',
    'AvFrameSourceFileVaapi.cpp',
    'AvFrameSourceMapOpenCl.cpp',
    'FrameSourceProfile.cpp',
    'FrameSourceFfmpegOpenCl.cpp',
    'utils.cpp',
    'Profiler.cpp',
]

libavformat = dependency('libavformat')
libavcodec = dependency('libavcodec')
libavutil = dependency('libavutil')
opencv = dependency('opencv4', version:'>= 4.5')
opencl = dependency('OpenCL')
libva_drm = dependency('libva-drm')
gpmf_parser = dependency('gpmf-parser')
gram_savitzky_golay = dependency('gram_savitzky_golay')


dependencies = [
    libavformat,
    libavcodec,
    libavutil,
    opencv,
    opencl,
    libva_drm,
    gpmf_parser,
    gram_savitzky_golay,
]

executable(
    'DisplayImage',
    display_image_sources,
    dependencies: dependencies,
    install: true,
)

calibrate_camera_sources = ['camera_calibration/camera_calibration.cpp']

executable(
    'camera_calibration',
    calibrate_camera_sources,
    dependencies: dependencies,
    install: true,
)


kalman_sources = ['kalman/kalman.cpp']

executable(
    'kalman',
    kalman_sources,
    dependencies: dependencies,
    install: true,
)
