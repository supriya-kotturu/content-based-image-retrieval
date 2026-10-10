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
#include "csv_util.h"
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
        case FeatureType::TEXTURE_COLOR:
            return textureColor(img, out, meta.bins);
    }
    return FEATURE_ERR_BAD_TYPE;
}

// Piece lengths of a concatenated vector; only MULTI uses them, others compare as one vector
std::vector<std::size_t> chunksFor(const FeatureMeta& meta) {
    switch (meta.feature) {
        case FeatureType::MULTI_HISTOGRAM:
            return multiHistogramLayout(meta.bins);
        case FeatureType::TEXTURE_COLOR:
            return textureColorLayout(meta.bins);
        case FeatureType::BASELINE:
        case FeatureType::HISTOGRAM:
            return {};
    }
    return {};
}

// Everything the ranking step needs, however the vectors were obtained
struct Prepared {
    std::vector<std::string> imagePaths;
    std::vector<std::vector<float>> vectors;
    std::vector<float> target;
    std::vector<std::size_t> chunks;
    fs::path imageBase;       // joined with a stored name to get a path imread can open
    std::string targetImage;  // path of the target image, for display
    std::string settings;     // feature description for the window title
};

// CSV written by extract: a .meta says how it was made, so the target is computed the same way
int prepareFromStore(const std::string& csv, const std::string& targetPath,
                     const std::string& root, Metric metric, Prepared& out) {
    FeatureMeta meta;
    StoreStatus status = loadFeatures(csv, meta, out.imagePaths, out.vectors);
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
    // multi splits the vector into concatenated pieces, so it only means anything on features
    // built that way
    if (metric == Metric::MULTI && meta.feature != FeatureType::MULTI_HISTOGRAM &&
        meta.feature != FeatureType::TEXTURE_COLOR) {
        std::cerr << "metric multi needs a multi_histogram or texture_color csv, but " << csv
                  << " is " << featureTypeName(meta.feature) << "\n";
        return 1;
    }

    out.chunks = chunksFor(meta);
    std::size_t chunkTotal = 0;
    for (std::size_t length : out.chunks) {
        chunkTotal += length;
    }
    if (!out.chunks.empty() && chunkTotal != meta.vectorLength) {
        std::cerr << csv << ": vector length " << meta.vectorLength
                  << " does not match the layout for bins=" << meta.bins << "\n";
        return 1;
    }

    cv::Mat img = cv::imread(targetPath);
    if (img.empty()) {
        std::cerr << targetPath << ": could not read target image\n";
        return 1;
    }

    int rc = computeTarget(meta, img, out.target);
    if (rc != FEATURE_OK) {
        std::cerr << targetPath << ": " << featureErrorString(rc) << "\n";
        return 1;
    }

    // Verified once here so distance() can assume equal lengths
    if (out.target.size() != meta.vectorLength) {
        std::cerr << "target vector length " << out.target.size() << " != csv vector length "
                  << meta.vectorLength << "\n";
        return 1;
    }

    out.imageBase = root;
    out.targetImage = targetPath;

    std::ostringstream settings;
    settings << featureTypeName(meta.feature);
    if (meta.feature == FeatureType::BASELINE) {
        settings << "  patch=" << meta.patch << "x" << meta.patch;
    } else if (meta.feature == FeatureType::HISTOGRAM ||
               meta.feature == FeatureType::TEXTURE_COLOR) {
        settings << "  bins=" << meta.bins;
    } else {
        settings << "  bins=" << meta.bins << "  center=" << meta.patch << "x" << meta.patch;
    }
    out.settings = settings.str();
    return 0;
}

