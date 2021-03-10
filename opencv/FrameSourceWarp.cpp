#include "FrameSourceWarp.hpp"

#include <iostream>
#include <math.h>
#include <cstdlib>

#include <opencv2/imgproc.hpp>
#include <opencv2/calib3d.hpp>
#include <opencv2/video/tracking.hpp>
#include <opencv2/highgui.hpp>

using namespace std;
using namespace cv;

const int INTERPOLATION = INTER_LINEAR;

// Published values from
// https://community.gopro.com/t5/en/HERO4-Field-of-View-FOV-Information/ta-p/390285
const int GOPRO_H4B_FOV_H_NOSTAB = 122.6;
const int GOPRO_H4B_FOV_V_NOSTAB = 94.4;

// Checks if a matrix is a valid rotation matrix.
bool isRotationMatrix(Mat R)
{
    Mat Rt;
    transpose(R, Rt);
    Mat shouldBeIdentity = Rt * R;
    Mat I = Mat::eye(3,3, shouldBeIdentity.type());
    
    return  norm(I, shouldBeIdentity) < 1e-6;
    
}

// Calculates rotation matrix to euler angles
// The result is the same as MATLAB except the order
// of the euler angles ( x and z are swapped ).
Vec3f rotationMatrixToEulerAngles(Mat R)
{

    assert(isRotationMatrix(R));
    
    float sy = sqrt(R.at<double>(0,0) * R.at<double>(0,0) +  R.at<double>(1,0) * R.at<double>(1,0) );

    bool singular = sy < 1e-6; // If

    float x, y, z;
    if (!singular)
    {
        x = atan2(R.at<double>(2,1) , R.at<double>(2,2));
        y = atan2(-R.at<double>(2,0), sy);
        z = atan2(R.at<double>(1,0), R.at<double>(0,0));
    }
    else
    {
        x = atan2(-R.at<double>(1,2), R.at<double>(1,1));
        y = atan2(-R.at<double>(2,0), sy);
        z = 0;
    }
    return Vec3f(x, y, z);
    
    
    
}


// Calculates rotation matrix given euler angles.
Mat eulerAnglesToRotationMatrix(Vec3f theta)
{
    // Calculate rotation about x axis
    Mat R_x = (Mat_<double>(3,3) <<
               1,       0,              0,
               0,       cos(theta[0]),   -sin(theta[0]),
               0,       sin(theta[0]),   cos(theta[0])
               );
    
    // Calculate rotation about y axis
    Mat R_y = (Mat_<double>(3,3) <<
               cos(theta[1]),    0,      sin(theta[1]),
               0,               1,      0,
               -sin(theta[1]),   0,      cos(theta[1])
               );
    
    // Calculate rotation about z axis
    Mat R_z = (Mat_<double>(3,3) <<
               cos(theta[2]),    -sin(theta[2]),      0,
               sin(theta[2]),    cos(theta[2]),       0,
               0,               0,                  1);
    
    
    // Combined rotation matrix
    Mat R = R_z * R_y * R_x;
    
    return R;

}




Camera get_preset_camera(CameraPreset preset, Size input_size) {
    Mat camera_matrix = Mat::eye(3, 3, CV_64F);

    // Default: principal point is at the centre
    camera_matrix.at<double>(0, 2) = (input_size.width - 1.) / 2;
    camera_matrix.at<double>(1, 2) = (input_size.height - 1.) / 2;

    // Default: zero distortion coefficients
    Mat distortion_coefficients = Mat::zeros(4, 1, CV_64F);

    switch (preset) {
        case GOPRO_H4B_WIDE43_PUBLISHED:
            camera_matrix.at<double>(0, 2) = (input_size.width - 1.) / 2;
            camera_matrix.at<double>(1, 2) = (input_size.height - 1.) / 2;
            camera_matrix.at<double>(0, 0) = input_size.width /
                (2 * atan ((GOPRO_H4B_FOV_H_NOSTAB / 2) * CV_PI / 180)); // 1171.95
            camera_matrix.at<double>(1, 1) = input_size.height /
                (2 * atan ((GOPRO_H4B_FOV_V_NOSTAB / 2) * CV_PI / 180)); // 1044.87
            break;
        case GOPRO_H4B_WIDE43_MEASURED:
            // Measured values for GoPro Hero 4 Black with 4:3 "Wide" FOV setting and stabilisation disabled
            camera_matrix.at<double>(0, 2) = 967.37 * input_size.width / 1920;
            camera_matrix.at<double>(1, 2) = 711.07 * input_size.height / 1440;
            camera_matrix.at<double>(0, 0) = 942.96 * input_size.height / 1440;
            camera_matrix.at<double>(1, 1) = 942.53 * input_size.height / 1440;
            break;
        case GOPRO_H4B_WIDE43_MEASURED_STABILISATION:
            // Measured values for GoPro Hero 4 Black with 4:3 "Wide" FOV setting and stabilisation enabled
            camera_matrix.at<double>(0, 2) = 965.90 * input_size.width / 1920;
            camera_matrix.at<double>(1, 2) = 712.94 * input_size.height / 1440;
            camera_matrix.at<double>(0, 0) = 1045.58 * input_size.height / 1440;
            camera_matrix.at<double>(1, 1) = 1045.64 * input_size.height / 1440;
            break;
        case GOPRO_H4B_WIDE169_MEASURED:
            // Measured values for GoPro Hero 4 Black with 16 "Wide" FOV setting and stabilisation disabled
            camera_matrix.at<double>(0, 2) = 1361.80 * input_size.width / 2704;
            camera_matrix.at<double>(1, 2) = 745.19 * input_size.height / 1520;
            camera_matrix.at<double>(0, 0) = 1392.49 * input_size.height / 1520;
            camera_matrix.at<double>(1, 1) = 1383.47 * input_size.height / 1520;
        case GOPRO_H4B_WIDE169_MEASURED_STABILISATION:
            // Measured values for GoPro Hero 4 Black with 16 "Wide" FOV setting and stabilisation enabled
            camera_matrix.at<double>(0, 2) = 1357.49 * input_size.width / 2704;
            camera_matrix.at<double>(1, 2) = 736.74 * input_size.height / 1520;
            camera_matrix.at<double>(0, 0) = 1626.67 * input_size.height / 1520;
            camera_matrix.at<double>(1, 1) = 1619.46 * input_size.height / 1520;
    }

    Camera camera;
    camera.model = FISHEYE;
    camera.matrix = camera_matrix;
    camera.distortion_coefficients = distortion_coefficients;
    camera.size = input_size;
    return camera;
}

