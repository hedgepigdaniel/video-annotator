#ifndef _ROTATION_HPP_
#define _ROTATION_HPP_

// Coped from https://learnopencv.com/rotation-matrix-to-euler-angles/

#include <opencv2/core.hpp>

// Checks if a matrix is a valid rotation matrix.
bool isRotationMatrix(cv::Mat R);

// Calculates rotation matrix to euler angles
// The result is the same as MATLAB except the order
// of the euler angles ( x and z are swapped ).
cv::Vec3f rotationMatrixToEulerAngles(cv::Mat R);


// Calculates rotation matrix given euler angles.
cv::Mat eulerAnglesToRotationMatrix(cv::Vec3f theta);

#endif // _ROTATION_HPP_