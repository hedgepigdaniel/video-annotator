#include "Warper.hpp"

#include <iostream>

#include <opencv2/imgproc.hpp>
#include <opencv2/calib3d.hpp>
#include <opencv2/video/tracking.hpp>
#include <opencv2/highgui.hpp>

#define PI 3.14159265

using namespace std;
using namespace cv;

Warper::Warper(int width, int height) {
    // Known field of view of the camera
    // int v_fov_s = 94.4 * PI / 180;
    // int h_fov_s = 122.6 * PI / 180;
    int d_fov = 149.2 * PI / 180;

    // Center coordinates
    float center_x = width / 2;
    float center_y = height / 2;

    // // Maximum input horizontal/vertical radius
    // double max_radius_x_s = tan(v_fov_s / 2.f);
    // double max_radius_y_s = tan(h_fov_s / 2.f);

    // Maximum input and output diagonal radius
    double max_radius_d = tan(d_fov / 2.f);
    double max_radius_d_pixels = sqrt(center_x * center_x + center_y * center_y);

    Mat map_x(height, width, CV_32FC1);
    Mat map_y(height, width, CV_32FC1);
    for(int y = 0; y < map_x.rows; y++) {
        for(int x = 0; x < map_x.cols; x++ ) {
            // We start from the rectilinear (output) coordinates
            int rel_x_r = x - center_x;
            int rel_y_r = y - center_y;
            float radius_r = sqrt(rel_x_r * rel_x_r + rel_y_r * rel_y_r) * max_radius_d / max_radius_d_pixels;

            // We find the real angle of the light source from the center
            float theta = atan(radius_r * max_radius_d);

            // Convert theta to the appropriate radius for stereographic projection
            float radius_s = 2 * tan(theta / 2);

            float scale = radius_s / radius_r;
            map_x.at<float>(y, x) = center_x + rel_x_r * scale;
            map_y.at<float>(y, x) = center_y + rel_y_r * scale;
        }
    }
    UMat map_1, map_2;
    convertMaps(map_x, map_y, map_1, map_2, CV_16SC2);
    this->map_x = map_1;
    this->map_y = map_2;
}

int Warper::write_frame(UMat frame_input) {
    UMat frame_bgr, frame_undistorted, frame_gray;
    UMat last_frame_gray = this->last_frame_gray;
    vector <Point2f> last_frame_corners = this->last_frame_corners;

    // Undistort frame, convert to grayscale
    cvtColor(frame_input, frame_bgr, COLOR_YUV2BGR_NV12);
    remap(
        frame_bgr,
        frame_undistorted,
        this->map_x,
        this->map_y,
        INTER_LINEAR
    );
    cvtColor(frame_undistorted, frame_gray, COLOR_BGR2GRAY);
    // resize(frame_gray, frame_gray, Size(1280, 720));

    // Find corners to track in current frame
    vector <Point2f> corners;
    goodFeaturesToTrack(frame_gray, corners, 200, 0.01, 30);
    std::cerr << "Found " << corners.size() << " corners\n";

    // // Display corners
    // UMat frame_display = frame_gray.clone();
    // for (size_t i = 0; i < corners.size(); i++) {
    //     drawMarker(frame_display, corners[i], Scalar(0, 0, 255), MARKER_TRIANGLE_UP);
    // }
    // imshow("fast", frame_display);
    // waitKey(20);

    // Keep the frame and the corners found in it for next time
    this->last_frame_gray = frame_gray;
    this->last_frame_corners = corners;
    if (last_frame_gray.empty()) {
        return 0;
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

    Mat homography = findHomography(last_frame_corners_filtered, corners_filtered, RANSAC);

    std::cerr << "Homography: \n" << homography << "\n\n";

    // decompose homography
    double dx = homography.at<double>(0,2);
    double dy = homography.at<double>(1,2);
    double da = atan2(homography.at<double>(1,0), homography.at<double>(0,0));

    fprintf(stderr, "homography: (%+.1f, %+.1f) %+.1f degrees\n", dx, dy, da * 180 / PI);

    this->transforms.push_back(homography.inv());

    UMat stable = frame_gray.clone();
    UMat temp;
    Mat transform = this->transforms[this->transforms.size() - 1].clone();
    for (size_t i = this->transforms.size() - 2; i < this->transforms.size(); i--) {
        transform = transform * this->transforms[i];
    }
    warpPerspective(stable, temp, transform, frame_gray.size());
    stable = temp;

    imshow("fast", stable);
    waitKey(20);
    return 0;
}