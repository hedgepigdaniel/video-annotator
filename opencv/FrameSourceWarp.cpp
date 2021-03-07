#include "FrameSourceWarp.hpp"

#include <iostream>
#include <math.h>

#include <opencv2/imgproc.hpp>
#include <opencv2/calib3d.hpp>
#include <opencv2/video/tracking.hpp>
#include <opencv2/highgui.hpp>

using namespace std;
using namespace cv;

const int INTERPOLATION = INTER_LINEAR;

Point2d FrameSourceWarp::mapPointToSource(int x, int y) {
    // We start from the rectilinear (output) coordinates
    int rel_x_r = x - this->center.x;
    int rel_y_r = y - this->center.y;
    float radius_r = sqrt(rel_x_r * rel_x_r + rel_y_r * rel_y_r) * this->max_radius_d / this->max_radius_d_pixels;

    // We find the real angle of the light source from the center
    float theta = atan(radius_r * this->max_radius_d);

    // Convert theta to the appropriate radius for stereographic projection
    float radius_s = 2 * tan(theta / 2);

    float scale = radius_s / radius_r;
    return Point2d(this->center.x + rel_x_r * scale, this->center.y + rel_y_r * scale);
}

Point2d FrameSourceWarp::mapPointFromSource(float x, float y) {
    float rel_x_r = x - this->center.x;
    float rel_y_r = y - this->center.y;
    float radius_s = sqrt(rel_x_r * rel_x_r + rel_y_r * rel_y_r) * this->max_radius_d / this->max_radius_d_pixels;

    float theta = 2 * atan(radius_s / 2);

    float radius_r = tan(theta) / this->max_radius_d;

    float scale = radius_r / radius_s;
    return Point2d(this->center.x + rel_x_r * scale, this->center.y + rel_y_r * scale);
}

FrameSourceWarp::FrameSourceWarp(FrameSource *source, int d_fov) {
    this->source = source;

    UMat first_frame = source->peek_frame();
    int width = first_frame.cols;
    int height = first_frame.rows * 2 / 3;

    // Center coordinates
    this->center = Point(width / 2, height / 2);

    // Maximum input and output diagonal radius
    this->max_radius_d = tan(d_fov / 2.f);
    this->max_radius_d_pixels = sqrt(center.x * center.x + center.y * center.y);

    Mat map_x(height, width, CV_32FC1);
    Mat map_y(height, width, CV_32FC1);
    for(int y = 0; y < map_x.rows; y++) {
        for(int x = 0; x < map_x.cols; x++ ) {
            Point2d mapped = mapPointToSource(x, y);
            map_x.at<float>(y, x) = mapped.x;
            map_y.at<float>(y, x) = mapped.y;
        }
    }
    UMat map_1, map_2;
    convertMaps(map_x, map_y, map_1, map_2, CV_16SC2);
    this->map_x = map_1;
    this->map_y = map_2;
}

UMat FrameSourceWarp::warp_frame(UMat input_frame) {
    // Create a UMat for only the luminance plane
    UMat frame_gray(input_frame, Rect(0, 0, input_frame.cols, input_frame.rows * 2 / 3));

    // Find corners to track in current frame
    vector <Point2f> corners;
    goodFeaturesToTrack(frame_gray, corners, 200, 0.01, 30);
    std::cerr << "Found " << corners.size() << " corners\n";

    // // Display corners
    // UMat frame_display = frame_gray.clone();
    // for (size_t i = 0; i < corners.size(); i++) {
    //     drawMarker(frame_display, corners[i], Scalar(0, 0, 255), MARKER_TRIANGLE_UP);
    // }
    // return frame_display;

    // Keep the frame and the corners found in it for next time
    UMat last_frame_gray = this->last_frame_gray;
    vector <Point2f> last_frame_corners = this->last_frame_corners;
    this->last_frame_gray = frame_gray;
    this->last_frame_corners = corners;
    if (last_frame_gray.empty()) {
        return frame_gray;
    }

    // If this is not the first frame, calculate optical flow
    vector <Point2f> corners_filtered, last_frame_corners_filtered;
    vector <uchar> status;
    vector <float> err;

    calcOpticalFlowPyrLK(
        last_frame_gray,
        frame_gray,
        last_frame_corners,
        corners,
        status,
        err
    );
    for (size_t i=0; i < status.size(); i++) {
        if (status[i]) {
            last_frame_corners_filtered.push_back(
                this->mapPointFromSource(last_frame_corners[i].x, last_frame_corners[i].y)
            );
            corners_filtered.push_back(
                this->mapPointFromSource(corners[i].x, corners[i].y)
            );
        }
    }

    Mat homography = findHomography(last_frame_corners_filtered, corners_filtered, RANSAC);

    std::cerr << "Homography: \n" << homography << "\n\n";

    // decompose homography
    double dx = homography.at<double>(0,2);
    double dy = homography.at<double>(1,2);
    double da = atan2(homography.at<double>(1,0), homography.at<double>(0,0));

    fprintf(stderr, "homography: (%+.1f, %+.1f) %+.1f degrees\n", dx, dy, da * 180 / M_PI);

    this->transforms.push_back(homography.inv());

    // convert to BGR
    UMat frame_bgr;
    cvtColor(input_frame, frame_bgr, COLOR_YUV2BGR_NV12);

    // Convert the image into rectilinear projection
    UMat frame_rectilinear;
    remap(
        frame_bgr,
        frame_rectilinear,
        this->map_x,
        this->map_y,
        INTERPOLATION
    );

    UMat stable = frame_rectilinear.clone();
    UMat temp;
    Mat transform = this->transforms[this->transforms.size() - 1].clone();
    for (size_t i = this->transforms.size() - 2; i < this->transforms.size(); i--) {
        transform = transform * this->transforms[i];
    }
    warpPerspective(stable, temp, transform, frame_rectilinear.size(), INTERPOLATION);
    stable = temp;

    return stable;
}

UMat FrameSourceWarp::pull_frame() {
    return this->warp_frame(this->source->pull_frame());
}

UMat FrameSourceWarp::peek_frame() {
    return this->warp_frame(this->source->peek_frame());
}
