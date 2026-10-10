#ifndef SOBEL_H
#define SOBEL_H

#include <opencv2/core.hpp>

// Sobel filters carried over from Project 1 (video-special-effects). They now accept grayscale
// (CV_8UC1) as well as BGR (CV_8UC3) input; the output keeps the input's channel count.

// Sobel X as separable 1x3 filters, positive to the right. src is CV_8UC1 or CV_8UC3, dest
// becomes CV_16SC1 or CV_16SC3 in [-255, 255]; the 1-pixel border is 0. Returns 0 on success.
int sobelX3x3(cv::Mat& src, cv::Mat& dest);

// Sobel Y as separable 1x3 filters, positive up. src is CV_8UC1 or CV_8UC3, dest becomes
// CV_16SC1 or CV_16SC3 in [-255, 255]; the 1-pixel border is 0. Returns 0 on success.
int sobelY3x3(cv::Mat& src, cv::Mat& dest);

// Gradient magnitude: per channel sqrt(sx^2 + sy^2), clamped to 255. sx and sy are Sobel outputs
// of the same type and size (CV_16SC1 or CV_16SC3); dest becomes CV_8UC1 or CV_8UC3. Returns 0
// on success.
int magnitude(cv::Mat& sx, cv::Mat& sy, cv::Mat& dest);

#endif
