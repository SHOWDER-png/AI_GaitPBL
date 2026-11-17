#pragma once
// Camera occlusion fallback (SCRUM-201, epic SCRUM-188).
//
// The frame-quality gate (SCRUM-380) throws a frame away as LOW_CONFIDENCE as soon as one knee
// or ankle is unreliable. That is right for the CSV training log, but on screen it means one
// leg hidden behind the other (normal in a front view when walking) blanks every metric.
// This module keeps the display and the downstream state going through short occlusions:
//
//   per leg   VISIBLE   hip (real or virtual) + knee + ankle reliable this frame
//             HELD      not visible now, last visible <= max_hold_ms ago: last good values kept
//             OCCLUDED  not visible for longer than max_hold_ms (or never seen): no values
//
//   overall   FULL             both legs VISIBLE
//             PARTIAL          at least one leg VISIBLE or HELD, not both VISIBLE
//             CAMERA_DEGRADED  both legs OCCLUDED: the camera cannot measure gait right now;
//                              use_imu_fallback is set so the caller can switch to IMU-only
//                              gait phase (SCRUM-179 chain) instead of showing stale numbers.
//
// Rules that keep it honest:
//   * Held values are never older than max_hold_ms (default 300 ms, about 3 frames at 10 FPS).
//   * Symmetry is only reported when both legs are VISIBLE in the same frame; a held leg never
//     produces a symmetry value.
//   * Time comes from the caller on one monotonic clock; time going backwards or a non-finite
//     time drops every hold (treated as OCCLUDED) instead of extending it.
//   * Nothing here writes the training CSV: main.cpp still logs only frames the quality gate
//     passed, with -1 for anything not measured.
//
// Header-only, no OpenCV calls; tested by tests/occlusion_fallback_tests.cpp.
#include <cmath>
#include <string>

#include "angle_calc.h"
#include "pose_detector.h"

namespace occlusion {

enum class LegState { VISIBLE, HELD, OCCLUDED };
enum class Mode { FULL, PARTIAL, CAMERA_DEGRADED };

inline const char* to_string(LegState state) {
    switch (state) {
        case LegState::VISIBLE: return "VISIBLE";
        case LegState::HELD: return "HELD";
        case LegState::OCCLUDED: return "OCCLUDED";
    }
    return "OCCLUDED";
}

inline const char* to_string(Mode mode) {
    switch (mode) {
        case Mode::FULL: return "FULL";
        case Mode::PARTIAL: return "PARTIAL";
        case Mode::CAMERA_DEGRADED: return "CAMERA_DEGRADED";
    }
    return "CAMERA_DEGRADED";
}

struct Config {
    double max_hold_ms = 300.0;      // longest time a hidden leg keeps its last values
    float keypoint_conf_min = 0.5f;  // same threshold as compute_gait and the quality gate
};

struct LegResult {
    LegState state = LegState::OCCLUDED;
    float knee_deg = -1.f;           // -1 when OCCLUDED
    float compression = 0.f;
    double hidden_for_ms = 0.0;      // 0 when VISIBLE
    std::string reason;              // why the leg is not visible ("" when VISIBLE)
};

struct Result {
    LegResult left;
    LegResult right;
    Mode mode = Mode::CAMERA_DEGRADED;
    float symmetry_pct = -1.f;       // only when both legs VISIBLE
    bool use_imu_fallback = true;    // true exactly when mode == CAMERA_DEGRADED
};

namespace detail {

inline bool reliable(const Landmark& p, float min_conf) {
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.score) &&
           p.score >= min_conf;
}

// Names the first missing joint of one leg, for the on-screen message and the log.
inline std::string why_hidden(const Detection* detection, bool left, float min_conf) {
    if (detection == nullptr) return "no usable detection";
    if (detection->kpts.size() < 17) return "detection has fewer than 17 keypoints";
    const int knee = left ? PoseDetector::L_KNEE : PoseDetector::R_KNEE;
    const int ankle = left ? PoseDetector::L_ANKLE : PoseDetector::R_ANKLE;
    const char* side = left ? "left" : "right";
    if (!reliable(detection->kpts[knee], min_conf)) return std::string(side) + " knee hidden";
    if (!reliable(detection->kpts[ankle], min_conf)) return std::string(side) + " ankle hidden";
    return std::string(side) + " hip hidden and no virtual hip";
}

struct LegMemory {
    bool have = false;
    double last_seen_ms = 0.0;
    float knee_deg = -1.f;
    float compression = 0.f;
};

}  // namespace detail

