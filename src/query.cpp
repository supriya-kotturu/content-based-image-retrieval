#include <algorithm>
#include <filesystem>
#include <iostream>
#include <opencv2/core/utils/logger.hpp>
#include <opencv2/imgcodecs.hpp>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "common.h"
#include "display.h"
#include "feature_store.h"
#include "features.h"
#include "metrics.h"

namespace fs = std::filesystem;

namespace {
const char* storeErrorString(int status) {
    switch (status) {
        case STORE_OK:
            return "ok";
        case STORE_ERR_IO:
            return "cannot read the csv";
        case STORE_ERR_NO_META:
            return "no .meta file next to the csv (was it made by extract?)";
        case STORE_ERR_BAD_META:
            return "the .meta file is malformed";
        case STORE_ERR_MISMATCH:
            return "the csv does not match its .meta (rows or vector length differ)";
        case STORE_ERR_UNKNOWN_FEATURE:
            return "the .meta names an unknown feature";
        default:
            return "unknown error";
    }
}

// Same feature function and patch the csv was built with, or the distances are meaningless
int computeTarget(const FeatureMeta& meta, const cv::Mat& img, std::vector<float>& out) {
    switch (meta.feature) {
        case FeatureType::BASELINE:
            return baselineFeature(img, out, meta.patch);
        case FeatureType::HISTOGRAM:
            return histogram(img, out, meta.bins);
        case FeatureType::MULTI_HISTOGRAM:
            return multiHistogram(img, out, meta.bins, meta.patch);
    }
    return FEATURE_ERR_BAD_TYPE;
}

// Piece lengths of a concatenated vector; only MULTI uses them, others compare as one vector
std::vector<std::size_t> chunksFor(const FeatureMeta& meta) {
    if (meta.feature == FeatureType::MULTI_HISTOGRAM) {
        return multiHistogramLayout(meta.bins);
    }
    return {};
}
}  // namespace

int main(int argc, char** argv) {
    cv::utils::logging::setLogLevel(cv::utils::logging::LOG_LEVEL_WARNING);

    Args args;
    if (!args.parse(argc, argv)) {
        std::cerr << "usage: query csv=<file> target=<image> [metric=ssd] [top=10]\n";
        return 1;
    }

    std::string csv = args.get("csv");
    std::string targetPath = args.get("target");
    std::string metricName = args.get("metric", "ssd");
    std::string topText = args.get("top", "10");
    std::string root = args.get("root", DEFAULT_DATA_ROOT);

    if (csv.empty() || targetPath.empty()) {
        std::cerr << "csv and target are required\n";
        return 1;
    }

    Metric metric = Metric::SSD;
    if (!parseMetric(metricName, metric)) {
        std::cerr << "unknown metric: " << metricName << "\n";
        return 1;
    }

    int top = 0;
    try {
        top = std::stoi(topText);
    } catch (const std::exception&) {
        top = 0;
    }
    if (top <= 0) {
        std::cerr << "invalid top: " << topText << "\n";
        return 1;
    }

    std::vector<std::string> unused = args.unusedKeys();
    if (!unused.empty()) {
        for (const auto& k : unused) {
            std::cerr << "unknown argument: " << k << "\n";
        }
        return 1;
    }

    FeatureMeta meta;
    std::vector<std::string> imagePaths;
    std::vector<std::vector<float>> vectors;
    StoreStatus status = loadFeatures(csv, meta, imagePaths, vectors);
    if (status != STORE_OK) {
        std::cerr << csv << ": " << storeErrorString(status) << "\n";
        return 1;
    }

    // Intersection assumes fractions summing to 1; baseline holds raw 0..255 pixels, so the
    // result would be a hugely negative distance and a silently meaningless ranking
    if (metric == Metric::INTERSECTION && meta.feature != FeatureType::HISTOGRAM) {
        std::cerr << "metric intersection needs a histogram csv, but " << csv << " is "
                  << featureTypeName(meta.feature) << "\n";
        return 1;
    }
    // multi splits the vector by the multi histogram's layout, so it only means anything there
    if (metric == Metric::MULTI && meta.feature != FeatureType::MULTI_HISTOGRAM) {
        std::cerr << "metric multi needs a multi_histogram csv, but " << csv << " is "
                  << featureTypeName(meta.feature) << "\n";
        return 1;
    }

    const std::vector<std::size_t> chunks = chunksFor(meta);
    std::size_t chunkTotal = 0;
    for (std::size_t length : chunks) {
        chunkTotal += length;
    }
    if (!chunks.empty() && chunkTotal != meta.vectorLength) {
        std::cerr << csv << ": vector length " << meta.vectorLength
                  << " does not match the layout for bins=" << meta.bins << "\n";
        return 1;
    }

    cv::Mat img = cv::imread(targetPath);
    if (img.empty()) {
        std::cerr << targetPath << ": could not read target image\n";
        return 1;
    }

    std::vector<float> target;
    int rc = computeTarget(meta, img, target);
    if (rc != FEATURE_OK) {
        std::cerr << targetPath << ": " << featureErrorString(rc) << "\n";
        return 1;
    }

    // Verified once here so distance() can assume equal lengths
    if (target.size() != meta.vectorLength) {
        std::cerr << "target vector length " << target.size() << " != csv vector length "
                  << meta.vectorLength << "\n";
        return 1;
    }

    // Compared by filename only: the csv stores root-relative names, the user types any path
    const std::string targetName = fs::path(targetPath).filename().generic_string();

    // (distance, row index): comparing pairs breaks ties by row order, so output is stable
    std::vector<std::pair<double, std::size_t>> ranked;

    ranked.reserve(vectors.size());
    for (std::size_t i = 0; i < vectors.size(); i++) {
        const std::string currentImageFile = fs::path(imagePaths[i]).filename().generic_string();
        if (currentImageFile == targetName) {
            continue;  // the target matches itself at 0; the handout's matches exclude it
        }
        ranked.emplace_back(distance(metric, target, vectors[i], chunks), i);
    }

    const std::size_t n = std::min<std::size_t>(static_cast<std::size_t>(top), ranked.size());
    std::partial_sort(ranked.begin(), ranked.begin() + static_cast<std::ptrdiff_t>(n),
                      ranked.end());

    // Print first: showResults blocks until a key is pressed
    std::vector<std::string> matchedPaths;
    std::vector<std::string> matchLabels;
    for (std::size_t r = 0; r < n; r++) {
        const std::string& stored = imagePaths[ranked[r].second];
        std::cout << r + 1 << "\t" << ranked[r].first << "\t" << stored << "\n";
        // Stored names are root-relative, so rebuild a path imread can open
        matchedPaths.push_back((fs::path(root) / stored).string());

        std::ostringstream label;
        label << "#" << r + 1 << "  " << fs::path(stored).filename().string() << "\n"
              << "dist " << ranked[r].first;
        matchLabels.push_back(label.str());
    }

    // Settings in the window so a screenshot for the report says how it was produced
    std::ostringstream title;
    title << featureTypeName(meta.feature);
    if (meta.feature == FeatureType::BASELINE) {
        title << "  patch=" << meta.patch << "x" << meta.patch;
    } else if (meta.feature == FeatureType::HISTOGRAM) {
        title << "  bins=" << meta.bins;
    } else {
        title << "  bins=" << meta.bins << "  center=" << meta.patch << "x" << meta.patch;
    }
    title << "  metric=" << metricName << "  top=" << n;

    showResults(targetPath, matchedPaths, matchLabels, title.str(), 300);
    return 0;
}
