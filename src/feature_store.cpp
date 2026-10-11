/*
  Author:  Sai Supriya Kotturu
  Date:    2026-10-06
  Purpose: Definitions of saveFeatures/loadFeatures and the .meta file they keep beside every
           feature csv: which feature made it, its parameters, and its row and vector counts,
           checked against the csv on load. The csv itself stays in csv_util's format.
*/

#include <csv_util.h>
#include <feature_store.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

namespace {
namespace fs = std::filesystem;

// Key names as written in the .meta file (key=value per line); renaming one breaks old files
constexpr const char* KEY_FEATURE = "feature";
constexpr const char* KEY_PATCH = "patch";
constexpr const char* KEY_ROWS = "rows";
constexpr const char* KEY_BINS = "bins";
constexpr const char* KEY_VECTOR_LENGTH = "vector_length";

template <typename T>
void writeKV(std::ostream& out, const char* key, const T& value) {
    out << key << '=' << value << '\n';
}

// The .meta path for a csv: same folder and name, with the extension swapped
std::string metaPathFor(const std::string& csvPath) {
    return fs::path(csvPath).replace_extension(".meta").string();
}

std::string tmpPathFor(const std::string& path) {
    return path + ".tmp";
}

// Renaming over the old file swaps it in one step, so a reader sees the old file or the new one,
// never a half-written one. Windows refuses to replace a file another process has open (a query
// reading the csv), so retry for a few seconds before giving up.
bool replaceFile(const std::string& from, const std::string& to) {
    constexpr int kAttempts = 50;
    for (int attempt = 0; attempt < kAttempts; attempt++) {
        std::error_code ec;
        fs::rename(from, to, ec);
        if (!ec) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return false;
}

// Parses key=value lines into `meta`. Unknown keys are ignored; the keys the feature needs (see
// FeatureMeta) must all be present, so a typo or a deleted line is an error, not a silent default.
StoreStatus readMeta(const std::string& metaPath, FeatureMeta& meta) {
    if (!std::filesystem::exists(metaPath)) {
        return STORE_ERR_NO_META;
    }
    std::ifstream in(metaPath);
    if (!in) {
        return STORE_ERR_IO;
    }

    bool seenFeature = false, seenPatch = false, seenBins = false;
    bool seenRows = false, seenVectorLength = false;
    std::string line;

    // stoi/stoul throw on garbage; a corrupt meta must not crash the caller
    try {
        while (std::getline(in, line)) {
            // A file checked out on Windows (git autocrlf) has CRLF endings; on Linux getline
            // keeps the '\r', which would turn "histogram" into an unknown feature
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            size_t eqPos = line.find('=');
            if (eqPos == std::string::npos) {
                return STORE_ERR_BAD_META;
            }
            std::string key = line.substr(0, eqPos);
            std::string value = line.substr(eqPos + 1);

            if (key == KEY_FEATURE) {
                StoreStatus status = parseFeatureType(value, meta.feature);
                if (status != STORE_OK) {
                    return status;
                }
                seenFeature = true;
            } else if (key == KEY_PATCH) {
                meta.patch = std::stoi(value);
                seenPatch = true;
            } else if (key == KEY_ROWS) {
                meta.rows = std::stoul(value);
                seenRows = true;
            } else if (key == KEY_BINS) {
                meta.bins = std::stoi(value);
                seenBins = true;
            } else if (key == KEY_VECTOR_LENGTH) {
                meta.vectorLength = std::stoul(value);
                seenVectorLength = true;
            }
            // Unknown keys are ignored so a newer extract can add fields
        }
    } catch (const std::exception&) {
        return STORE_ERR_BAD_META;
    }

    if (!seenFeature || !seenRows || !seenVectorLength) {
        return STORE_ERR_BAD_META;
    }

    switch (meta.feature) {
        case FeatureType::BASELINE:
            return seenPatch ? STORE_OK : STORE_ERR_BAD_META;
        case FeatureType::HISTOGRAM:
            return seenBins ? STORE_OK : STORE_ERR_BAD_META;
        case FeatureType::MULTI_HISTOGRAM:
            // bins sizes the chromaticity pieces, patch is the center square's side
            return (seenBins && seenPatch) ? STORE_OK : STORE_ERR_BAD_META;
        case FeatureType::TEXTURE_COLOR:
            return seenBins ? STORE_OK : STORE_ERR_BAD_META;
        case FeatureType::ORANGE_DNN:
            return STORE_OK;  // fixed layout, no parameters
    }

    return STORE_ERR_BAD_META;
}

// Writes only the parameters the feature uses, matching what readMeta requires
StoreStatus writeMeta(const std::string& metaPath, const FeatureMeta& meta) {
    std::ofstream out(metaPath, std::ios::out | std::ios::trunc);
    if (!out) {
        return STORE_ERR_IO;
    }

    writeKV(out, KEY_FEATURE, featureTypeName(meta.feature));
    writeKV(out, KEY_VECTOR_LENGTH, meta.vectorLength);
    writeKV(out, KEY_ROWS, meta.rows);

    switch (meta.feature) {
        case FeatureType::BASELINE:
            writeKV(out, KEY_PATCH, meta.patch);
            break;
        case FeatureType::HISTOGRAM:
            writeKV(out, KEY_BINS, meta.bins);
            break;
        case FeatureType::MULTI_HISTOGRAM:
            writeKV(out, KEY_BINS, meta.bins);
            writeKV(out, KEY_PATCH, meta.patch);
            break;
        case FeatureType::TEXTURE_COLOR:
            writeKV(out, KEY_BINS, meta.bins);
            break;
        case FeatureType::ORANGE_DNN:
            break;
    }

    out.flush();
    return out ? STORE_OK : STORE_ERR_IO;
}

}  // namespace

StoreStatus parseFeatureType(const std::string& name, FeatureType& out) {
    if (name == "baseline") {
        out = FeatureType::BASELINE;
        return STORE_OK;
    } else if (name == "histogram") {
        out = FeatureType::HISTOGRAM;
        return STORE_OK;
    } else if (name == "multi_histogram") {
        out = FeatureType::MULTI_HISTOGRAM;
        return STORE_OK;
    } else if (name == "texture_color") {
        out = FeatureType::TEXTURE_COLOR;
        return STORE_OK;
    } else if (name == "orange_dnn") {
        out = FeatureType::ORANGE_DNN;
        return STORE_OK;
    }
    return STORE_ERR_UNKNOWN_FEATURE;
}

const char* featureTypeName(FeatureType type) {
    switch (type) {
        case FeatureType::BASELINE:
            return "baseline";
        case FeatureType::HISTOGRAM:
            return "histogram";
        case FeatureType::MULTI_HISTOGRAM:
            return "multi_histogram";
        case FeatureType::TEXTURE_COLOR:
            return "texture_color";
        case FeatureType::ORANGE_DNN:
            return "orange_dnn";
    }
    // No default above so -Wswitch flags a new enumerator; this covers out-of-range values
    return "unknown feature type";
}

StoreStatus saveFeatures(const std::string& csvPath, const FeatureMeta& meta,
                         const std::vector<std::string>& imagePaths,
                         const std::vector<std::vector<float>>& vectors) {
    if (imagePaths.empty() || imagePaths.size() != vectors.size()) {
        return STORE_ERR_IO;
    }
    const std::size_t vectorLength = vectors[0].size();
    for (const auto& v : vectors) {
        if (v.size() != vectorLength) {
            return STORE_ERR_IO;
        }
    }

    fs::path parent = fs::path(csvPath).parent_path();
    std::error_code ec;
    if (!parent.empty()) {
        fs::create_directories(parent, ec);
        if (ec) {
            return STORE_ERR_IO;
        }
    }

    // Both files are written in full under .tmp names and then swapped in, so a concurrent query
    // keeps reading the previous version instead of a partial one
    const std::string metaPath = metaPathFor(csvPath);
    const std::string csvTmp = tmpPathFor(csvPath);
    const std::string metaTmp = tmpPathFor(metaPath);

    // csv_util takes non-const char*, so work on copies
    std::string csvCopy = csvTmp;
    for (std::size_t idx = 0; idx < imagePaths.size(); idx++) {
        std::string pathCopy = imagePaths[idx];
        std::vector<float> rowCopy = vectors[idx];

        // reset_file on the first row truncates any existing CSV
        if (append_image_data_csv(csvCopy.data(), pathCopy.data(), rowCopy, idx == 0) != 0) {
            return STORE_ERR_IO;
        }
    }

    FeatureMeta finalMeta = meta;
    finalMeta.rows = vectors.size();
    finalMeta.vectorLength = vectorLength;
    StoreStatus status = writeMeta(metaTmp, finalMeta);
    if (status != STORE_OK) {
        return status;
    }

    // The pair can't be swapped in one step. csv first, meta last: between the two renames (or
    // after a crash there) the row counts disagree, which loadFeatures reports as a mismatch
    // rather than trusting a stale meta.
    if (!replaceFile(csvTmp, csvPath) || !replaceFile(metaTmp, metaPath)) {
        return STORE_ERR_IO;
    }
    return STORE_OK;
}

namespace {
StoreStatus loadOnce(const std::string& csvPath, FeatureMeta& meta,
                     std::vector<std::string>& imagePaths,
                     std::vector<std::vector<float>>& vectors) {
    imagePaths.clear();
    vectors.clear();

    // Meta first: it's cheap, and a missing or corrupt meta shouldn't cost a full CSV read
    StoreStatus status = readMeta(metaPathFor(csvPath), meta);
    if (status != STORE_OK) {
        return status;
    }

    // csv_util returns -1 for an un-openable file but is otherwise silent
    std::string csvCopy = csvPath;
    std::vector<char*> rawNames;
    if (read_image_data_csv(csvCopy.data(), rawNames, vectors) != 0) {
        return STORE_ERR_IO;
    }

    // csv_util allocates each name with new[]; take ownership into std::string and free it
    imagePaths.reserve(rawNames.size());
    for (char* name : rawNames) {
        imagePaths.emplace_back(name);
        delete[] name;
    }

    // Catches a CSV paired with the wrong meta, or a truncated/edited CSV
    if (vectors.size() != meta.rows) {
        return STORE_ERR_MISMATCH;
    }
    for (const auto& v : vectors) {
        if (v.size() != meta.vectorLength) {
            return STORE_ERR_MISMATCH;
        }
    }

    return STORE_OK;
}
}  // namespace

StoreStatus loadFeatures(const std::string& csvPath, FeatureMeta& meta,
                         std::vector<std::string>& imagePaths,
                         std::vector<std::vector<float>>& vectors) {
    // A mismatch can be transient: saveFeatures swaps the csv and its meta with two renames, and
    // a read can land between them. Only a mismatch that persists is a real error.
    constexpr int kAttempts = 4;
    StoreStatus status = STORE_ERR_MISMATCH;
    for (int attempt = 0; attempt < kAttempts && status == STORE_ERR_MISMATCH; attempt++) {
        if (attempt > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
        }
        status = loadOnce(csvPath, meta, imagePaths, vectors);
    }
    return status;
}
