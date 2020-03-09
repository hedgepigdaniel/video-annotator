project(
	'action-video-processor',
    'c',
	'cpp',
	version: '0.1',
	license: 'GPL',
	meson_version: '>=0.47.0',
	default_options: [
		'cpp_std=c++11',
        'c_std=c11',
		'werror=true',
	],
)

include_directories(['.'])

display_image_sources = ['DisplayImage.cpp']

libavformat = dependency('libavformat')
opencv = dependency('opencv4')
libva_drm = dependency('libva-drm')

dependencies = [
    libavformat,
    opencv,
    libva_drm,
]

executable(
    'DisplayImage',
    display_image_sources,
    dependencies: dependencies,
    install: true,
)