Camera get_output_camera(Camera input_camera, double scale) {
    Size input_size = input_camera.size;

    vector<Point2d> extreme_points;
    fisheye::undistortPoints(
        vector<Point2d>({
            // corners
            Point2d(0, 0),
            Point2d(0, input_size.height),
            Point2d(input_size.width, 0),
            Point2d(input_size.width, input_size.height),

            // midpoint of edges
            Point2d(input_size.width / 2, 0),
            Point2d(input_size.width, input_size.height / 2),
            Point2d(input_size.width / 2, input_size.height),
            Point2d(0, input_size.height / 2),
        }),
        extreme_points,
        input_camera.matrix,
        input_camera.distortion_coefficients
    );
    auto compare_x = [](const Point2d &point1, const Point2d &point2) { return point1.x < point2.x; };
    auto compare_y = [](const Point2d &point1, const Point2d &point2) { return point1.y < point2.y; };
    double max_x = max_element( begin(extreme_points), end(extreme_points), compare_x)->x;
    double min_x = min_element( begin(extreme_points), end(extreme_points), compare_x)->x;
    double max_y = max_element( begin(extreme_points), end(extreme_points), compare_y)->y;
    double min_y = min_element( begin(extreme_points), end(extreme_points), compare_y)->y;

    Matx33d matrix = Matx33d::eye();
    matrix(0, 0) = scale;
    matrix(1, 1) = scale;
    matrix(0, 2) = scale * (max_x - min_x) / 2;
    matrix(1, 2) = scale * (max_y - min_y) / 2;

    Camera camera;
    camera.model = RECTILINEAR;
    camera.matrix = matrix;
    camera.distortion_coefficients = Mat::zeros(4, 1, CV_64F);
    camera.size = Size(scale * (max_x - min_x), scale * (max_y - min_y));
    return camera;
}

FrameSourceWarp::FrameSourceWarp(std::shared_ptr<FrameSource> source, CameraPreset input_camera): m_source(source) {
    UMat first_frame = m_source->peek_frame();
    m_input_camera = get_preset_camera(
        input_camera,
        Size(first_frame.cols, first_frame.rows * 2 / 3)
    );
    m_output_camera = get_output_camera(m_input_camera, m_input_camera.size.width / 5);

    fisheye::initUndistortRectifyMap(
        m_input_camera.matrix,
        m_input_camera.distortion_coefficients,
        Mat::eye(3, 3, CV_64F),
        m_output_camera.matrix,
        m_output_camera.size,
        CV_16SC2,
        m_camera_map_1,
        m_camera_map_2
    );

    m_measured_rotation = Vec3f(0, 0, 0);
    m_corrected_rotation = Vec3f(0, 0, 0);
}

vector<Point2f> find_corners(UMat image) {
    vector <Point2f> corners;
    goodFeaturesToTrack(image, corners, 200, 0.01, 30);
    std::cerr << "Found " << corners.size() << " corners\n";

    // // Display corners
    // UMat frame_display = frame_gray.clone();
    // for (size_t i = 0; i < corners.size(); i++) {
    //     drawMarker(frame_display, corners[i], Scalar(0, 0, 255), MARKER_TRIANGLE_UP);
    // }
    // return frame_display;

    return corners;
}

