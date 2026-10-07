#include <features.h>

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
    feature.reserve(static_cast<size_t>(patchSize) * patchSize * PATCH_CHANNELS);

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
