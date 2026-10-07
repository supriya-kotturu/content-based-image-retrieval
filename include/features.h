#ifndef FEATURES_H
#define FEATURES_H

#include <opencv2/core.hpp>
#include <vector>

constexpr const int PATCH_CHANNELS = 3;
constexpr const int DEFAULT_PATCH_SIZE = 7;
constexpr const char* DEFAULT_DATA_ROOT = "data";
constexpr const char* DEFAULT_CSV_SUBDIR = "csv";

// Fixed underlying type: keeps signedness identical across compilers
enum FeatureStatus : int {
    FEATURE_OK = 0,
    FEATURE_ERR_EMPTY_IMAGE = -1,
    FEATURE_ERR_BAD_TYPE = -2,
    FEATURE_ERR_BAD_PATCH_SIZE = -3,
    FEATURE_ERR_PATCH_TOO_LARGE = -4,
};

// Maps a FeatureStatus to a static, human-readable reason so callers can log
// "<filename>: <reason>". Never returns null; unrecognized codes yield "unknown error".
const char* featureErrorString(int status);

// Checks shared by every feature extractor: the image is non-empty and 8-bit 3-channel (BGR).
// Reports via return code only, so the caller (which knows the filename) owns the logging.
// Returns FEATURE_OK, FEATURE_ERR_EMPTY_IMAGE or FEATURE_ERR_BAD_TYPE.
int validateImage(const cv::Mat& img);

// Extracts a patchSize x patchSize square centered on the image into `out` as
// patchSize*patchSize*3 floats, row-major, B,G,R per pixel. Element i of two vectors must mean
// the same thing for SSD to be valid, hence the fixed order.
//
// patchSize must be positive and odd (so a true centre pixel exists) and no larger than the
// image. Per-image function: it neither logs nor aborts; whether to skip the image or stop the
// run is the caller's policy. FEATURE_ERR_BAD_PATCH_SIZE is a configuration error (same for
// every image), FEATURE_ERR_PATCH_TOO_LARGE depends on the individual image.
//
// `out` is cleared on entry and its contents are unspecified on failure.
// Returns FEATURE_OK or a negative FeatureStatus.
int baselineFeature(const cv::Mat& img, std::vector<float>& out,
                    int patchSize = DEFAULT_PATCH_SIZE);

#endif