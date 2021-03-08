#ifndef _FRAME_SOURCE_WARP_HPP_
#define _FRAME_SOURCE_WARP_HPP_

#include <deque>
#include <opencv2/core.hpp>

#include "FrameSource.hpp"

using namespace std;
using namespace cv;

/**
 * FrameSourceWarp is a video processor that accepts a stream of input video frames
 * and metadata and applies reprojection and stabilisation on them
 */
class FrameSourceWarp: public FrameSource {
    FrameSource *source;
    UMat last_frame_gray;
    vector <Point2f> last_frame_corners;
    vector<Mat> frame_movements;
    Mat camera_matrix;
    Mat distortion_coefficients;
    Mat camera_map_1;
    Mat camera_map_2;
    Matx33d output_camera_matrix;
    Size output_size;

    cv::UMat warp_frame(UMat input_frame);
  public:
    FrameSourceWarp(FrameSource *source);
    cv::UMat pull_frame();
    cv::UMat peek_frame();
};

#endif // _FRAME_SOURCE_WARP_HPP_