class Tracker {
public:
    explicit Tracker(Config config = Config{}) : config_(config) {}

    // detection: the frame's person detection, or nullptr when there is none or the frame was
    //            rejected for a reason other than low confidence (no person, stale, invalid).
    // gait:      compute_gait() on that detection, or nullptr when detection is nullptr.
    //            A leg counts as VISIBLE when compute_gait measured its knee angle.
    // now_ms:    capture time of the frame on the caller's monotonic clock.
    Result update(const Detection* detection, const GaitResult* gait, double now_ms) {
        Result result;
        const bool time_ok = std::isfinite(now_ms) && (!have_time_ || now_ms >= last_time_ms_);
        if (!time_ok) {
            // Never extend a hold on a broken clock: forget both legs.
            left_ = detail::LegMemory{};
            right_ = detail::LegMemory{};
        } else {
            last_time_ms_ = now_ms;
            have_time_ = true;
        }

        const GaitResult* g = (time_ok && detection != nullptr) ? gait : nullptr;
        result.left = update_leg(left_, g ? g->l_knee_angle : -1.f,
                                 g ? g->l_leg_compression : 0.f, now_ms, time_ok,
                                 time_ok ? detail::why_hidden(detection, true,
                                                              config_.keypoint_conf_min)
                                         : "timestamp invalid or went backwards");
        result.right = update_leg(right_, g ? g->r_knee_angle : -1.f,
                                  g ? g->r_leg_compression : 0.f, now_ms, time_ok,
                                  time_ok ? detail::why_hidden(detection, false,
                                                               config_.keypoint_conf_min)
                                          : "timestamp invalid or went backwards");

        const bool l_vis = result.left.state == LegState::VISIBLE;
        const bool r_vis = result.right.state == LegState::VISIBLE;
        const bool l_any = result.left.state != LegState::OCCLUDED;
        const bool r_any = result.right.state != LegState::OCCLUDED;
        if (l_vis && r_vis) {
            result.mode = Mode::FULL;
            result.symmetry_pct = g ? g->symmetry_index : -1.f;
        } else if (l_any || r_any) {
            result.mode = Mode::PARTIAL;
        } else {
            result.mode = Mode::CAMERA_DEGRADED;
        }
        result.use_imu_fallback = result.mode == Mode::CAMERA_DEGRADED;
        return result;
    }

    // Writes the fallback view into a GaitResult for display. Held legs get their last values;
    // occluded legs get -1; symmetry is -1 unless both legs are VISIBLE. Phase labels say HELD.
    static void apply(const Result& result, GaitResult& g) {
        auto fill = [](const LegResult& leg, float& knee, float& comp, std::string& phase,
                       const std::string& live_phase) {
            knee = leg.knee_deg;
            comp = leg.compression;
            phase = leg.state == LegState::VISIBLE ? live_phase
                    : leg.state == LegState::HELD  ? "HELD"
                                                   : "N/A";
        };
        fill(result.left, g.l_knee_angle, g.l_leg_compression, g.l_phase, g.squat_phase);
        fill(result.right, g.r_knee_angle, g.r_leg_compression, g.r_phase, g.squat_phase);
        g.symmetry_index = result.symmetry_pct;
        if (result.mode == Mode::CAMERA_DEGRADED) g.squat_phase = "N/A";
    }

    const Config& config() const { return config_; }

private:
    LegResult update_leg(detail::LegMemory& memory, float knee_deg, float compression,
                         double now_ms, bool time_ok, std::string reason) const {
        LegResult leg;
        if (time_ok && knee_deg >= 0.f && std::isfinite(knee_deg)) {
            memory.have = true;
            memory.last_seen_ms = now_ms;
            memory.knee_deg = knee_deg;
            memory.compression = compression;
            leg.state = LegState::VISIBLE;
            leg.knee_deg = knee_deg;
            leg.compression = compression;
            return leg;
        }
        leg.reason = std::move(reason);
        if (time_ok && memory.have) {
            leg.hidden_for_ms = now_ms - memory.last_seen_ms;
            if (leg.hidden_for_ms <= config_.max_hold_ms) {
                leg.state = LegState::HELD;
                leg.knee_deg = memory.knee_deg;
                leg.compression = memory.compression;
                return leg;
            }
        }
        leg.state = LegState::OCCLUDED;
        return leg;
    }

    Config config_;
    detail::LegMemory left_;
    detail::LegMemory right_;
    double last_time_ms_ = 0.0;
    bool have_time_ = false;
};

}  // namespace occlusion
