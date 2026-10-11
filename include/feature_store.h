/*
  Author:  Sai Supriya Kotturu
  Date:    2026-10-06
  Purpose: Declares FeatureType, FeatureMeta and saveFeatures/loadFeatures: how extract writes a
           feature file (a csv plus a .meta describing it) and how query reads it back and checks
           that the two agree.
*/

#ifndef FEATURE_STORE_H
#define FEATURE_STORE_H

#include <image_features.h>

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

enum class FeatureType { BASELINE, HISTOGRAM, MULTI_HISTOGRAM, TEXTURE_COLOR, ORANGE_DNN };

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
//
// Only the parameters a feature uses are written and required:
//   baseline         patch (side of the center square)
//   histogram        bins (per axis)
//   multi_histogram  bins and patch (side of the center square)
//   texture_color    bins
//   orange_dnn       none (fixed layout)
struct FeatureMeta {
    FeatureType feature = FeatureType::BASELINE;
    int patch = DEFAULT_PATCH_SIZE;
    int bins = DEFAULT_BIN_SIZE;
    std::size_t rows = 0;          // number of images
    std::size_t vectorLength = 0;  // values per image
};

// Writes imagePaths/vectors to csvPath and the config to the same-stem .meta beside it.
// imagePaths[i] is the image that vectors[i] was computed from.
//
// Preconditions: imagePaths.size() == vectors.size(), and every vector has the same length.
// meta.rows and meta.vectorLength are ignored; they are computed from the data so the meta can't
// disagree with the CSV. `imagePaths` are stored as given (the caller makes them relative to the
// data root and normalized); each must be shorter than 256 chars (csv_util's buffer).
//
// Atomic per file: the CSV and .meta are written in full as <name>.tmp, then renamed over the old
// ones (CSV first, .meta last), so a concurrent reader sees the previous version or the new one,
// never a partial file. Between the two renames the pair disagrees on row count, which
// loadFeatures retries through. On Windows a rename blocked by an open reader is retried for ~5 s.
// Returns STORE_OK or STORE_ERR_IO.
[[nodiscard]] StoreStatus saveFeatures(const std::string& csvPath, const FeatureMeta& meta,
                                       const std::vector<std::string>& imagePaths,
                                       const std::vector<std::vector<float>>& vectors);

// Reads csvPath and its same-stem .meta, filling meta (rows/vectorLength reflect what was read),
// imagePaths and vectors. The output parameters are cleared first and unspecified on failure.
//
// Verifies that every row's length equals meta.vectorLength and the row count equals meta.rows,
// so a CSV paired with the wrong meta fails here and not later in the distance loop. A mismatch is
// retried a few times (~0.5 s) first, since one can appear briefly while saveFeatures swaps files.
// Moving or renaming the CSV without its .meta yields STORE_ERR_NO_META.
// Returns STORE_OK, STORE_ERR_IO, STORE_ERR_NO_META, STORE_ERR_BAD_META, STORE_ERR_MISMATCH or
// STORE_ERR_UNKNOWN_FEATURE.
[[nodiscard]] StoreStatus loadFeatures(const std::string& csvPath, FeatureMeta& meta,
                                       std::vector<std::string>& imagePaths,
                                       std::vector<std::vector<float>>& vectors);

#endif