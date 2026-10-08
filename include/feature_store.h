#ifndef FEATURE_STORE_H
#define FEATURE_STORE_H

#include <features.h>

#include <cstddef>
#include <string>
#include <vector>

// Fixed underlying type, same reasoning as FeatureStatus. Functions here never print;
// the caller owns reporting so it can prefix the filename.
enum StoreStatus : int {
    STORE_OK = 0,
    STORE_ERR_IO = -1,               // can't create dir, open, or write
    STORE_ERR_NO_META = -2,          // .meta missing next to the csv
    STORE_ERR_BAD_META = -3,         // missing key or unparsable value
    STORE_ERR_MISMATCH = -4,         // meta disagrees with the csv contents
    STORE_ERR_UNKNOWN_FEATURE = -5,  // feature name not recognized
};

enum class FeatureType { BASELINE, HISTOGRAM, MULTI_HISTOGRAM };

// Parses the on-disk / command-line spelling (e.g. "baseline") into `out`.
// Returns STORE_OK or STORE_ERR_UNKNOWN_FEATURE; `out` is untouched on failure. The caller
// decides what an unknown name means: a bad argument on the command line, a corrupt file in a
// .meta.
[[nodiscard]] StoreStatus parseFeatureType(const std::string& name, FeatureType& out);

// Inverse of parseFeatureType; returns a static string, never null. Used when writing the .meta.
const char* featureTypeName(FeatureType type);

// Describes how a stored CSV was produced, so query can compute the target's vector the same way.
// Lives in a .meta file beside the CSV rather than in the CSV itself, which keeps csv_util's
// format untouched and the file readable in Excel / pandas.
struct FeatureMeta {
    FeatureType feature = FeatureType::BASELINE;
    int patch = DEFAULT_PATCH_SIZE;
    int bins = DEFAULT_BIN_SIZE;
    std::size_t rows = 0;
    std::size_t vectorLength = 0;
};

// Writes imagePaths/vectors to csvPath and the config to the same-stem .meta beside it.
// imagePaths[i] is the image that vectors[i] was computed from.
//
// Preconditions: imagePaths.size() == vectors.size(), and every vector has the same length.
// meta.rows and meta.vectorLength are ignored; they are computed from the data so the meta can't
// disagree with the CSV. `imagePaths` are stored as given (the caller makes them relative to the
// data root and normalized); each must be shorter than 256 chars (csv_util's buffer).
//
// Order matters: creates the parent directory, deletes any old .meta, writes the CSV, and writes
// the new .meta last. A .meta that exists therefore means a completed write; a crash leaves a CSV
// with no meta, which loadFeatures refuses. Truncates an existing CSV.
// Returns STORE_OK or STORE_ERR_IO.
[[nodiscard]] StoreStatus saveFeatures(const std::string& csvPath, const FeatureMeta& meta,
                                       const std::vector<std::string>& imagePaths,
                                       const std::vector<std::vector<float>>& vectors);

// Reads csvPath and its same-stem .meta, filling meta (rows/vectorLength reflect what was read),
// imagePaths and vectors. The output parameters are cleared first and unspecified on failure.
//
// Verifies that every row's length equals meta.vectorLength and the row count equals meta.rows,
// so a CSV paired with the wrong meta fails here and not later in the distance loop.
// Moving or renaming the CSV without its .meta yields STORE_ERR_NO_META.
// Returns STORE_OK, STORE_ERR_IO, STORE_ERR_NO_META, STORE_ERR_BAD_META, STORE_ERR_MISMATCH or
// STORE_ERR_UNKNOWN_FEATURE.
[[nodiscard]] StoreStatus loadFeatures(const std::string& csvPath, FeatureMeta& meta,
                                       std::vector<std::string>& imagePaths,
                                       std::vector<std::vector<float>>& vectors);

#endif