// The provided ResNet18 CSV: bare filenames, 512 values, and no .meta. The target's vector comes
// from its own row, because we have no network to compute it with.
int prepareDnn(const std::string& csv, const std::string& targetPath, const std::string& db,
               Metric metric, Prepared& out) {
    // The vector length is arbitrary for this feature, so only these two make sense
    if (metric != Metric::SSD && metric != Metric::COSINE) {
        std::cerr << "feature dnn supports metric ssd or cosine\n";
        return 1;
    }

    std::string csvCopy = csv;
    std::vector<char*> rawNames;
    if (read_image_data_csv(csvCopy.data(), rawNames, out.vectors) != 0) {
        std::cerr << csv << ": cannot read the csv\n";
        return 1;
    }
    out.imagePaths.reserve(rawNames.size());
    for (char* name : rawNames) {
        out.imagePaths.emplace_back(name);
        delete[] name;
    }

    if (out.vectors.empty()) {
        std::cerr << csv << ": no rows\n";
        return 1;
    }
    // No meta to check against, so verify every row is the same length as the first
    const std::size_t length = out.vectors[0].size();
    for (const auto& v : out.vectors) {
        if (v.size() != length) {
            std::cerr << csv << ": rows have different lengths\n";
            return 1;
        }
    }

    // The user may type a bare name or a path; the csv only has bare names
    const std::string targetName = fs::path(targetPath).filename().generic_string();
    bool found = false;
    for (std::size_t i = 0; i < out.imagePaths.size(); i++) {
        if (fs::path(out.imagePaths[i]).filename().generic_string() == targetName) {
            out.target = out.vectors[i];
            found = true;
            break;
        }
    }
    if (!found) {
        std::cerr << "target " << targetName << " not found in " << csv << "\n";
        return 1;
    }

    out.imageBase = db;
    out.targetImage = (fs::path(db) / targetName).string();
    out.settings = "dnn  " + std::to_string(length) + "-d embedding";
    return 0;
}
}  // namespace

int main(int argc, char** argv) {
    cv::utils::logging::setLogLevel(cv::utils::logging::LOG_LEVEL_WARNING);

    Args args;
    if (!args.parse(argc, argv)) {
        std::cerr << "usage: query csv=<file> target=<image> [metric=ssd|intersection|multi|cosine] "
                     "[top=10] [root=data]\n"
                     "       query feature=dnn csv=<resnet csv> target=<name> [metric=ssd|cosine] "
                     "[db=data/olympus]\n";
        return 1;
    }

    std::string csv = args.get("csv");
    std::string targetPath = args.get("target");
    std::string metricName = args.get("metric", "ssd");
    std::string topText = args.get("top", "10");
    std::string root = args.get("root", DEFAULT_DATA_ROOT);
    // Only "dnn" is meaningful: every other feature is read from the csv's .meta
    std::string featureName = args.get("feature");
    std::string db = args.get("db", (fs::path(root) / DEFAULT_IMAGE_SUBDIR).generic_string());

    if (csv.empty() || targetPath.empty()) {
        std::cerr << "csv and target are required\n";
        return 1;
    }
    if (!featureName.empty() && featureName != "dnn") {
        std::cerr << "feature=" << featureName
                  << " is read from the csv's .meta; only feature=dnn may be given\n";
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

    Prepared q;
    const int prepared = featureName == "dnn" ? prepareDnn(csv, targetPath, db, metric, q)
                                              : prepareFromStore(csv, targetPath, root, metric, q);
    if (prepared != 0) {
        return prepared;
    }
    const std::vector<std::string>& imagePaths = q.imagePaths;
    const std::vector<std::vector<float>>& vectors = q.vectors;
    const std::vector<float>& target = q.target;
    const std::vector<std::size_t>& chunks = q.chunks;

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
        // Stored names are relative to imageBase (root, or the image folder for dnn)
        matchedPaths.push_back((q.imageBase / stored).string());

        std::ostringstream label;
        label << "#" << r + 1 << "  " << fs::path(stored).filename().string() << "\n"
              << "dist " << ranked[r].first;
        matchLabels.push_back(label.str());
    }

    // Settings in the window so a screenshot for the report says how it was produced
    std::ostringstream title;
    title << q.settings << "  metric=" << metricName << "  top=" << n;

    showResults(q.targetImage, matchedPaths, matchLabels, title.str(), 300);
    return 0;
}
