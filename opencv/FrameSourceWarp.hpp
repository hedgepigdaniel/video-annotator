#ifndef _FRAME_SOURCE_WARP_HPP_
#define _FRAME_SOURCE_WARP_HPP_

#include <deque>
#include <memory>
#include <queue>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
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
    cv::Mat m_camera_map_1;
    cv::Mat m_camera_map_2;

    // Properties of the output camera
    Camera m_output_camera;

    long m_frame_index = 0;

    // The last input frame
    cv::UMat m_last_input_frame;
    cv::Mat m_measured_rotation;
    unsigned int m_smooth_radius;
    gram_sg::RotationFilter m_rotation_filter;
    std::queue<cv::UMat> m_buffered_frames;
    std::queue<cv::Mat> m_buffered_rotations;

    std::vector<cv::Point2f> m_last_input_frame_corners;

    // The last input frame for which corners were detected from scratch
    long m_last_key_frame_index = -1;

    void warp_frame(cv::UMat input_frame);
    cv::UMat change_projection(cv::UMat input);
    cv::Mat guess_camera_rotation(std::vector<cv::Point2f> points_prev, std::vector<cv::Point2f> points_current);
  public:
    FrameSourceWarp(
      std::shared_ptr<FrameSource> source,
      CameraPreset input_camera,
      double scale = 1,
      bool crop_borders = false,
      double zoom = 1,
      int smooth_radius = 30
    );
    cv::UMat pull_frame();
    cv::UMat peek_frame();
};

#endif // _FRAME_SOURCE_WARP_HPP_