pair<vector<Point2f>, vector<Point2f>> find_point_pairs_with_optical_flow(
    UMat prev_frame,
    UMat current_frame,
    vector<Point2f> prev_corners
) {
    // Given a set of points in a previous frame, calculate optical flow to the current frame
    vector <Point2f> current_corners_maybe, corners_filtered, last_frame_corners_filtered;
    vector <uchar> status;
    vector <float> err;

    calcOpticalFlowPyrLK(
        prev_frame,
        current_frame,
        prev_corners,
        current_corners_maybe,
        status,
        err
    );

    // Return point pairs for which optical flow was found
    vector<Point2f> prev_points, current_points;
    for (size_t i = 0; i < status.size(); i++) {
        if (status[i]) {
            prev_points.push_back(prev_corners[i]);
            current_points.push_back(current_corners_maybe[i]);
        }
    }
    return pair<vector<Point2f>, vector<Point2f>>(prev_points, current_points);
}

UMat FrameSourceWarp::change_projection(UMat input_camera_frame) {
    UMat output_camera_frame;
    remap(
        input_camera_frame,
        output_camera_frame,
        m_camera_map_1,
        m_camera_map_2,
        INTERPOLATION
    );
    return output_camera_frame;
}

Mat FrameSourceWarp::guess_camera_rotation(vector<Point2f> points_prev, vector<Point2f> points_current) {
    vector<Point2f> corners_output;
    fisheye::undistortPoints(
        points_current,
        corners_output,
        m_input_camera.matrix,
        m_input_camera.distortion_coefficients,
        Matx33d::eye(),
        m_output_camera.matrix
        // No output distortion coefficients...?
    );

    vector<Point2f> prev_corners_identity;
    fisheye::undistortPoints(
        points_prev,
        prev_corners_identity,
        m_input_camera.matrix,
        m_input_camera.distortion_coefficients
    );

    Mat rotation, translation;
    vector<Point3d> last_frame_corner_coordinates;
    for (size_t i = 0; i < prev_corners_identity.size(); ++i) {
        // Add noise to change the depth of each point. This prevents
        // the detection of translations, but doesn't affect rotations.
        double scale = rand() * 1. / RAND_MAX;
        last_frame_corner_coordinates.push_back(Point3d(
            prev_corners_identity[i].x * scale,
            prev_corners_identity[i].y * scale,
            scale
        ));
    }
    try {
        solvePnPRansac(
            last_frame_corner_coordinates,
            corners_output,
            m_output_camera.matrix,
            m_output_camera.distortion_coefficients,
            rotation,
            translation
        );
    } catch (cv::Exception &e) {
        cerr << "solvePnPRansac failed!" << endl;
        return Mat::eye(3, 3, CV_64F);
    }

    Rodrigues(rotation, rotation);
    return rotation;
}

UMat FrameSourceWarp::warp_frame(UMat input_frame) {
    // Create grayscale and BGR versions
    UMat frame_gray(input_frame, Rect(0, 0, input_frame.cols, input_frame.rows * 2 / 3));
    UMat output_frame;
    cvtColor(input_frame, output_frame, COLOR_YUV2BGR_NV12);

    // Change projection
    output_frame = change_projection(output_frame);

    if (m_last_key_frame_index == -1) {
        // This is the first frame
        m_last_key_frame_index = m_frame_index;
        m_last_input_frame_corners = find_corners(frame_gray);
    } else {

        /**
         * We sometimes reuse corners which were first detected in older frames, and since
         * successfully followed with optical flow. If it's been too long since we detected
         * corners from scratch or there are too few corners left from the original set,
         * we find a new set of corners.
         */
        if (m_frame_index - m_last_key_frame_index > 20 || m_last_input_frame_corners.size() < 150) {
            // Find corners in the last frame by Harris response
            m_last_key_frame_index = m_frame_index - 1;
            m_last_input_frame_corners = find_corners(m_last_input_frame);
        }

        // Use optical flow to see where the corners moved since the last frame
        pair<vector<Point2f>, vector<Point2f>> point_pairs = find_point_pairs_with_optical_flow(
            m_last_input_frame,
            frame_gray,
            m_last_input_frame_corners
        );
        m_last_input_frame_corners = point_pairs.second;

        // Calculate the camera rotation since the last frame with RANSAC
        m_measured_rotation += rotationMatrixToEulerAngles(
            guess_camera_rotation(point_pairs.first, point_pairs.second)
        );
        float gain = 0.1;
        m_corrected_rotation = gain * m_measured_rotation + (1 - gain) * m_corrected_rotation;
        cerr << "measured rotation: " << m_measured_rotation << endl;
        cerr << "corrected rotation: " << m_corrected_rotation << endl;

        // Stabilise by applying the inverse of the accumulated camera rotation
        Mat correction = eulerAnglesToRotationMatrix(m_corrected_rotation - m_measured_rotation);
        UMat temp;
        warpPerspective(
            output_frame,
            temp,
            m_output_camera.matrix * correction * m_output_camera.matrix.inv(),
            output_frame.size(),
            INTERPOLATION
        );
        output_frame = temp;
    }
    m_last_input_frame = frame_gray;
    ++m_frame_index;
    return output_frame;
}

UMat FrameSourceWarp::pull_frame() {
    UMat input_frame = m_source->pull_frame();
    UMat result = warp_frame(input_frame);
    return result;
}

UMat FrameSourceWarp::peek_frame() {
    return warp_frame(m_source->peek_frame());
}
