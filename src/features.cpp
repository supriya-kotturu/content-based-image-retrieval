#include <features.h>
#include <sobel.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <opencv2/imgproc.hpp>

namespace {
// Odd size so the patch has a true center pixel
int validatePatch(const cv::Mat& frame, int patchSize) {
    if (patchSize <= 0 || patchSize % 2 == 0) {
        return FEATURE_ERR_BAD_PATCH_SIZE;
    }
    if (frame.rows < patchSize || frame.cols < patchSize) {
        return FEATURE_ERR_PATCH_TOO_LARGE;
    }
    return FEATURE_OK;
}
}  // namespace

int validateImage(const cv::Mat& frame) {
    if (frame.empty()) {
        return FEATURE_ERR_EMPTY_IMAGE;
    }
    if (frame.type() != CV_8UC3) {
        return FEATURE_ERR_BAD_TYPE;
    }
    return FEATURE_OK;
}

const char* featureErrorString(int status) {
    switch (status) {
        case FEATURE_OK:
            return "ok";
        case FEATURE_ERR_EMPTY_IMAGE:
            return "empty image";
        case FEATURE_ERR_BAD_TYPE:
            return "image is not 8-bit 3-channel";
        case FEATURE_ERR_BAD_PATCH_SIZE:
            return "patch size must be positive and odd";
        case FEATURE_ERR_PATCH_TOO_LARGE:
            return "image is smaller than patch size";
        case FEATURE_ERR_BAD_BINS:
            return "bins must be positive";
        default:
            return "unknown error";
    }
}

int baselineFeature(const cv::Mat& frame, std::vector<float>& feature, int patchSize) {
    if (int rc = validateImage(frame); rc != FEATURE_OK) {
        return rc;
    }
    if (int rc = validatePatch(frame, patchSize); rc != FEATURE_OK) {
        return rc;
    }

    feature.clear();
    feature.reserve(patchSize * patchSize * PATCH_CHANNELS);

    int half = patchSize / 2;
    int startRow = (frame.rows / 2) - half;
    int startCol = (frame.cols / 2) - half;

    for (int r = startRow; r < startRow + patchSize; r++) {
        for (int c = startCol; c < startCol + patchSize; c++) {
            cv::Vec3b pixel = frame.at<cv::Vec3b>(r, c);
            feature.push_back(static_cast<float>(pixel[0]));  // Blue channel
            feature.push_back(static_cast<float>(pixel[1]));  // Green channel
            feature.push_back(static_cast<float>(pixel[2]));  // Red channel
        }
    }

    return FEATURE_OK;
}

namespace {
// Pixel channel indices in OpenCV's B, G, R order
constexpr int kBlue = 0;
constexpr int kGreen = 1;
constexpr int kRed = 2;

// Appends a bins*bins chromaticity histogram of `region`, using the two channels (first,
// second) divided by B+G+R. Dividing removes brightness, so a dark and a bright version of the
// same color land in the same bin.
void appendChromaHistogram(const cv::Mat& region, int bins, int first, int second,
                           std::vector<float>& out) {
    const std::size_t start = out.size();
    out.resize(start + static_cast<std::size_t>(bins) * static_cast<std::size_t>(bins), 0.0f);

    std::size_t counted = 0;
    for (int r = 0; r < region.rows; r++) {
        for (int c = 0; c < region.cols; c++) {
            const cv::Vec3b pixel = region.at<cv::Vec3b>(r, c);
            const int sum = pixel[kBlue] + pixel[kGreen] + pixel[kRed];
            if (sum == 0) {
                continue;  // black pixel has no chromaticity (0/0)
            }
            const float a = static_cast<float>(pixel[first]) / static_cast<float>(sum);
            const float b = static_cast<float>(pixel[second]) / static_cast<float>(sum);

            // min(): a fraction of exactly 1.0 would otherwise index one past the last bin
            const int aBin = std::min(static_cast<int>(a * static_cast<float>(bins)), bins - 1);
            const int bBin = std::min(static_cast<int>(b * static_cast<float>(bins)), bins - 1);
            out[start + static_cast<std::size_t>(aBin * bins + bBin)] += 1.0f;
            counted++;
        }
    }

    // Fractions, not counts, so region size doesn't leak into the intersection. An all-black
    // region is left as zeros (nothing to normalize) and is maximally distant from everything.
    if (counted > 0) {
        for (std::size_t i = start; i < out.size(); i++) {
            out[i] /= static_cast<float>(counted);
        }
    }
}

// Appends a `bins`-value histogram of gradient magnitudes from the Project 1 Sobel filters: how
// much of the image is flat versus strongly edged.
void appendSobelHistogram(const cv::Mat& frame, int bins, std::vector<float>& out) {
    const std::size_t start = out.size();
    out.resize(start + static_cast<std::size_t>(bins), 0.0f);

    // Grayscale first: Sobel on one channel does a third of the work of three, and the magnitude
    // is already the single channel the histogram needs
    cv::Mat gray;
    cv::Mat gradX;
    cv::Mat gradY;
    cv::Mat edgeGray;
    cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
    sobelX3x3(gray, gradX);
    sobelY3x3(gray, gradY);
    magnitude(gradX, gradY, edgeGray);  // CV_8UC1, clamped to 255

    for (int r = 0; r < edgeGray.rows; r++) {
        for (int c = 0; c < edgeGray.cols; c++) {
            // Most pixels have weak gradients, so evenly spaced bins would put nearly everything
            // in the first few. The square root spreads weak edges across more bins.
            const float scaled = static_cast<float>(edgeGray.at<uchar>(r, c)) / 255.0f;
            const int bin =
                std::min(static_cast<int>(std::sqrt(scaled) * static_cast<float>(bins)), bins - 1);
            out[start + static_cast<std::size_t>(bin)] += 1.0f;
        }
    }

    const float total = static_cast<float>(edgeGray.rows) * static_cast<float>(edgeGray.cols);
    for (std::size_t i = start; i < out.size(); i++) {
        out[i] /= total;
    }
}

// Appends a bins^3 RGB histogram of `region`. Keeps brightness, unlike chromaticity.
void appendRgbHistogram(const cv::Mat& region, int bins, std::vector<float>& out) {
    const std::size_t start = out.size();
    const std::size_t cells =
        static_cast<std::size_t>(bins) * static_cast<std::size_t>(bins) * static_cast<std::size_t>(bins);
    out.resize(start + cells, 0.0f);

    std::size_t counted = 0;
    for (int r = 0; r < region.rows; r++) {
        for (int c = 0; c < region.cols; c++) {
            const cv::Vec3b pixel = region.at<cv::Vec3b>(r, c);
            // 0..255 maps onto 0..bins-1 without a clamp: 255 * bins / 256 < bins
            const int redBin = pixel[kRed] * bins / 256;
            const int greenBin = pixel[kGreen] * bins / 256;
            const int blueBin = pixel[kBlue] * bins / 256;
            out[start + static_cast<std::size_t>((redBin * bins + greenBin) * bins + blueBin)] +=
                1.0f;
            counted++;
        }
    }

    if (counted > 0) {
        for (std::size_t i = start; i < out.size(); i++) {
            out[i] /= static_cast<float>(counted);
        }
    }
}
}  // namespace

