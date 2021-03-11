#ifndef _FRAME_SOURCE_WARP_HPP_
#define _FRAME_SOURCE_WARP_HPP_

#include <deque>
#include <memory>
#include <opencv2/core.hpp>
#include <opencv2/video/tracking.hpp>

#include "FrameSource.hpp"
#include "rotation.hpp"

enum CameraPreset {
    GOPRO_H4B_WIDE43_PUBLISHED,
    GOPRO_H4B_WIDE43_MEASURED,
    GOPRO_H4B_WIDE43_MEASURED_STABILISATION,
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
    cv::Vec3f m_measured_rotation;
    cv::KalmanFilter m_x_filter;
    cv::KalmanFilter m_y_filter;
    cv::KalmanFilter m_z_filter;

    std::vector<cv::Point2f> m_last_input_frame_corners;

    // The last input frame for which corners were detected from scratch
    long m_last_key_frame_index = -1;

    cv::UMat warp_frame(cv::UMat input_frame);
    cv::UMat change_projection(cv::UMat input);
    cv::Mat guess_camera_rotation(std::vector<cv::Point2f> points_prev, std::vector<cv::Point2f> points_current);
  public:
    FrameSourceWarp(std::shared_ptr<FrameSource> source, CameraPreset input_camera);
    cv::UMat pull_frame();
    cv::UMat peek_frame();
};

#endif // _FRAME_SOURCE_WARP_HPP_
