#include "game/online/LatencyMeter.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace cosmic {

void LatencyMeter::add(double ms) {
    if (!std::isfinite(ms) || ms < 0.0 || ms > 60000.0) return;
    samples_.push_back(ms);
    while (samples_.size() > kSamples) samples_.pop_front();
}

int LatencyMeter::ms() const {
    if (samples_.empty()) return -1;
    std::vector<double> v(samples_.begin(), samples_.end());
    std::sort(v.begin(), v.end());
    const size_t n = v.size();
    const double median = n % 2 == 1 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) * 0.5;
    return static_cast<int>(std::lround(median));
}

bool LatencyMeter::probe(std::string_view path) { return path == "/api/v1/health" || path == "/api/v1/presence"; }

}  // namespace cosmic
