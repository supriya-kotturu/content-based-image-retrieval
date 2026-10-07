#include <metrics.h>

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
}  // namespace

bool parseMetric(const std::string& name, Metric& out) {
    if (name == "ssd") {
        out = Metric::SSD;
        return true;
    }
    return false;
}

double distance(Metric metric, const std::vector<float>& a, const std::vector<float>& b) {
    assert(a.size() == b.size());
    // No default, so adding a Metric triggers -Wswitch here
    switch (metric) {
        case Metric::SSD:
            return ssd(a, b);
    }
    throw std::logic_error("unknown metric");
}
