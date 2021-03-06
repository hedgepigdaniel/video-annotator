#ifndef _FRAME_SOURCE_WARP_HPP_
#define _FRAME_SOURCE_WARP_HPP_

#include <deque>
#include <memory>
#include <queue>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/core/ocl.hpp>
#include <gram_savitzky_golay/spatial_filters.h>

#include "FrameSource.hpp"

enum CameraPreset {
    GOPRO_H4B_WIDE43_PUBLISHED,
    GOPRO_H4B_WIDE43_MEASURED,
    GOPRO_H4B_WIDE43_MEASURED_STABILISATION,
    GOPRO_H4B_WIDE169_PUBLISHED,
    GOPRO_H4B_WIDE169_MEASURED,
    GOPRO_H4B_WIDE169_MEASURED_STABILISATION
};

enum CameraModel {
  RECTILINEAR,
  FISHEYE
};

class Camera {
  public:
    CameraModel model;
    cv::Matx33d matrix;
    cv::Mat distortion_coefficients;
    cv::Size size;
};

/**
 * FrameSourceWarp is a video processor that accepts a stream of input video frames
 * and metadata and applies reprojection and stabilisation on them
 */
class FrameSourceWarp: public FrameSource {
    std::shared_ptr<FrameSource> m_source;

    // Properties of the input camera
    Camera m_input_camera;

    // Optimized pixel mapping table from output camera to input camera
    cv::UMat m_map_x;
    cv::UMat m_map_y;
    cv::ocl::Kernel m_remap_kernel;

    // Properties of the output camera
    Camera m_output_camera;

    // Current frame index
    long m_frame_index = 0;

    // The last input frame
    cv::UMat m_last_input_frame;
    cv::Mat m_measured_rotation;
    std::vector<cv::Point2f> m_last_input_frame_corners;

    // Settings
    unsigned int m_smooth_radius;
    cv::InterpolationFlags m_interpolation;

    // Stabilization lookahead buffer
    gram_sg::RotationFilter m_rotation_filter;
    std::queue<cv::UMat> m_buffered_frames;
    std::queue<cv::Mat> m_buffered_rotations;
    cv::Mat m_last_frame_rotation;

    // The last input frame for which corners were detected from scratch
    long m_last_key_frame_index = -1;

    void consume_frame(cv::UMat input_frame);
    cv::UMat warp_frame(cv::UMat input, cv::Mat rotation);
    int guess_camera_rotation(
      std::vector<cv::Point2f> points_prev,
      std::vector<cv::Point2f> points_current,
      cv::OutputArray rotation
    );
  public:
    FrameSourceWarp(
      std::shared_ptr<FrameSource> source,
      CameraPreset input_camera,
      double scale = 1,
      bool crop_borders = false,
      double zoom = 1,
      int smooth_radius = 30,
      cv::InterpolationFlags interpolation = cv::INTER_LINEAR
    );
    cv::UMat pull_frame();
    cv::UMat peek_frame();
};

#endif // _FRAME_SOURCE_WARP_HPP_
