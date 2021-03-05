#include "Warper.hpp"

int Warper::process_frame_mat(UMat frame_input) {
    UMat frame_bgr, frame_undistorted, frame_gray;
    UMat last_frame_gray = frames_ctx->last_frame_gray;
    vector <Point2f> last_frame_corners = frames_ctx->last_frame_corners;

    // Undistort frame, convert to grayscale
    cvtColor(frame_input, frame_bgr, COLOR_YUV2BGR_NV12);
    remap(
        frame_bgr,
        frame_undistorted,
        frames_ctx->map_x,
        frames_ctx->map_y,
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
    frames_ctx->last_frame_gray = frame_gray;
    frames_ctx->last_frame_corners = corners;
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

    frames_ctx->transforms.push_back(homography.inv());

    UMat stable = frame_gray.clone();
    UMat temp;
    Mat transform = frames_ctx->transforms[frames_ctx->transforms.size() - 1].clone();
    for (size_t i = frames_ctx->transforms.size() - 2; i < frames_ctx->transforms.size(); i--) {
        transform = transform * frames_ctx->transforms[i];
    }
    warpPerspective(stable, temp, transform, frame_gray.size());
    stable = temp;

    imshow("fast", stable);
    waitKey(20);
    return 0;
}