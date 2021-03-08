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

FrameSourceWarp::FrameSourceWarp(FrameSource *source) {
    this->source = source;

    UMat first_frame = this->source->peek_frame();
    Size size = Size(first_frame.cols, first_frame.rows * 2 / 3);

    Mat camera_matrix = Mat::eye(3, 3, CV_64F);
    this->camera_matrix = camera_matrix;

    // Set principal point in the centre
    camera_matrix.at<double>(0, 2) = (size.width - 1.) / 2;
    camera_matrix.at<double>(1, 2) = (size.height - 1.) / 2;

    // Set zero distortion coefficients
    this->distortion_coefficients = Mat::zeros(4, 1, CV_64F);

    // Set field of view
    int mode = 1;
    if (mode == 0) {
        // Use published values from
        // https://community.gopro.com/t5/en/HERO4-Field-of-View-FOV-Information/ta-p/390285
        const int GOPRO_H4B_FOV_H_NOSTAB = 122.6;
        const int GOPRO_H4B_FOV_V_NOSTAB = 94.4;

        camera_matrix.at<double>(0, 0) = size.width /
            (2 * atan ((GOPRO_H4B_FOV_H_NOSTAB / 2) * CV_PI / 180)); // 1171.95
        camera_matrix.at<double>(1, 1) = size.height /
            (2 * atan ((GOPRO_H4B_FOV_V_NOSTAB / 2) * CV_PI / 180)); // 1044.87
    } else if (mode == 1) {
        // Measured values for GoPro Hero 4 Black with 4:3 "Wide" FOV setting and stabilisation disabled
        camera_matrix.at<double>(0, 2) = 967.37;
        camera_matrix.at<double>(1, 2) = 711.07;
        camera_matrix.at<double>(0, 0) = 942.96;
        camera_matrix.at<double>(1, 1) = 942.53;
    } else if (mode == 2) {
        // Measured values for GoPro Hero 4 Black with 4:3 "Wide" FOV setting and stabilisation enabled
        camera_matrix.at<double>(0, 2) = 965.90;
        camera_matrix.at<double>(1, 2) = 712.94;
        camera_matrix.at<double>(0, 0) = 1045.58;
        camera_matrix.at<double>(1, 1) = 1045.64;
    }

    vector<Point2d> extreme_points;
    fisheye::undistortPoints(
        vector<Point2d>({
            // corners
            Point2d(0, 0),
            Point2d(0, size.height),
            Point2d(size.width, 0),
            Point2d(size.width, size.height),

            // midpoint of edges
            Point2d(size.width / 2, 0),
            Point2d(size.width, size.height / 2),
            Point2d(size.width / 2, size.height),
            Point2d(0, size.height / 2),
        }),
        extreme_points,
        this->camera_matrix,
        this->distortion_coefficients
    );
    auto compare_x = [](const Point2d &point1, const Point2d &point2) { return point1.x < point2.x; };
    auto compare_y = [](const Point2d &point1, const Point2d &point2) { return point1.y < point2.y; };
    double max_x = max_element( begin(extreme_points), end(extreme_points), compare_x)->x;
    double min_x = min_element( begin(extreme_points), end(extreme_points), compare_x)->x;
    double max_y = max_element( begin(extreme_points), end(extreme_points), compare_y)->y;
    double min_y = min_element( begin(extreme_points), end(extreme_points), compare_y)->y;

    double scale = 200;

    this->output_size = Size(scale * (max_x - min_x), scale * (max_y - min_y));
    this->output_camera_matrix = Matx33d::eye();
    this->output_camera_matrix(0, 0) = scale;
    this->output_camera_matrix(1, 1) = scale;
    this->output_camera_matrix(0, 2) = scale * (max_x - min_x) / 2;
    this->output_camera_matrix(1, 2) = scale * (max_y - min_y) / 2;

    fisheye::initUndistortRectifyMap(
        camera_matrix,
        this->distortion_coefficients,
        Mat::eye(3, 3, CV_64F),
        this->output_camera_matrix,
        this->output_size,
        CV_16SC2,
        this->camera_map_1,
        this->camera_map_2
    );
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
            last_frame_corners_filtered.push_back(last_frame_corners[i]);
            corners_filtered.push_back(corners[i]);
        }
    }

    fisheye::undistortPoints(
        last_frame_corners_filtered,
        last_frame_corners_filtered,
        this->camera_matrix,
        this->distortion_coefficients,
        Matx33d::eye(),
        this->output_camera_matrix
    );
    fisheye::undistortPoints(
        corners_filtered,
        corners_filtered,
        this->camera_matrix,
        this->distortion_coefficients,
        Matx33d::eye(),
        this->output_camera_matrix
    );

    // Find camera movement since last frame
    Mat frame_movement = findHomography(last_frame_corners_filtered, corners_filtered, RANSAC);
    // std::cerr << "frame_movement: \n" << frame_movement << "\n\n";

    // decompose homography
    double dx = frame_movement.at<double>(0,2);
    double dy = frame_movement.at<double>(1,2);
    double da = atan2(frame_movement.at<double>(1,0), frame_movement.at<double>(0,0));
    fprintf(stderr, "frame_movement: (%+.1f, %+.1f) %+.1f degrees\n", dx, dy, da * 180 / M_PI);
    this->frame_movements.push_back(frame_movement.inv());

    // Find the accumulated camera movement since the beginning
    Mat accumulated_movement = this->frame_movements[this->frame_movements.size() - 1].clone();
    for (size_t i = this->frame_movements.size() - 2; i < this->frame_movements.size(); i--) {
        accumulated_movement = accumulated_movement * this->frame_movements[i];
    }
    dx = frame_movement.at<double>(0,2);
    dy = frame_movement.at<double>(1,2);
    da = atan2(frame_movement.at<double>(1,0), frame_movement.at<double>(0,0));
    fprintf(stderr, "accumulated_movement: (%+.1f, %+.1f) %+.1f degrees\n", dx, dy, da * 180 / M_PI);

    // convert to BGR
    UMat frame_bgr;
    cvtColor(input_frame, frame_bgr, COLOR_YUV2BGR_NV12);

    // Change projection
    UMat frame_rectilinear;
    remap(frame_bgr, frame_rectilinear, this->camera_map_1, this->camera_map_2, INTERPOLATION);

    // Apply motion stabilisation
    UMat frame_output;
    cerr << "warping with " << accumulated_movement << "\n";
    warpPerspective(frame_rectilinear, frame_output, accumulated_movement, frame_rectilinear.size(), INTERPOLATION);

    return frame_output;
}

UMat FrameSourceWarp::pull_frame() {
    return this->warp_frame(this->source->pull_frame());
}

UMat FrameSourceWarp::peek_frame() {
    return this->warp_frame(this->source->peek_frame());
}
