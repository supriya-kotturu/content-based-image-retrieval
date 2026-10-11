/*
  Author:  Sai Supriya Kotturu
  Date:    2026-10-06
  Purpose: Declares the image features (baseline patch, rg histogram, multi-histogram,
           texture + color, orange mask + DNN embedding), their default parameters and the status
           codes they return. Each feature turns one BGR image into one vector of floats.
*/

#ifndef IMAGE_FEATURES_H
#define IMAGE_FEATURES_H

#include <opencv2/core.hpp>

#include <cstddef>
#include <vector>

constexpr const int PATCH_CHANNELS = 3;     // B, G, R
constexpr const int DEFAULT_PATCH_SIZE = 7;  // side of the baseline's center square, in pixels
constexpr const int DEFAULT_BIN_SIZE = 16;   // histogram bins per axis
constexpr const char* DEFAULT_DATA_ROOT = "data";  // folder that holds the images and the csvs
constexpr const char* DEFAULT_CSV_SUBDIR = "csv";  // under the data root: extract's output
constexpr const char* DEFAULT_IMAGE_SUBDIR = "olympus";  // under the data root: the database

// Fixed underlying type: keeps signedness identical across compilers
enum FeatureStatus : int {
    FEATURE_OK = 0,
    FEATURE_ERR_EMPTY_IMAGE = -1,
    FEATURE_ERR_BAD_TYPE = -2,
    FEATURE_ERR_BAD_PATCH_SIZE = -3,
    FEATURE_ERR_PATCH_TOO_LARGE = -4,
    FEATURE_ERR_BAD_BINS = -5,      // config error: same for every image, like BAD_PATCH_SIZE
    FEATURE_ERR_NO_EMBEDDING = -6,  // no usable DNN embedding for this image
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
// patchSize must be positive and odd (so a true center pixel exists) and no larger than the
// image. Per-image function: it neither logs nor aborts; whether to skip the image or stop the
// run is the caller's policy. FEATURE_ERR_BAD_PATCH_SIZE is a configuration error (same for
// every image), FEATURE_ERR_PATCH_TOO_LARGE depends on the individual image.
//
// `out` is cleared on entry and its contents are unspecified on failure.
// Returns FEATURE_OK or a negative FeatureStatus.
int baselineFeature(const cv::Mat& frame, std::vector<float>& feature,
                    int patchSize = DEFAULT_PATCH_SIZE);

// Whole-image rg chromaticity histogram: bins*bins fractions summing to 1 (all zeros for an
// all-black image). Returns FEATURE_OK, FEATURE_ERR_EMPTY_IMAGE, FEATURE_ERR_BAD_TYPE or
// FEATURE_ERR_BAD_BINS.
int histogram(const cv::Mat& frame, std::vector<float>& feature, int bins = DEFAULT_BIN_SIZE);

// Bins per channel for the center region's RGB histogram. Fixed (8 -> 512 cells) because a
// bins^3 histogram at the user's `bins` would be enormous.
constexpr const int CENTER_RGB_BINS = 8;

// Default side of the center square for multiHistogram. The baseline's 7 would leave a few dozen
// pixels spread over 512 cells, which says nothing about the image.
constexpr const int DEFAULT_CENTER_SIZE = 101;

// Sizes of the pieces multiHistogram concatenates, in order: top, bottom, center. The metric
// needs these to split the vector back apart.
std::vector<std::size_t> multiHistogramLayout(int bins);

// Three histograms of different regions, concatenated into one vector:
//   top half     -> rg chromaticity, bins*bins
//   bottom half  -> bg chromaticity, bins*bins
//   center       -> RGB, CENTER_RGB_BINS^3, over a centerSize x centerSize square
// Each piece is normalized to sum to 1 on its own. centerSize follows the same rules as the
// baseline's patch (positive, odd, no larger than the image). Returns FEATURE_OK or the same
// errors as histogram() and baselineFeature().
int multiHistogram(const cv::Mat& frame, std::vector<float>& feature, int bins, int centerSize);

// Sizes of the pieces textureColor concatenates, in order: color, texture.
std::vector<std::size_t> textureColorLayout(int bins);

// Whole-image color and texture, concatenated:
//   color    -> rg chromaticity histogram, bins*bins
//   texture  -> histogram of Sobel gradient magnitudes of the grayscale image, bins values
// Each piece is normalized to sum to 1 on its own, so the equally weighted metric treats them
// the same however long they are. The Sobel filters are the Project 1 ones in sobel.h. Returns
// the same statuses as histogram().
int textureColor(const cv::Mat& frame, std::vector<float>& feature, int bins);

// Length of the provided ResNet18 embeddings (global average pooling output)
constexpr const int DNN_LENGTH = 512;

// Orange shade histogram: hue bins x saturation bins over the pixels the orange rule accepts
constexpr const int ORANGE_HUE_BINS = 8;
constexpr const int ORANGE_SAT_BINS = 4;
constexpr const int ORANGE_BINS = ORANGE_HUE_BINS * ORANGE_SAT_BINS;

// Sizes of the pieces orangeDnnFeature concatenates, in order: embedding, orange shade.
std::vector<std::size_t> orangeDnnLayout();

// Custom feature for photos containing a saturated orange object (the orange mask):
//   embedding    -> the provided DNN embedding, DNN_LENGTH values, copied as given
//   orange shade -> histogram of hue and saturation over only the saturated orange pixels,
//                   normalized to sum to 1 (zeros if the image has none)
// The embedding says "what is in the picture"; the orange part checks that the object has the
// right color whatever the background. Fails with FEATURE_ERR_NO_EMBEDDING if `embedding` does
// not have DNN_LENGTH values.
int orangeDnnFeature(const cv::Mat& frame, const std::vector<float>& embedding,
                     std::vector<float>& feature);

#endif