/*
  Author:  Sai Supriya Kotturu
  Date:    2026-10-06
  Purpose: The extract program: computes one feature vector for every image in a folder and saves
           them, with a .meta describing the feature, for query to rank against. Offline step of
           the two-program pipeline, run once per feature and parameter setting.
*/

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <functional>
#include <iostream>
#include <map>
#include <opencv2/imgcodecs.hpp>
#include <string>
#include <vector>

#include "common.h"
#include "csv_util.h"
#include "feature_store.h"
#include "image_features.h"

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

// The file is passed too, for features that need more than pixels (the embedding is looked up by
// filename)
using FeatureFn = std::function<int(const fs::path&, const cv::Mat&, std::vector<float>&)>;

// The loop every per-image feature shares: read, compute, skip failures, store root-relative
// names. Returns FEATURE_ERR_BAD_PATCH_SIZE or FEATURE_ERR_BAD_BINS (config errors, identical for
// every image) to abort the run; any other per-image failure is skipped and counted.
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
        int rc = compute(files[i], img, vec);
        // Config errors are identical for every image, so retrying the rest is pointless
        if (rc == FEATURE_ERR_BAD_PATCH_SIZE || rc == FEATURE_ERR_BAD_BINS) {
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
            // Flushed: piped stdout is fully buffered, and the web app reads these as progress
            std::cout << "[" << i + 1 << "/" << files.size() << "]" << std::endl;
        }
    }
    return FEATURE_OK;
}

// One wrapper per feature: it only supplies the per-image callback. A feature that needs
// setup (e.g. loading a model once) does it here before calling extractAll.
int extractBaseline(const std::vector<fs::path>& files, const fs::path& root, int patchSize,
                    std::vector<std::string>& imagePaths, std::vector<std::vector<float>>& vectors,
                    std::size_t& skipped) {
    auto compute = [patchSize](const fs::path&, const cv::Mat& img, std::vector<float>& out) {
        return baselineFeature(img, out, patchSize);
    };
    return extractAll(files, root, compute, imagePaths, vectors, skipped);
}

int extractHistogram(const std::vector<fs::path>& files, const fs::path& root, int bins,
                     std::vector<std::string>& imagePaths, std::vector<std::vector<float>>& vectors,
                     std::size_t& skipped) {
    auto compute = [bins](const fs::path&, const cv::Mat& img, std::vector<float>& out) {
        return histogram(img, out, bins);
    };
    return extractAll(files, root, compute, imagePaths, vectors, skipped);
}

int extractMultiHistogram(const std::vector<fs::path>& files, const fs::path& root, int bins,
                          int centerSize, std::vector<std::string>& imagePaths,
                          std::vector<std::vector<float>>& vectors, std::size_t& skipped) {
    auto compute = [bins, centerSize](const fs::path&, const cv::Mat& img,
                                      std::vector<float>& out) {
        return multiHistogram(img, out, bins, centerSize);
    };
    return extractAll(files, root, compute, imagePaths, vectors, skipped);
}

int extractTextureColor(const std::vector<fs::path>& files, const fs::path& root, int bins,
                        std::vector<std::string>& imagePaths,
                        std::vector<std::vector<float>>& vectors, std::size_t& skipped) {
    auto compute = [bins](const fs::path&, const cv::Mat& img, std::vector<float>& out) {
        return textureColor(img, out, bins);
    };
    return extractAll(files, root, compute, imagePaths, vectors, skipped);
}

// The provided embeddings file: bare filename, then DNN_LENGTH values per row, keyed by filename
int loadEmbeddings(const std::string& path, std::map<std::string, std::vector<float>>& out) {
    std::string pathCopy = path;
    std::vector<char*> names;
    std::vector<std::vector<float>> rows;
    if (read_image_data_csv(pathCopy.data(), names, rows) != 0) {
        return -1;
    }
    for (std::size_t i = 0; i < names.size(); i++) {
        out[fs::path(names[i]).filename().generic_string()] = rows[i];
        delete[] names[i];
    }
    return 0;
}