int histogram(const cv::Mat& frame, std::vector<float>& feature, int bins) {
    if (int rc = validateImage(frame); rc != FEATURE_OK) {
        return rc;
    }

    if (bins <= 0) {
        return FEATURE_ERR_BAD_BINS;
    }

    feature.clear();
    appendChromaHistogram(frame, bins, kRed, kGreen, feature);
    return FEATURE_OK;
}

std::vector<std::size_t> textureColorLayout(int bins) {
    return {static_cast<std::size_t>(bins) * static_cast<std::size_t>(bins),
            static_cast<std::size_t>(bins)};
}

int textureColor(const cv::Mat& frame, std::vector<float>& feature, int bins) {
    if (int rc = validateImage(frame); rc != FEATURE_OK) {
        return rc;
    }
    if (bins <= 0) {
        return FEATURE_ERR_BAD_BINS;
    }

    feature.clear();
    appendChromaHistogram(frame, bins, kRed, kGreen, feature);
    appendSobelHistogram(frame, bins, feature);
    return FEATURE_OK;
}

std::vector<std::size_t> multiHistogramLayout(int bins) {
    const std::size_t chroma = static_cast<std::size_t>(bins) * static_cast<std::size_t>(bins);
    const std::size_t center = static_cast<std::size_t>(CENTER_RGB_BINS) *
                               static_cast<std::size_t>(CENTER_RGB_BINS) *
                               static_cast<std::size_t>(CENTER_RGB_BINS);
    return {chroma, chroma, center};
}

int multiHistogram(const cv::Mat& frame, std::vector<float>& feature, int bins, int centerSize) {
    if (int rc = validateImage(frame); rc != FEATURE_OK) {
        return rc;
    }
    if (bins <= 0) {
        return FEATURE_ERR_BAD_BINS;
    }
    if (int rc = validatePatch(frame, centerSize); rc != FEATURE_OK) {
        return rc;
    }

    // frame(Rect) is a view into the same pixels, not a copy
    const int halfRows = frame.rows / 2;
    const cv::Mat top = frame(cv::Rect(0, 0, frame.cols, halfRows));
    const cv::Mat bottom = frame(cv::Rect(0, halfRows, frame.cols, frame.rows - halfRows));

    const int half = centerSize / 2;
    const cv::Mat center = frame(
        cv::Rect(frame.cols / 2 - half, frame.rows / 2 - half, centerSize, centerSize));

    feature.clear();
    appendChromaHistogram(top, bins, kRed, kGreen, feature);      // rg
    appendChromaHistogram(bottom, bins, kBlue, kGreen, feature);  // bg
    appendRgbHistogram(center, CENTER_RGB_BINS, feature);
    return FEATURE_OK;
}
