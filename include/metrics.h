#ifndef METRICS_H
#define METRICS_H

#include <string>
#include <vector>

// Distance metrics compare two feature vectors; smaller means more similar.
//   SSD           sum of squared differences
//   INTERSECTION  1 - histogram intersection of one normalized histogram
//   MULTI         for vectors made of several concatenated histograms: the equally weighted
//                 mean of (1 - intersection) over each piece
//   COSINE        1 - cos(angle between the vectors); ignores vector length, which often suits
//                 high-dimensional embeddings better than SSD
enum class Metric { SSD, INTERSECTION, MULTI, COSINE };

// Parses the command-line spelling ("ssd", "intersection", "multi", "cosine"). Returns false for
// an unknown name.
[[nodiscard]] bool parseMetric(const std::string& name, Metric& out);

// Precondition: a.size() == b.size(). Callers verify lengths once before ranking, so this
// does not re-check per pair.
// `chunks` is the length of each concatenated piece and is used only by MULTI; its sum must
// equal a.size().
// Accumulates in double: summing 147 squared differences of 0..255 pixels stays exact, and
// later features (histograms, embeddings) won't lose precision.
double distance(Metric metric, const std::vector<float>& a, const std::vector<float>& b,
                const std::vector<std::size_t>& chunks = {});

#endif
