#include "FrameSourceWarp.hpp"

#include <iostream>
#include <fstream>
#include <cerrno>
#include <math.h>
#include <cstdlib>

#include <CL/opencl.hpp>

#include <opencv2/calib3d.hpp>
#include <opencv2/video/tracking.hpp>

using namespace std;
using namespace cv;
using namespace gram_sg;

const int INTERPOLATION = INTER_LINEAR;

// Published values from
// https://community.gopro.com/t5/en/HERO4-Field-of-View-FOV-Information/ta-p/390285
const int GOPRO_H5B_FOV_H_43W_NOSTAB = 122.6;
const int GOPRO_H5B_FOV_V_43W_NOSTAB = 94.4;
const int GOPRO_H5B_FOV_H_169W_NOSTAB = 118.2;
const int GOPRO_H5B_FOV_V_169W_NOSTAB = 69.5;

Camera get_preset_camera(CameraPreset preset, Size input_size) {
    Mat camera_matrix = Mat::eye(3, 3, CV_64F);

    // Default: principal point is at the centre
    camera_matrix.at<double>(0, 2) = (input_size.width - 1.) / 2;
    camera_matrix.at<double>(1, 2) = (input_size.height - 1.) / 2;

    // Default: zero distortion coefficients
    Mat distortion_coefficients = Mat::zeros(4, 1, CV_64F);

    switch (preset) {
        case GOPRO_H4B_WIDE43_PUBLISHED:
            camera_matrix.at<double>(0, 0) = input_size.width /
                (GOPRO_H5B_FOV_H_43W_NOSTAB * CV_PI / 180);
            camera_matrix.at<double>(1, 1) = input_size.height /
                (GOPRO_H5B_FOV_V_43W_NOSTAB * CV_PI / 180);
            break;
        case GOPRO_H4B_WIDE169_PUBLISHED:
            camera_matrix.at<double>(0, 0) = input_size.width /
                (GOPRO_H5B_FOV_H_169W_NOSTAB * CV_PI / 180);
            camera_matrix.at<double>(1, 1) = input_size.height /
                (GOPRO_H5B_FOV_V_169W_NOSTAB * CV_PI / 180);
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
            break;
        case GOPRO_H4B_WIDE169_MEASURED_STABILISATION:
            // Measured values for GoPro Hero 4 Black with 16 "Wide" FOV setting and stabilisation enabled
            camera_matrix.at<double>(0, 2) = 1357.49 * input_size.width / 2704;
            camera_matrix.at<double>(1, 2) = 736.74 * input_size.height / 1520;
            camera_matrix.at<double>(0, 0) = 1626.67 * input_size.height / 1520;
            camera_matrix.at<double>(1, 1) = 1619.46 * input_size.height / 1520;
            break;
    }

    Camera camera;
    camera.model = FISHEYE;
    camera.matrix = camera_matrix;
    camera.distortion_coefficients = distortion_coefficients;
    camera.size = input_size;
    return camera;
}

Camera get_output_camera(Camera input_camera, double scale, bool crop_borders, double zoom) {
    Size input_size = input_camera.size;

    // Find the coordinates of the corners and edge midpoints in the identity camera
    vector<Point2d> extreme_points;
    fisheye::undistortPoints(
        vector<Point2d>({
            // corners
            Point2d(0, 0),
            Point2d(0, input_size.height - 1),
            Point2d(input_size.width - 1, 0),
            Point2d(input_size.width - 1, input_size.height - 1),

            // midpoint of edges
            Point2d(input_camera.matrix(0, 2), 0),
            Point2d(input_size.width - 1, input_camera.matrix(1, 2)),
            Point2d(input_camera.matrix(0, 2), input_size.height - 1),
            Point2d(0, input_camera.matrix(1, 2)),
        }),
        extreme_points,
        input_camera.matrix,
        input_camera.distortion_coefficients
    );

    // Find a bounding rectangle in the identity camera which maps to all points in the input
    auto compare_x = [](const Point2d &point1, const Point2d &point2) {
        return point1.x < point2.x;
    };
    auto compare_y = [](const Point2d &point1, const Point2d &point2) {
        return point1.y < point2.y;
    };
    int start_point = crop_borders ? 4 : 0;
    double max_x = max_element(
        begin(extreme_points) + start_point,
        end(extreme_points),
        compare_x
    )->x;
    double min_x = min_element(
        begin(extreme_points) + start_point,
        end(extreme_points),
        compare_x
    )->x;
    double max_y = max_element(
        begin(extreme_points) + start_point,
        end(extreme_points),
        compare_y
    )->y;
    double min_y = min_element(
        begin(extreme_points) + start_point,
        end(extreme_points),
        compare_y
    )->y;

    // Find (roughly) the average scale on the diagonal between the before/after cameras
    Point input_diagonal = Point2d(input_size.width - 1, input_size.height - 1);
    double input_diagonal_length = sqrt(
        1. * input_diagonal.x * input_diagonal.x + input_diagonal.y * input_diagonal.y
    );
    Point output_diagonal = extreme_points[3] - extreme_points[0];
    double output_diagonal_length = sqrt(
        1. * output_diagonal.x * output_diagonal.x + output_diagonal.y * output_diagonal.y
    );
    scale *= input_diagonal_length / output_diagonal_length;

    // Create output camera matrix, with the center positioned to ideally fit the remapped input
    Matx33d matrix = Matx33d::eye();
    matrix(0, 0) = scale;
    matrix(1, 1) = scale;
    matrix(0, 2) = scale * - min_x / zoom;
    matrix(1, 2) = scale * - min_y / zoom;

    Camera camera;
    camera.model = RECTILINEAR;
    camera.matrix = matrix;
    camera.distortion_coefficients = Mat::zeros(4, 1, CV_64F);
    camera.size = Size(scale * (max_x - min_x) / zoom, scale * (max_y - min_y) / zoom);
    return camera;
}

