/*
  Author:  Sai Supriya Kotturu
  Date:    2026-10-06
  Purpose: Declares the distance metrics query ranks with (SSD, histogram intersection, the
           multi-histogram and custom weighted combinations, cosine) and distance(), which
           compares two feature vectors. Smaller always means more similar.
*/

#ifndef METRICS_H
#define METRICS_H

#include <cstddef>
#include <string>
#include <vector>

// Distance metrics compare two feature vectors; smaller means more similar.
//   SSD           sum of squared differences
//   INTERSECTION  1 - histogram intersection of one normalized histogram
//   MULTI         for vectors made of several concatenated histograms: the equally weighted
//                 mean of (1 - intersection) over each piece
//   COSINE        1 - cos(angle between the vectors); ignores vector length, which often suits
//                 high-dimensional embeddings better than SSD
//   CUSTOM        for the orange_dnn feature (two pieces: embedding, orange shade):
//                 0.7 * cosine distance of the embedding + 0.3 * (1 - intersection) of the
//                 orange shade histogram. Tuned on the orange-mask photos.
enum class Metric { SSD, INTERSECTION, MULTI, COSINE, CUSTOM };

// Parses the command-line spelling ("ssd", "intersection", "multi", "cosine", "custom").
// Returns false for an unknown name.
[[nodiscard]] bool parseMetric(const std::string& name, Metric& out);

// Precondition: a.size() == b.size(). Callers verify lengths once before ranking, so this
// does not re-check per pair.
// `chunks` is the length of each concatenated piece. It is used only by MULTI (any number of
// pieces) and CUSTOM (exactly two: embedding, orange shade); its sum must equal a.size().
// Accumulates in double: summing 147 squared differences of 0..255 pixels stays exact, and
// later features (histograms, embeddings) won't lose precision.
double distance(Metric metric, const std::vector<float>& a, const std::vector<float>& b,
                const std::vector<std::size_t>& chunks = {});

#endif
