#pragma once
// No-person, stale-data and low-confidence handling (SCRUM-380, parent SCRUM-34).
//
// Decides, per frame, whether a pose detection may be used for gait analysis. Header-only
// and free of OpenCV/ONNX types so it can be unit-tested anywhere (tests/frame_quality_tests.cpp).
// Time is passed in by the caller (milliseconds on one monotonic clock), never read here, so
// every rule is deterministic and testable.
//
// Status priority when several conditions hold (most severe first):
//   INVALID_DATA > STALE_DATA > NO_PERSON > LOW_CONFIDENCE > OK
#include <array>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

#include "pose_detector.h"

namespace frame_quality {

enum class Status { OK, NO_PERSON, LOW_CONFIDENCE, STALE_DATA, INVALID_DATA };

inline const char* to_string(Status status) {
    switch (status) {
        case Status::OK: return "OK";
        case Status::NO_PERSON: return "NO_PERSON";
        case Status::LOW_CONFIDENCE: return "LOW_CONFIDENCE";
        case Status::STALE_DATA: return "STALE_DATA";
        case Status::INVALID_DATA: return "INVALID_DATA";
    }
    return "INVALID_DATA";
}

struct Config {
    float person_conf_min = 0.3f;      // same default as the detector call in main.cpp
    float keypoint_conf_min = 0.5f;    // same as the offline pipeline (SCRUM-371/372)
    // Knees and ankles are always required. Hips (COCO 11, 12) are required unless the
    // caller has a hip fallback (main.cpp uses the virtual-hip fallback, so it sets false).
    bool hips_required = true;
    double max_frame_age_ms = 500.0;   // result older than this is stale
};

struct Assessment {
    Status status = Status::NO_PERSON;
    int valid_lower_body = 0;          // lower-body keypoints that passed the checks
    double frame_age_ms = 0.0;
    std::vector<std::string> reasons;  // human-readable, for logs and the on-screen warning
    bool usable() const { return status == Status::OK; }
};

namespace detail {
inline constexpr std::array<int, 6> kLowerBody = {
    PoseDetector::L_HIP, PoseDetector::R_HIP, PoseDetector::L_KNEE,
    PoseDetector::R_KNEE, PoseDetector::L_ANKLE, PoseDetector::R_ANKLE};

inline const char* keypoint_name(int index) {
    switch (index) {
        case PoseDetector::L_HIP: return "left_hip";
        case PoseDetector::R_HIP: return "right_hip";
        case PoseDetector::L_KNEE: return "left_knee";
        case PoseDetector::R_KNEE: return "right_knee";
        case PoseDetector::L_ANKLE: return "left_ankle";
        case PoseDetector::R_ANKLE: return "right_ankle";
        default: return "keypoint";
    }
}
}  // namespace detail

// Stateful only to reject timestamps that go backwards and to count consecutive bad frames.
class Tracker {
public:
    explicit Tracker(Config config = Config{}) : config_(config) {}

    // detection == nullptr means the detector found no person in this frame.
    // capture_ms: when the frame was captured; now_ms: when the result is being used.
    Assessment assess(const Detection* detection, double capture_ms, double now_ms) {
        Assessment result = evaluate(detection, capture_ms, now_ms);
        if (result.status == Status::OK) {
            consecutive_bad_ = 0;
        } else {
            ++consecutive_bad_;
        }
        // Only move the "latest" timestamp forward, so one bad frame cannot lower the bar.
        if (std::isfinite(capture_ms) && (!have_last_ || capture_ms >= last_capture_ms_)) {
            last_capture_ms_ = capture_ms;
            have_last_ = true;
        }
        return result;
    }

    int consecutive_bad_frames() const { return consecutive_bad_; }
    const Config& config() const { return config_; }

private:
    Assessment evaluate(const Detection* detection, double capture_ms, double now_ms) const {
        Assessment result;

        // 1. Timestamps must be finite, not in the future, and not go backwards.
        if (!std::isfinite(capture_ms) || !std::isfinite(now_ms)) {
            result.status = Status::INVALID_DATA;
            result.reasons.push_back("timestamp is not a finite number");
            return result;
        }
        result.frame_age_ms = now_ms - capture_ms;
        if (result.frame_age_ms < 0.0) {
            result.status = Status::INVALID_DATA;
            result.reasons.push_back("frame timestamp is in the future");
            return result;
        }
        if (have_last_ && capture_ms < last_capture_ms_) {
            result.status = Status::INVALID_DATA;
            result.reasons.push_back("frame timestamp went backwards");
            return result;
        }

        // 2. A detection that is structurally broken is invalid, not merely low confidence.
        if (detection != nullptr) {
            if (detection->kpts.size() < 17) {
                result.status = Status::INVALID_DATA;
                result.reasons.push_back("detection has fewer than 17 keypoints");
                return result;
            }
            if (!std::isfinite(detection->conf)) {
                result.status = Status::INVALID_DATA;
                result.reasons.push_back("person confidence is not a finite number");
                return result;
            }
        }

        // 3. Old results must not drive the display or the gait metrics.
        if (result.frame_age_ms > config_.max_frame_age_ms) {
            result.status = Status::STALE_DATA;
            result.reasons.push_back("result is " + std::to_string((long long)result.frame_age_ms) +
                                     " ms old (limit " +
                                     std::to_string((long long)config_.max_frame_age_ms) + " ms)");
            return result;
        }

        // 4. Nobody in view.
        if (detection == nullptr) {
            result.status = Status::NO_PERSON;
            result.reasons.push_back("no person detected");
            return result;
        }

        // 5. Person found, but is it good enough?
        bool low = false;
        if (detection->conf < config_.person_conf_min) {
            low = true;
            result.reasons.push_back("person confidence " + std::to_string(detection->conf) +
                                     " below " + std::to_string(config_.person_conf_min));
        }
        for (const int index : detail::kLowerBody) {
            const Landmark& point = detection->kpts[static_cast<std::size_t>(index)];
            const bool ok = std::isfinite(point.x) && std::isfinite(point.y) &&
                            std::isfinite(point.score) &&
                            point.score >= config_.keypoint_conf_min;
            if (ok) {
                ++result.valid_lower_body;
                continue;
            }
            const bool is_hip = index == PoseDetector::L_HIP || index == PoseDetector::R_HIP;
            result.reasons.push_back(std::string(detail::keypoint_name(index)) +
                                     (is_hip && !config_.hips_required
                                          ? " not reliable (hip fallback allowed)"
                                          : " not reliable"));
            if (!is_hip || config_.hips_required) low = true;
        }
        result.status = low ? Status::LOW_CONFIDENCE : Status::OK;
        // An OK frame keeps no reasons; the weak hips are visible through valid_lower_body.
        if (!low) result.reasons.clear();
        return result;
    }

    Config config_;
    double last_capture_ms_ = 0.0;
    bool have_last_ = false;
    int consecutive_bad_ = 0;
};

}  // namespace frame_quality