void init_filter(KalmanFilter &filter) {
    filter.init(2, 1);
    setIdentity(filter.measurementMatrix);
    setIdentity(filter.processNoiseCov, Scalar::all(1e-5));
    setIdentity(filter.measurementNoiseCov, Scalar::all(1e-1));
    setIdentity(filter.errorCovPost, Scalar::all(1));
    setIdentity(filter.transitionMatrix);
    filter.transitionMatrix.at<float>(0, 1) = 1;
}

string read_string_from_file(string file_name) {
    ifstream kernel_stream(file_name, ios::in | ios::binary);
    if (kernel_stream.fail()) {
        cerr << "Failed to open file \"" << file_name << "\": " << strerror(errno) << endl;
        throw(errno);
    }
    return string((istreambuf_iterator<char>(kernel_stream)), istreambuf_iterator<char>());
}

ocl::Program read_opencl_program_from_file(string file_name, string program_opts) {
    ocl::ProgramSource program_source(read_string_from_file(file_name));
    ocl::Context context = ocl::Context::getDefault(false);
    string err;
    ocl::Program program = context.getProg(program_source, program_opts, err);
    if (!err.empty()) {
        cerr << "Failed to read OpenCL program from file " << file_name <<
            " with opts \"" << program_opts << "\":\n" << err << endl;
        throw err;
    }
    return program;
}

FrameSourceWarp::FrameSourceWarp(
    std::shared_ptr<FrameSource> source,
    CameraPreset input_camera,
    double scale,
    bool crop_borders,
    double zoom,
    int smooth_radius,
    InterpolationFlags interpolation
):
    m_source(source),
    m_measured_rotation(Mat::eye(3, 3, CV_64F)),
    m_smooth_radius(smooth_radius),
    m_interpolation(interpolation),
    m_rotation_filter(RotationFilter(SavitzkyGolayFilterConfig(smooth_radius, 0, 2, 0)))
{
    UMat first_frame = m_source->peek_frame();
    m_input_camera = get_preset_camera(
        input_camera,
        Size(first_frame.cols, first_frame.rows * 2 / 3)
    );
    m_output_camera = get_output_camera(m_input_camera, scale, crop_borders, zoom);


    m_map_x = UMat(m_output_camera.size, CV_32F);
    m_map_y = UMat(m_output_camera.size, CV_32F);
    ocl::Program program = read_opencl_program_from_file("createMap.cl", "");
    m_remap_kernel = ocl::Kernel("createMap", program);
}

