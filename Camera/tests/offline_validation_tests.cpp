#include <cmath>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "offline_inference.h"
#include "offline_validation.h"

namespace {

int failures = 0;

void check(bool condition, const char* description) {
    if (!condition) {
        std::cerr << "FAIL: " << description << '\n';
        ++failures;
    }
}

void test_confidence_threshold_boundaries() {
    check(!offline_validation::confidence_meets_threshold(0.2999f, 0.3f),
          "person confidence below threshold is rejected");
    check(offline_validation::confidence_meets_threshold(0.3f, 0.3f),
          "person confidence equal to threshold is accepted");
    check(offline_validation::confidence_meets_threshold(0.8f, 0.3f),
          "person confidence above threshold is accepted");
    check(!offline_validation::confidence_meets_threshold(
              std::nanf(""), 0.3f),
          "non-finite confidence is rejected");
}

void test_no_person_json_record() {
    std::ostringstream json;
    write_offline_frame_json(json, "no_person/frame.png",
                             "/fixtures/no_person/frame.png", "no_person",
                             "439", "SCRUM-370-test", 0.3f, 0.5f,
                             1280, 720, 12.5, nullptr);
    const std::string record = json.str();
    check(record.find("\"detection_valid\":false") != std::string::npos,
          "no-person record explicitly marks detection invalid");
    check(record.find("\"person_confidence\":null") != std::string::npos,
          "no-person record has null person confidence");
    check(record.find("\"keypoints\":[]") != std::string::npos,
          "no-person record does not invent keypoints");
}

void test_keypoint_confidence_is_preserved() {
    Detection detection;
    detection.conf = 0.82f;
    detection.kpts.resize(17, Landmark{10.0f, 20.0f, 0.5f});
    detection.kpts[0] = Landmark{12.5f, 18.0f, 0.25f};

    std::ostringstream json;
    write_offline_frame_json(json, "walking/frame.png",
                             "/fixtures/walking/frame.png", "walking",
                             "251", "SCRUM-370-test", 0.3f, 0.5f,
                             1280, 720, 10.0, &detection);
    const std::string record = json.str();
    check(record.find("\"confidence\":0.25,\"valid\":false")
              != std::string::npos,
          "low keypoint confidence is preserved and marked invalid");
    check(record.find("\"confidence\":0.5,\"valid\":true")
              != std::string::npos,
          "keypoint confidence at or above its threshold is valid");
}

void test_metrics() {
    const auto metrics = offline_validation::calculate_metrics(
        4, 2, 1, 1, {10.0, 20.0, 30.0});
    check(metrics.total_frames == 4 && metrics.valid_frames == 2
              && metrics.rejected_frames == 1 && metrics.failed_frames == 1,
          "frame counts are retained");
    check(metrics.latency_samples == 3, "latency sample count is explicit");
    check(std::abs(metrics.mean_latency_ms - 20.0) < 1e-9,
          "mean latency is calculated");
    check(std::abs(metrics.p95_latency_ms - 30.0) < 1e-9,
          "p95 uses nearest-rank percentile");
    check(std::abs(metrics.fps - 50.0) < 1e-9,
          "FPS is based on summed inference time");

    const auto empty = offline_validation::calculate_metrics(0, 0, 0, 0, {});
    check(empty.latency_samples == 0 && empty.mean_latency_ms == 0.0
              && empty.p95_latency_ms == 0.0 && empty.fps == 0.0,
          "empty metrics avoid fabricated latency values");
}

}  // namespace

int main() {
    test_confidence_threshold_boundaries();
    test_no_person_json_record();
    test_keypoint_confidence_is_preserved();
    test_metrics();
    return failures == 0 ? 0 : 1;
}
