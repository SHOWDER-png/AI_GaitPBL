// Unit tests for the camera occlusion fallback (SCRUM-201).
// No camera, model or image: detections are built from the real keypoints the pose model found
// on the recorded fixtures (Camera/results/scrum372-run-20261007, standing_frame_94 and
// walking_mid_frame_298), then joints are hidden to simulate occlusion.
#include <cmath>
#include <iostream>
#include <limits>
#include <string>

#include "angle_calc.h"
#include "occlusion_fallback.h"

namespace {

using occlusion::LegState;
using occlusion::Mode;

int failures = 0;

void check(bool condition, const std::string& description) {
    if (!condition) {
        std::cerr << "FAIL: " << description << '\n';
        ++failures;
    }
}

// x, y, confidence of hip/knee/ankle (left, right) from scrum372 lower-body records.
Detection from_record(const float (&p)[6][3]) {
    Detection d;
    d.conf = 0.9f;
    d.kpts.assign(17, Landmark{0.f, 0.f, 0.f});     // upper body: not visible
    const int index[6] = {PoseDetector::L_HIP, PoseDetector::R_HIP, PoseDetector::L_KNEE,
                          PoseDetector::R_KNEE, PoseDetector::L_ANKLE, PoseDetector::R_ANKLE};
    for (int i = 0; i < 6; ++i) d.kpts[index[i]] = Landmark{p[i][0], p[i][1], p[i][2]};
    return d;
}

const float kStanding94[6][3] = {{673.7f, 33.9f, 0.979f}, {561.1f, 31.8f, 0.977f},
                                 {672.1f, 263.7f, 0.995f}, {560.5f, 257.0f, 0.995f},
                                 {675.6f, 425.9f, 0.989f}, {577.7f, 417.0f, 0.989f}};
const float kWalkingMid298[6][3] = {{648.6f, 84.2f, 0.995f}, {565.8f, 86.0f, 0.995f},
                                    {648.8f, 240.3f, 0.998f}, {578.9f, 237.2f, 0.998f},
                                    {645.9f, 366.4f, 0.996f}, {605.8f, 358.0f, 0.996f}};

void hide(Detection& d, int index) { d.kpts[index].score = 0.1f; }

struct Frame {
    Detection detection;
    GaitResult gait;
};

Frame frame(const float (&p)[6][3]) {
    Frame f{from_record(p), {}};
    f.gait = compute_gait(f.detection.kpts);
    return f;
}

void refresh(Frame& f) { f.gait = compute_gait(f.detection.kpts); }

bool near(float a, float b) { return std::fabs(a - b) < 1e-4f; }

void test_both_legs_visible_is_full() {
    occlusion::Tracker t;
    Frame f = frame(kStanding94);
    const auto r = t.update(&f.detection, &f.gait, 0.0);
    check(r.mode == Mode::FULL, "both legs visible -> FULL");
    check(r.left.state == LegState::VISIBLE && r.right.state == LegState::VISIBLE,
          "both legs VISIBLE");
    check(near(r.left.knee_deg, f.gait.l_knee_angle), "left knee is the live value");
    check(r.left.knee_deg > 170.f && r.right.knee_deg > 165.f,
          "real standing frame gives near-straight knees");
    check(near(r.symmetry_pct, f.gait.symmetry_index) && r.symmetry_pct >= 0.f,
          "symmetry reported when both visible");
    check(!r.use_imu_fallback, "no IMU fallback while FULL");
}

void test_short_occlusion_holds_last_value() {
    occlusion::Tracker t;
    Frame f = frame(kWalkingMid298);
    const float live_left = t.update(&f.detection, &f.gait, 1000.0).left.knee_deg;
    hide(f.detection, PoseDetector::L_KNEE);
    refresh(f);
    check(f.gait.l_knee_angle < 0.f, "compute_gait drops a leg whose knee is hidden");
    const auto r = t.update(&f.detection, &f.gait, 1100.0);
    check(r.left.state == LegState::HELD, "left leg HELD 100 ms after it disappeared");
    check(near(r.left.knee_deg, live_left), "held knee equals the last live knee");
    check(std::fabs(r.left.hidden_for_ms - 100.0) < 1e-9, "hidden_for_ms counts from last seen");
    check(r.left.reason == "left knee hidden", "reason names the hidden joint");
    check(r.right.state == LegState::VISIBLE, "right leg stays VISIBLE");
    check(r.mode == Mode::PARTIAL, "one held leg -> PARTIAL");
    check(r.symmetry_pct < 0.f, "no symmetry from a held leg");
    check(!r.use_imu_fallback, "no IMU fallback while one leg is visible");
}

void test_hold_expires_at_limit() {
    occlusion::Tracker t;   // default 300 ms
    Frame f = frame(kWalkingMid298);
    t.update(&f.detection, &f.gait, 0.0);
    hide(f.detection, PoseDetector::R_ANKLE);
    refresh(f);
    check(t.update(&f.detection, &f.gait, 300.0).right.state == LegState::HELD,
          "exactly max_hold_ms is still HELD");
    const auto r = t.update(&f.detection, &f.gait, 300.5);
    check(r.right.state == LegState::OCCLUDED, "just past max_hold_ms -> OCCLUDED");
    check(r.right.knee_deg < 0.f, "occluded leg has no knee value");
    check(r.right.reason == "right ankle hidden", "ankle occlusion is named");
    check(r.mode == Mode::PARTIAL, "left still visible -> PARTIAL");
}

void test_leg_comes_back() {
    occlusion::Tracker t;
    Frame f = frame(kStanding94);
    t.update(&f.detection, &f.gait, 0.0);
    Frame hidden = f;
    hide(hidden.detection, PoseDetector::L_ANKLE);
    refresh(hidden);
    t.update(&hidden.detection, &hidden.gait, 1000.0);           // OCCLUDED by now
    const auto r = t.update(&f.detection, &f.gait, 1033.0);
    check(r.mode == Mode::FULL && r.left.state == LegState::VISIBLE,
          "a reappearing leg is VISIBLE again at once");
    check(r.left.hidden_for_ms == 0.0 && r.left.reason.empty(), "visible leg has no reason");
}

void test_both_hidden_long_is_camera_degraded() {
    occlusion::Tracker t;
    Frame f = frame(kStanding94);
    t.update(&f.detection, &f.gait, 0.0);
    const auto held = t.update(nullptr, nullptr, 200.0);
    check(held.mode == Mode::PARTIAL && held.left.state == LegState::HELD &&
              held.right.state == LegState::HELD,
          "person lost for 200 ms: both legs HELD, PARTIAL");
    check(held.left.reason == "no usable detection", "no-detection reason");
    const auto r = t.update(nullptr, nullptr, 400.0);
    check(r.mode == Mode::CAMERA_DEGRADED, "both legs gone > hold -> CAMERA_DEGRADED");
    check(r.use_imu_fallback, "CAMERA_DEGRADED asks for the IMU fallback");
}

void test_never_seen_is_degraded() {
    occlusion::Tracker t;
    const auto r = t.update(nullptr, nullptr, 0.0);
    check(r.mode == Mode::CAMERA_DEGRADED && r.left.state == LegState::OCCLUDED,
          "nothing seen yet -> CAMERA_DEGRADED, nothing held");
}

void test_time_backwards_drops_holds() {
    occlusion::Tracker t;
    Frame f = frame(kStanding94);
    t.update(&f.detection, &f.gait, 1000.0);
    const auto back = t.update(&f.detection, &f.gait, 900.0);
    check(back.mode == Mode::CAMERA_DEGRADED, "time going backwards is not trusted");
    check(back.left.reason == "timestamp invalid or went backwards", "clock reason given");
    const auto after = t.update(nullptr, nullptr, 1010.0);
    check(after.left.state == LegState::OCCLUDED,
          "hold is gone after a bad clock: nothing to extend");
    const auto nan = t.update(&f.detection, &f.gait,
                              std::numeric_limits<double>::quiet_NaN());
    check(nan.mode == Mode::CAMERA_DEGRADED, "NaN time is not trusted");
    const auto ok = t.update(&f.detection, &f.gait, 1100.0);
    check(ok.mode == Mode::FULL, "a good clock afterwards recovers");
}

void test_custom_hold() {
    occlusion::Config config;
    config.max_hold_ms = 0.0;
    occlusion::Tracker t(config);
    Frame f = frame(kStanding94);
    t.update(&f.detection, &f.gait, 0.0);
    check(t.update(nullptr, nullptr, 1.0).mode == Mode::CAMERA_DEGRADED,
          "max_hold_ms = 0 disables holding");
}

void test_apply_writes_display_view() {
    occlusion::Tracker t;
    Frame f = frame(kWalkingMid298);
    t.update(&f.detection, &f.gait, 0.0);
    GaitResult g = f.gait;                      // starts from a full frame...
    check(g.symmetry_index >= 0.f, "full frame has a symmetry value");
    hide(f.detection, PoseDetector::L_KNEE);
    refresh(f);
    occlusion::Tracker::apply(t.update(&f.detection, &f.gait, 50.0), g);
    check(g.l_knee_angle > 0.f && g.l_phase == "HELD", "apply fills the held leg and marks it");
    check(g.r_phase == g.squat_phase, "visible leg keeps its live phase");
    check(g.symmetry_index < 0.f, "apply clears symmetry when a leg is held");

    GaitResult degraded = f.gait;
    occlusion::Tracker::apply(t.update(nullptr, nullptr, 1000.0), degraded);
    check(degraded.l_knee_angle < 0.f && degraded.r_knee_angle < 0.f,
          "degraded view has no knee values");
    check(degraded.squat_phase == "N/A" && degraded.l_phase == "N/A", "degraded phase is N/A");
}

void test_virtual_hip_still_counts_as_visible() {
    occlusion::Tracker t;
    Frame f = frame(kStanding94);
    hide(f.detection, PoseDetector::L_HIP);
    refresh(f);
    check(f.gait.l_hip_virtual, "compute_gait uses the virtual hip");
    const auto r = t.update(&f.detection, &f.gait, 0.0);
    check(r.left.state == LegState::VISIBLE, "a leg measured with a virtual hip is VISIBLE");
}

}  // namespace

int main() {
    test_both_legs_visible_is_full();
    test_short_occlusion_holds_last_value();
    test_hold_expires_at_limit();
    test_leg_comes_back();
    test_both_hidden_long_is_camera_degraded();
    test_never_seen_is_degraded();
    test_time_backwards_drops_holds();
    test_custom_hold();
    test_apply_writes_display_view();
    test_virtual_hip_still_counts_as_visible();
    if (failures == 0) {
        std::cout << "occlusion_fallback_tests: all 10 groups passed\n";
        return 0;
    }
    std::cerr << failures << " check(s) failed\n";
    return 1;
}
