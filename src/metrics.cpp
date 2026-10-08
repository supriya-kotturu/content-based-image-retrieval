#include <metrics.h>

#include <algorithm>
#include <cassert>
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
    }
    throw std::logic_error("unknown metric");
}
