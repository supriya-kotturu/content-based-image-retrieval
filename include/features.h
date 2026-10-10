#ifndef FEATURES_H
#define FEATURES_H

#include <opencv2/core.hpp>
#include <vector>

constexpr const int PATCH_CHANNELS = 3;
constexpr const int DEFAULT_PATCH_SIZE = 7;
constexpr const int DEFAULT_BIN_SIZE = 16;
constexpr const char* DEFAULT_DATA_ROOT = "data";
constexpr const char* DEFAULT_CSV_SUBDIR = "csv";
constexpr const char* DEFAULT_IMAGE_SUBDIR = "olympus";

// Fixed underlying type: keeps signedness identical across compilers
enum FeatureStatus : int {
    FEATURE_OK = 0,
    FEATURE_ERR_EMPTY_IMAGE = -1,
    FEATURE_ERR_BAD_TYPE = -2,
    FEATURE_ERR_BAD_PATCH_SIZE = -3,
    FEATURE_ERR_PATCH_TOO_LARGE = -4,
    FEATURE_ERR_BAD_BINS = -5,  // config error: same for every image, like BAD_PATCH_SIZE
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
int baselineFeature(const cv::Mat& frame, std::vector<float>& feature,
                    int patchSize = DEFAULT_PATCH_SIZE);

// Whole-image rg chromaticity histogram: bins*bins fractions summing to 1 (all zeros for an
// all-black image).
int histogram(const cv::Mat& frame, std::vector<float>& feature, int bins = DEFAULT_BIN_SIZE);

// Bins per channel for the centre region's RGB histogram. Fixed (8 -> 512 cells) because a
// bins^3 histogram at the user's `bins` would be enormous.
constexpr const int CENTER_RGB_BINS = 8;

// Default side of the centre square for multiHistogram. The baseline's 7 would leave a few dozen
// pixels spread over 512 cells, which says nothing about the image.
constexpr const int DEFAULT_CENTER_SIZE = 101;

// Sizes of the pieces multiHistogram concatenates, in order: top, bottom, centre. The metric
// needs these to split the vector back apart.
std::vector<std::size_t> multiHistogramLayout(int bins);

// Three histograms of different regions, concatenated into one vector:
//   top half     -> rg chromaticity, bins*bins
//   bottom half  -> bg chromaticity, bins*bins
//   centre       -> RGB, CENTER_RGB_BINS^3, over a centerSize x centerSize square
// Each piece is normalized to sum to 1 on its own. centerSize follows the same rules as the
// baseline's patch (positive, odd, no larger than the image).
int multiHistogram(const cv::Mat& frame, std::vector<float>& feature, int bins, int centerSize);

// Sizes of the pieces textureColor concatenates, in order: color, texture.
std::vector<std::size_t> textureColorLayout(int bins);

// Whole-image color and texture, concatenated:
//   color    -> rg chromaticity histogram, bins*bins
//   texture  -> histogram of Sobel gradient magnitudes of the grayscale image, bins values
// Each piece is normalized to sum to 1 on its own, so the equally weighted metric treats them
// the same however long they are.
int textureColor(const cv::Mat& frame, std::vector<float>& feature, int bins);

#endif