int extractOrangeDnn(const std::vector<fs::path>& files, const fs::path& root,
                     const std::map<std::string, std::vector<float>>& embeddings,
                     std::vector<std::string>& imagePaths,
                     std::vector<std::vector<float>>& vectors, std::size_t& skipped) {
    auto compute = [&embeddings](const fs::path& file, const cv::Mat& img,
                                 std::vector<float>& out) {
        auto it = embeddings.find(file.filename().generic_string());
        if (it == embeddings.end()) {
            return static_cast<int>(FEATURE_ERR_NO_EMBEDDING);
        }
        return orangeDnnFeature(img, it->second, out);
    };
    return extractAll(files, root, compute, imagePaths, vectors, skipped);
}
}  // namespace

int main(int argc, char** argv) {
    Args args;
    if (!args.parse(argc, argv)) {
        std::cerr << "usage: extract db=<dir> [root=data] "
                     "[feature=baseline|histogram|multi_histogram|texture_color|orange_dnn] "
                     "[patch=<odd size>] [bins=16] [dnn=<embeddings csv>] [out=<dir>] "
                     "[append=0|1]\n";
        return 1;
    }

    args.print(std::cout);

    std::string root = args.get("root", DEFAULT_DATA_ROOT);
    std::string outDir = args.get("out", (fs::path(root) / DEFAULT_CSV_SUBDIR).generic_string());
    std::string featureName = args.get("feature", "baseline");
    std::string patchText = args.get("patch");  // empty = use the feature's own default
    std::string binsText = args.get("bins", std::to_string(DEFAULT_BIN_SIZE));
    std::string dnnCsv = args.get("dnn");  // only orange_dnn uses it
    std::string db = args.get("db");
    std::string appendText = args.get("append", "0");

    if (db.empty()) {
        std::cerr << "db is required\n";
        return 1;
    }

    FeatureType featureType;
    StoreStatus status = parseFeatureType(featureName, featureType);

    if (status != STORE_OK) {
        std::cerr << "unknown feature: " << featureName << "\n";
        return 1;
    }

    // `patch` means the baseline's patch side or the multi histogram's center side, and the
    // sensible default differs: 7 pixels is a fine patch but a useless histogram region
    if (patchText.empty()) {
        patchText = std::to_string(featureType == FeatureType::MULTI_HISTOGRAM
                                       ? DEFAULT_CENTER_SIZE
                                       : DEFAULT_PATCH_SIZE);
    }

    // stoi throws on non-numeric input; a bad patch is a config error, so abort the run
    int patchSize = 0;
    int bins = 0;
    try {
        patchSize = std::stoi(patchText);
    } catch (const std::exception&) {
        std::cerr << "invalid patch: " << patchText << "\n";
        return 1;
    }

    try {
        bins = std::stoi(binsText);
    } catch (const std::exception&) {
        std::cerr << "invalid bins: " << binsText << "\n";
        return 1;
    }

    if (appendText != "0" && appendText != "1") {
        std::cerr << "invalid append: " << appendText << " (use 0 or 1)\n";
        return 1;
    }
    const bool append = appendText == "1";

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
    std::cout << "found " << files.size() << " images" << std::endl;

    // Filename derived from the config so it can't disagree with the meta
    FeatureMeta meta;
    meta.feature = featureType;
    meta.patch = patchSize;
    meta.bins = bins;
    std::string csvName;

    switch (featureType) {
        case FeatureType::BASELINE:
            csvName = "baseline_" + std::to_string(meta.patch) + "x" + std::to_string(meta.patch) +
                      ".csv";
            break;
        case FeatureType::HISTOGRAM:
            csvName = "histogram_" + std::to_string(meta.bins) + ".csv";
            break;
        case FeatureType::MULTI_HISTOGRAM:
            csvName = "multi_histogram_" + std::to_string(meta.bins) + "_c" +
                      std::to_string(meta.patch) + ".csv";
            break;
        case FeatureType::TEXTURE_COLOR:
            csvName = "texture_color_" + std::to_string(meta.bins) + ".csv";
            break;
        case FeatureType::ORANGE_DNN:
            csvName = "orange_dnn.csv";
            break;
    }

    std::string csvPath = (fs::path(outDir) / csvName).generic_string();

    // append=1 reuses the rows already in the csv and computes only images that have none, so
    // adding one image costs one image, not the whole folder. The csv name encodes bins/patch,
    // so a loaded file was made with the same parameters.
    std::map<std::string, std::vector<float>> existing;
    if (append) {
        FeatureMeta oldMeta;
        std::vector<std::string> oldPaths;
        std::vector<std::vector<float>> oldVectors;
        StoreStatus loaded = loadFeatures(csvPath, oldMeta, oldPaths, oldVectors);
        if (loaded == STORE_OK && oldMeta.feature == featureType) {
            for (std::size_t i = 0; i < oldPaths.size(); i++) {
                existing.emplace(oldPaths[i], std::move(oldVectors[i]));
            }
        } else if (loaded == STORE_ERR_NO_META || !fs::exists(csvPath)) {
            std::cout << "append: no usable " << csvPath << " yet, extracting everything" << std::endl;
        } else {
            // A corrupt or mismatched file must not be silently extended
            std::cerr << csvPath << ": cannot append to it (status " << loaded
                      << "); run without append=1 to rebuild it\n";
            return 1;
        }
    }

    std::vector<fs::path> toCompute;
    for (const fs::path& file : files) {
        if (existing.count(fs::relative(file, root).generic_string()) == 0) {
            toCompute.push_back(file);
        }
    }

    std::vector<std::string> newPaths;
    std::vector<std::vector<float>> newVectors;
    std::size_t skipped = 0;

    // Switched once per run: each case calls a batch function that owns the loop. No default,
    // so adding a FeatureType triggers -Wswitch here.
    int rc = FEATURE_OK;
    if (!toCompute.empty()) {
        switch (featureType) {
            case FeatureType::BASELINE:
                rc = extractBaseline(toCompute, root, patchSize, newPaths, newVectors, skipped);
                break;
            case FeatureType::HISTOGRAM:
                rc = extractHistogram(toCompute, root, bins, newPaths, newVectors, skipped);
                break;
            case FeatureType::MULTI_HISTOGRAM:
                rc = extractMultiHistogram(toCompute, root, bins, patchSize, newPaths, newVectors,
                                           skipped);
                break;
            case FeatureType::TEXTURE_COLOR:
                rc = extractTextureColor(toCompute, root, bins, newPaths, newVectors, skipped);
                break;
            case FeatureType::ORANGE_DNN: {
                if (dnnCsv.empty()) {
                    std::cerr << "feature orange_dnn needs dnn=<embeddings csv>\n";
                    return 1;
                }
                std::map<std::string, std::vector<float>> embeddings;
                if (loadEmbeddings(dnnCsv, embeddings) != 0) {
                    std::cerr << dnnCsv << ": cannot read the embeddings csv\n";
                    return 1;
                }
                rc = extractOrangeDnn(toCompute, root, embeddings, newPaths, newVectors, skipped);
                break;
            }
        }
    }
    if (rc != FEATURE_OK) {
        std::cerr << featureErrorString(rc) << " (patch=" << patchSize << ", bins=" << bins
                  << ")\n";
        return 1;
    }

    // Rebuilt in the folder's sorted order, so an appended csv is identical to a full run's.
    // Rows for images no longer in the folder are dropped.
    std::map<std::string, std::vector<float>> computed;
    for (std::size_t i = 0; i < newPaths.size(); i++) {
        computed.emplace(newPaths[i], std::move(newVectors[i]));
    }
    std::vector<std::string> imagePaths;
    std::vector<std::vector<float>> vectors;
    std::size_t kept = 0;
    for (const fs::path& file : files) {
        const std::string rel = fs::relative(file, root).generic_string();
        if (auto it = existing.find(rel); it != existing.end()) {
            imagePaths.push_back(rel);
            vectors.push_back(std::move(it->second));
            existing.erase(it);
            kept++;
        } else if (auto jt = computed.find(rel); jt != computed.end()) {
            imagePaths.push_back(rel);
            vectors.push_back(std::move(jt->second));
        }
    }

    if (vectors.empty()) {
        std::cerr << "no image produced a feature; nothing to save\n";
        return 1;
    }

    if (saveFeatures(csvPath, meta, imagePaths, vectors) != STORE_OK) {
        std::cerr << "failed to write " << csvPath << "\n";
        return 1;
    }

    if (append) {
        std::cout << "append: kept " << kept << ", computed " << computed.size() << ", dropped "
                  << existing.size() << "\n";
    }
    std::cout << "saved " << vectors.size() << " vectors to " << csvPath << " (" << skipped
              << " skipped)\n";
    return 0;
}
