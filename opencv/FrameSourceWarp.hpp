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
    UMat map_x;
    UMat map_y;
    UMat last_frame_gray;
    vector <Point2f> last_frame_corners;
    vector<Mat> transforms;
    cv::UMat warp_frame(UMat input_frame);

    Point2d center;
    double max_radius_d;
    double max_radius_d_pixels;
    Point2d mapPointToSource(int x, int y);
    Point2d mapPointFromSource(float x, float y);
  public:
    FrameSourceWarp(FrameSource *source, int d_fov);
    cv::UMat pull_frame();
    cv::UMat peek_frame();
};

#endif // _FRAME_SOURCE_WARP_HPP_
