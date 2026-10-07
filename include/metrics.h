#ifndef METRICS_H
#define METRICS_H

#include <string>
#include <vector>

// Distance metrics compare two feature vectors; smaller means more similar.
enum class Metric { SSD };

// Parses the command-line spelling (e.g. "ssd"). Returns false for an unknown name.
[[nodiscard]] bool parseMetric(const std::string& name, Metric& out);

// Precondition: a.size() == b.size(). Callers verify lengths once before ranking, so this
// does not re-check per pair.
// Accumulates in double: summing 147 squared differences of 0..255 pixels stays exact, and
// later features (histograms, embeddings) won't lose precision.
double distance(Metric metric, const std::vector<float>& a, const std::vector<float>& b);

#endif
