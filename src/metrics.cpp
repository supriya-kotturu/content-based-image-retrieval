/*
  Author:  Sai Supriya Kotturu
  Date:    2026-10-06
  Purpose: Definitions of the distance metrics declared in metrics.h: SSD, histogram intersection,
           the equally weighted multi-histogram distance, cosine distance, and the weighted
           embedding + orange-shade distance of the custom feature.
*/

#include <metrics.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <stdexcept>

namespace {
// The handout requires writing SSD yourself (no OpenCV function). Comparing an image with
// itself must give exactly 0.
double ssd(const std::vector<float>& a, const std::vector<float>& b) {
    double sum = 0.0;
    for (std::size_t i = 0; i < a.size(); i++) {
        const double diff = static_cast<double>(a[i]) - static_cast<double>(b[i]);
        sum += diff * diff;
    }

    return sum;
}

double intersection(const std::vector<float>& a, const std::vector<float>& b) {
    double sum = 0.0;
    for (std::size_t i = 0; i < a.size(); i++) {
        sum += std::min(a[i], b[i]);
    }

    return sum;
}

// Intersection of each concatenated piece separately, then the equally weighted mean of
// (1 - intersection). Summing across pieces would let the result exceed 1 and let a long piece
// outweigh a short one.
double multiIntersection(const std::vector<float>& a, const std::vector<float>& b,
                         const std::vector<std::size_t>& chunks) {
    double total = 0.0;
    std::size_t offset = 0;
    for (std::size_t length : chunks) {
        double overlap = 0.0;
        for (std::size_t i = offset; i < offset + length; i++) {
            overlap += std::min(a[i], b[i]);
        }
        total += 1.0 - overlap;
        offset += length;
    }
    return total / static_cast<double>(chunks.size());
}

// 1 - cos(theta) = 1 - (a . b) / (|a| |b|), the same as normalizing both to unit length and
// taking the dot product
double cosineDistance(const std::vector<float>& a, const std::vector<float>& b) {
    double dot = 0.0;
    double normA = 0.0;
    double normB = 0.0;
    for (std::size_t i = 0; i < a.size(); i++) {
        const double x = static_cast<double>(a[i]);
        const double y = static_cast<double>(b[i]);
        dot += x * y;
        normA += x * x;
        normB += y * y;
    }
    if (normA == 0.0 || normB == 0.0) {
        return 1.0;  // a zero vector has no direction, so treat it as unrelated to everything
    }
    // Rounding can leave a vector's distance to itself at ~1e-16 or slightly negative; clamp so
    // identical vectors report exactly 0
    return std::max(0.0, 1.0 - dot / (std::sqrt(normA) * std::sqrt(normB)));
}

// Weights found by trying 0.2 to 0.5 for the orange part on the orange-mask photos: 0.3 to 0.4
// gave the most of the other mask images in the top five
constexpr double kEmbeddingWeight = 0.7;
constexpr double kOrangeWeight = 0.3;

// chunks = {embedding length, orange shade length}
double customDistance(const std::vector<float>& a, const std::vector<float>& b,
                      const std::vector<std::size_t>& chunks) {
    const std::size_t split = chunks[0];
    const std::vector<float> embA(a.begin(), a.begin() + static_cast<std::ptrdiff_t>(split));
    const std::vector<float> embB(b.begin(), b.begin() + static_cast<std::ptrdiff_t>(split));

    double overlap = 0.0;
    for (std::size_t i = split; i < split + chunks[1]; i++) {
        overlap += std::min(a[i], b[i]);
    }
    return kEmbeddingWeight * cosineDistance(embA, embB) + kOrangeWeight * (1.0 - overlap);
}
}  // namespace

bool parseMetric(const std::string& name, Metric& out) {
    if (name == "ssd") {
        out = Metric::SSD;
        return true;
    }
    if (name == "intersection") {
        out = Metric::INTERSECTION;
        return true;
    }
    if (name == "multi") {
        out = Metric::MULTI;
        return true;
    }
    if (name == "cosine") {
        out = Metric::COSINE;
        return true;
    }
    if (name == "custom") {
        out = Metric::CUSTOM;
        return true;
    }
    return false;
}

double distance(Metric metric, const std::vector<float>& a, const std::vector<float>& b,
                const std::vector<std::size_t>& chunks) {
    assert(a.size() == b.size());
    // No default, so adding a Metric triggers -Wswitch here
    switch (metric) {
        case Metric::SSD:
            return ssd(a, b);
        case Metric::INTERSECTION:
            return 1 - intersection(a, b);
        case Metric::MULTI:
            assert(!chunks.empty());
            return multiIntersection(a, b, chunks);
        case Metric::COSINE:
            return cosineDistance(a, b);
        case Metric::CUSTOM:
            assert(chunks.size() == 2);
            return customDistance(a, b, chunks);
    }
    throw std::logic_error("unknown metric");
}
