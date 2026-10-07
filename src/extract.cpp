#include <opencv2/imgcodecs.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

#include "common.h"
#include "feature_store.h"
#include "features.h"

namespace fs = std::filesystem;

namespace {
// Lowercase so ".JPG" and ".jpg" compare equal
std::string lowerExt(const fs::path& p) {
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

bool isImageFile(const fs::path& p) {
    std::string ext = lowerExt(p);
    return ext == ".jpg" || ext == ".jpeg" || ext == ".png";
}

// Sorted so the CSV row order is identical between runs (directory order is unspecified)
int listImages(const fs::path& dir, std::vector<fs::path>& files) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
        return -1;
    }
    for (const fs::directory_entry& entry : fs::directory_iterator(dir, ec)) {
        if (entry.is_regular_file() && isImageFile(entry.path())) {
            files.push_back(entry.path());
        }
    }
    if (ec) {
        return -1;
    }
    std::sort(files.begin(), files.end());
    return 0;
}

using FeatureFn = std::function<int(const cv::Mat&, std::vector<float>&)>;

// The loop every per-image feature shares: read, compute, skip failures, store root-relative
// names. Returns FEATURE_ERR_BAD_PATCH_SIZE (a config error, identical for every image) to abort
// the run; any other per-image failure is skipped and counted.
int extractAll(const std::vector<fs::path>& files, const fs::path& root, const FeatureFn& compute,
               std::vector<std::string>& imagePaths, std::vector<std::vector<float>>& vectors,
               std::size_t& skipped) {
    for (std::size_t i = 0; i < files.size(); i++) {
        cv::Mat img = cv::imread(files[i].string());
        if (img.empty()) {
            std::cerr << files[i].generic_string() << ": could not read image\n";
            skipped++;
            continue;
        }

        std::vector<float> vec;
        int rc = compute(img, vec);
        if (rc == FEATURE_ERR_BAD_PATCH_SIZE) {
            return rc;
        }
        if (rc != FEATURE_OK) {
            std::cerr << files[i].generic_string() << ": " << featureErrorString(rc) << "\n";
            skipped++;
            continue;
        }

        imagePaths.push_back(fs::relative(files[i], root).generic_string());
        vectors.push_back(std::move(vec));

        if ((i + 1) % 100 == 0) {
            std::cout << "[" << i + 1 << "/" << files.size() << "]\n";
        }
    }
    return FEATURE_OK;
}

// One wrapper per feature: it only supplies the per-image callback. A feature that needs
// setup (e.g. loading a model once) does it here before calling extractAll.
int extractBaseline(const std::vector<fs::path>& files, const fs::path& root, int patchSize,
                    std::vector<std::string>& imagePaths,
                    std::vector<std::vector<float>>& vectors, std::size_t& skipped) {
    auto compute = [patchSize](const cv::Mat& img, std::vector<float>& out) {
        return baselineFeature(img, out, patchSize);
    };
    return extractAll(files, root, compute, imagePaths, vectors, skipped);
}
}  // namespace

int main(int argc, char** argv) {
    Args args;
    if (!args.parse(argc, argv)) {
        std::cerr
            << "usage: extract db=<dir> [root=data] [feature=baseline] [patch=7] [out=<dir>]\n";
        return 1;
    }

    args.print(std::cout);

    std::string root = args.get("root", DEFAULT_DATA_ROOT);
    std::string outDir =
        args.get("out", (fs::path(root) / DEFAULT_CSV_SUBDIR).generic_string());
    std::string featureName = args.get("feature", "baseline");
    std::string patchText = args.get("patch", std::to_string(DEFAULT_PATCH_SIZE));
    std::string db = args.get("db");

    if (db.empty()) {
        std::cerr << "db is required\n";
        return 1;
    }

    // stoi throws on non-numeric input; a bad patch is a config error, so abort the run
    int patchSize = 0;
    try {
        patchSize = std::stoi(patchText);
    } catch (const std::exception&) {
        std::cerr << "invalid patch: " << patchText << "\n";
        return 1;
    }

    FeatureType featureType;
    StoreStatus status = parseFeatureType(featureName, featureType);

    if (status != STORE_OK) {
        std::cerr << "unknown feature: " << featureName << "\n";
        return 1;
    }

    // Report every unknown key, then abort once
    std::vector<std::string> unused = args.unusedKeys();
    if (!unused.empty()) {
        for (const auto& k : unused) {
            std::cerr << "unknown argument: " << k << "\n";
        }
        return 1;
    }

    // Stored names are relative to root, so db must live inside it
    std::error_code ec;
    fs::path dbRelative = fs::relative(db, root, ec);
    if (ec || dbRelative.empty() || *dbRelative.begin() == "..") {
        std::cerr << "db (" << db << ") must be inside root (" << root << ")\n";
        return 1;
    }

    std::vector<fs::path> files;
    if (listImages(db, files) != 0) {
        std::cerr << "cannot read directory: " << db << "\n";
        return 1;
    }
    if (files.empty()) {
        std::cerr << "no images (.jpg/.jpeg/.png) found in " << db << "\n";
        return 1;
    }
    std::cout << "found " << files.size() << " images\n";

    std::vector<std::string> imagePaths;
    std::vector<std::vector<float>> vectors;
    std::size_t skipped = 0;

    // Switched once per run: each case calls a batch function that owns the loop. No default,
    // so adding a FeatureType triggers -Wswitch here.
    int rc = FEATURE_ERR_BAD_TYPE;
    switch (featureType) {
        case FeatureType::BASELINE:
            rc = extractBaseline(files, root, patchSize, imagePaths, vectors, skipped);
            break;
    }
    if (rc != FEATURE_OK) {
        std::cerr << featureErrorString(rc) << " (patch=" << patchSize << ")\n";
        return 1;
    }

    if (vectors.empty()) {
        std::cerr << "no image produced a feature; nothing to save\n";
        return 1;
    }

    // Filename derived from the config so it can't disagree with the meta
    FeatureMeta meta;
    meta.feature = featureType;
    meta.patch = patchSize;
    std::string csvName = std::string(featureTypeName(featureType)) + "_" +
                          std::to_string(patchSize) + "x" + std::to_string(patchSize) + ".csv";
    std::string csvPath = (fs::path(outDir) / csvName).generic_string();

    if (saveFeatures(csvPath, meta, imagePaths, vectors) != STORE_OK) {
        std::cerr << "failed to write " << csvPath << "\n";
        return 1;
    }

    std::cout << "saved " << vectors.size() << " vectors to " << csvPath << " (" << skipped
              << " skipped)\n";
    return 0;
}
