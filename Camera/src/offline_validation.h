#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace offline_validation {

inline bool confidence_meets_threshold(float confidence, float threshold) {
    return std::isfinite(confidence) && std::isfinite(threshold)
        && confidence >= threshold;
}

struct Metrics {
    std::size_t total_frames = 0;
    std::size_t valid_frames = 0;
    std::size_t rejected_frames = 0;
    std::size_t failed_frames = 0;
    std::size_t latency_samples = 0;
    double mean_latency_ms = 0.0;
    double p95_latency_ms = 0.0;
    double fps = 0.0;
};

inline Metrics calculate_metrics(
    std::size_t total_frames,
    std::size_t valid_frames,
    std::size_t rejected_frames,
    std::size_t failed_frames,
    std::vector<double> latencies_ms)
{
    Metrics result;
    result.total_frames = total_frames;
    result.valid_frames = valid_frames;
    result.rejected_frames = rejected_frames;
    result.failed_frames = failed_frames;
    result.latency_samples = latencies_ms.size();
    if (latencies_ms.empty()) return result;

    std::sort(latencies_ms.begin(), latencies_ms.end());
    double latency_sum = 0.0;
    for (const double latency : latencies_ms) latency_sum += latency;
    result.mean_latency_ms = latency_sum / latencies_ms.size();
    const std::size_t p95_index = static_cast<std::size_t>(
        std::ceil(0.95 * latencies_ms.size())) - 1;
    result.p95_latency_ms = latencies_ms[p95_index];
    if (latency_sum > 0.0) {
        result.fps = latencies_ms.size() * 1000.0 / latency_sum;
    }
    return result;
}

}  // namespace offline_validation
