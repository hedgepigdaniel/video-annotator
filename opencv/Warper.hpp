#ifndef _WARPER_HPP_
#define _WARPER_HPP_

#include <deque>
#include <opencv2/core.hpp>

using namespace std;
using namespace cv;

class GyroFrame {
    double start_ts;
    double end_ts;
    double roll;
    double pitch;
    double yaw;
};

/**
 * Warper is a video processor that accepts a stream of input video frames
 * and metadata and applies reprojection and stabilisation on them
 */
class Warper {
    UMat map_x;
    UMat map_y;
    UMat last_frame_gray;
    vector <Point2f> last_frame_corners;
    vector<Mat> transforms;
    deque<GyroFrame> gyro_frames;
  public:
    /**
     * Push a video frame in for processing
     */
    int write_frame(UMat frame_input);

    /**
     * Indicate that the input has ended and there will be no more input
     * video frames
     */
    void close_input();

    /**
     * Attempt to read a processed output frame
     * Raises an exception if no frames are ready
     */
    Umat read_frame();
};

#endif // _WARPER_HPP_