vector<Point2f> find_corners(UMat image) {
    vector <Point2f> corners;
    goodFeaturesToTrack(image, corners, 200, 0.01, 30);

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

UMat FrameSourceWarp::warp_frame(UMat input_camera_frame, Mat rotation) {
    UMat output_camera_frame;

    ocl::KernelArg map_x_args = cv::ocl::KernelArg::WriteOnly(m_map_x, m_map_x.channels());
    ocl::KernelArg map_y_args = cv::ocl::KernelArg::WriteOnlyNoSize(m_map_y, m_map_y.channels());

    size_t global_size[2] = { (size_t) m_map_x.cols, (size_t) m_map_x.rows };
    // rotation = Mat::eye(3, 3, CV_64F);
    ocl::Kernel kernel_with_args = m_remap_kernel.args(
        map_x_args,
        map_y_args,
        (cl_float) m_input_camera.matrix(0, 2),
        (cl_float) m_input_camera.matrix(1, 2),
        (cl_float) m_input_camera.matrix(0, 0),
        (cl_float) m_input_camera.matrix(1, 1),
        (cl_float) m_output_camera.matrix(0, 2),
        (cl_float) m_output_camera.matrix(1, 2),
        (cl_float) m_output_camera.matrix(0, 0),
        (cl_float) m_output_camera.matrix(1, 1),
        (cl_float) rotation.at<double>(0, 0),
        (cl_float) rotation.at<double>(0, 1),
        (cl_float) rotation.at<double>(0, 2),
        (cl_float) rotation.at<double>(1, 0),
        (cl_float) rotation.at<double>(1, 1),
        (cl_float) rotation.at<double>(1, 2),
        (cl_float) rotation.at<double>(2, 0),
        (cl_float) rotation.at<double>(2, 1),
        (cl_float) rotation.at<double>(2, 2)
    );
    if (!kernel_with_args.run(2, global_size, NULL, true)) {
        std::cerr << "executing kernel failed" << std::endl;
        throw -1;
    }

    remap(
        input_camera_frame,
        output_camera_frame,
        m_map_x,
        m_map_y,
        m_interpolation
    );
    return output_camera_frame;
}

int FrameSourceWarp::guess_camera_rotation(
    vector<Point2f> points_prev,
    vector<Point2f> points_current,
    OutputArray &rotation
) {
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

    Mat rotation_vector, translation;
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
    vector<int> inliers;
    try {
        solvePnPRansac(
            last_frame_corner_coordinates,
            corners_output,
            m_output_camera.matrix,
            m_output_camera.distortion_coefficients,
            rotation_vector,
            translation,
            false,
            100,
            8.0,
            0.99,
            inliers
        );
    } catch (cv::Exception &e) {
        cerr << "solvePnPRansac failed!" << endl;
        rotation.assign(Mat::eye(3, 3, CV_64F));
        return 0;
    }

    Rodrigues(rotation_vector, rotation);
    return inliers.size();
}

Eigen::Matrix3d eigen_mat_from_cv_mat (Mat cv_mat) {
    Eigen::Matrix3d eigen_mat;
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            eigen_mat(i, j) = cv_mat.at<double>(i, j);
        }
    }
    return eigen_mat;
}

Mat cv_mat_from_eigen_mat(Eigen::Matrix3d eigen_mat) {
    Mat cv_mat(3, 3, CV_64F);
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            cv_mat.at<double>(i, j) = eigen_mat(i, j);
        }
    }
    return cv_mat;
}

void FrameSourceWarp::consume_frame(UMat input_frame) {
    // Create grayscale and BGR versions
    UMat frame_gray(input_frame, Rect(0, 0, input_frame.cols, input_frame.rows * 2 / 3));
    UMat output_frame;
    cvtColor(input_frame, output_frame, COLOR_YUV2BGR_NV12);

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
        Mat rotation_since_last_frame;
        int num_inliers = guess_camera_rotation(point_pairs.first, point_pairs.second, rotation_since_last_frame);
        if (num_inliers < 40) {
            if (m_last_frame_rotation.empty()) {
                rotation_since_last_frame = Mat::eye(3, 3, CV_64F);
            } else {
                rotation_since_last_frame = m_last_frame_rotation;
            }
        }

        m_last_frame_rotation = rotation_since_last_frame;
        Mat accumulated_rotation = rotation_since_last_frame * m_measured_rotation;
        m_measured_rotation = accumulated_rotation;

        m_rotation_filter.add(eigen_mat_from_cv_mat(accumulated_rotation));
        m_buffered_frames.push(output_frame);
        m_buffered_rotations.push(accumulated_rotation);
    }
    m_last_input_frame = frame_gray;
    ++m_frame_index;
}

UMat FrameSourceWarp::pull_frame() {
    while(m_buffered_frames.size() <= m_smooth_radius) {
        try {
            consume_frame(m_source->pull_frame());
        } catch (int err) {
            if (err == EOF) {
                // Pretend the camera kept moving the same way after the last frame
                m_rotation_filter.add(eigen_mat_from_cv_mat(m_measured_rotation));
                break;
            }
            throw err;
        }
    }
    if (m_buffered_frames.size() == 0) {
        throw EOF;
    }
    // Stabilise by applying the inverse of the accumulated camera rotation
    UMat frame = m_buffered_frames.front();
    Mat measured_rotation = m_buffered_rotations.front();
    Mat corrected_rotation = cv_mat_from_eigen_mat(m_rotation_filter.filter());
    Mat rotation_correction = corrected_rotation * measured_rotation.inv();
    m_buffered_frames.pop();
    m_buffered_rotations.pop();
    return warp_frame(frame, rotation_correction.inv());
}

UMat FrameSourceWarp::peek_frame() {
    return pull_frame();
}
