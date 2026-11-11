// Unit tests for no-person / stale-data / low-confidence handling (SCRUM-380).
// Synthetic detections only: no camera, model or image is needed.
#include <cmath>
#include <iostream>
#include <string>

#include "frame_quality.h"

namespace {

using frame_quality::Status;

int failures = 0;

void check(bool condition, const char* description) {
    if (!condition) {
        std::cerr << "FAIL: " << description << '\n';
        ++failures;
    }
}

Detection good_detection(float person = 0.9f, float keypoint = 0.9f) {
    Detection d;
    d.conf = person;
    d.kpts.assign(17, Landmark{100.f, 200.f, keypoint});
    return d;
}

bool mentions(const frame_quality::Assessment& a, const std::string& text) {
    for (const auto& reason : a.reasons)
        if (reason.find(text) != std::string::npos) return true;
    return false;
}

void test_good_frame_is_ok() {
    frame_quality::Tracker tracker;
    const Detection d = good_detection();
    const auto a = tracker.assess(&d, 1000.0, 1080.0);
    check(a.status == Status::OK && a.usable(), "good frame is OK and usable");
    check(a.valid_lower_body == 6, "all six lower-body keypoints counted");
    check(a.reasons.empty(), "OK frame has no reasons");
    check(std::abs(a.frame_age_ms - 80.0) < 1e-9, "frame age is now - capture");
}

void test_no_person() {
    frame_quality::Tracker tracker;
    const auto a = tracker.assess(nullptr, 1000.0, 1050.0);
    check(a.status == Status::NO_PERSON && !a.usable(), "null detection is NO_PERSON");
    check(mentions(a, "no person"), "NO_PERSON explains itself");
}

void test_low_person_confidence() {
    frame_quality::Tracker tracker;
    const Detection d = good_detection(0.29f, 0.9f);
    check(tracker.assess(&d, 0.0, 10.0).status == Status::LOW_CONFIDENCE,
          "person confidence just below 0.3 is LOW_CONFIDENCE");
    const Detection edge = good_detection(0.3f, 0.9f);
    check(tracker.assess(&edge, 20.0, 30.0).status == Status::OK,
          "person confidence exactly 0.3 is accepted");
}

void test_low_keypoint_confidence() {
    frame_quality::Tracker tracker;
    Detection d = good_detection();
    d.kpts[PoseDetector::L_ANKLE].score = 0.49f;
    const auto a = tracker.assess(&d, 0.0, 10.0);
    check(a.status == Status::LOW_CONFIDENCE, "one weak ankle is LOW_CONFIDENCE by default");
    check(a.valid_lower_body == 5, "five of six keypoints still counted valid");
    check(mentions(a, "left_ankle"), "reason names the weak keypoint");

    d.kpts[PoseDetector::L_ANKLE].score = 0.5f;
    check(tracker.assess(&d, 20.0, 30.0).status == Status::OK,
          "keypoint confidence exactly 0.5 is accepted");
}

void test_upper_body_does_not_matter() {
    frame_quality::Tracker tracker;
    Detection d = good_detection();
    d.kpts[PoseDetector::NOSE].score = 0.0f;
    d.kpts[PoseDetector::L_SHOULDER].score = 0.0f;
    check(tracker.assess(&d, 0.0, 10.0).status == Status::OK,
          "weak non-lower-body keypoints do not reject the frame");
}

void test_hips_optional_when_fallback_exists() {
    frame_quality::Config config;
    config.hips_required = false;
    frame_quality::Tracker tracker(config);
    Detection d = good_detection();
    d.kpts[PoseDetector::L_HIP].score = 0.1f;
    d.kpts[PoseDetector::R_HIP].score = 0.1f;
    const auto a = tracker.assess(&d, 0.0, 10.0);
    check(a.status == Status::OK && a.valid_lower_body == 4,
          "frame without hips is OK when hips are optional");
    check(a.reasons.empty(), "no reasons recorded for an accepted frame");
    d.kpts[PoseDetector::L_KNEE].score = 0.1f;
    const auto b = tracker.assess(&d, 20.0, 30.0);
    check(b.status == Status::LOW_CONFIDENCE && mentions(b, "left_knee"),
          "a weak knee is still rejected when hips are optional");
    Detection no_ankle = good_detection();
    no_ankle.kpts[PoseDetector::R_ANKLE].score = 0.0f;
    check(tracker.assess(&no_ankle, 40.0, 50.0).status == Status::LOW_CONFIDENCE,
          "a missing ankle is still rejected when hips are optional");

    frame_quality::Tracker strict;   // default: hips required
    Detection no_hip = good_detection();
    no_hip.kpts[PoseDetector::R_HIP].score = 0.1f;
    check(strict.assess(&no_hip, 0.0, 10.0).status == Status::LOW_CONFIDENCE,
          "a weak hip is rejected when hips are required");
}

void test_stale_data() {
    frame_quality::Tracker tracker;
    const Detection d = good_detection();
    check(tracker.assess(&d, 0.0, 500.0).status == Status::OK,
          "age exactly 500 ms is still fresh");
    const auto stale = tracker.assess(&d, 1000.0, 1500.1);
    check(stale.status == Status::STALE_DATA && !stale.usable(),
          "age above 500 ms is STALE_DATA");
    check(mentions(stale, "old"), "stale reason mentions the age");
    check(tracker.assess(nullptr, 2000.0, 2900.0).status == Status::STALE_DATA,
          "an old no-person result is stale, not NO_PERSON");
}

void test_invalid_timestamps() {
    frame_quality::Tracker tracker;
    const Detection d = good_detection();
    check(tracker.assess(&d, std::nan(""), 10.0).status == Status::INVALID_DATA,
          "NaN capture time is INVALID_DATA");
    check(tracker.assess(&d, 10.0, INFINITY).status == Status::INVALID_DATA,
          "infinite now time is INVALID_DATA");
    check(tracker.assess(&d, 100.0, 90.0).status == Status::INVALID_DATA,
          "capture time in the future is INVALID_DATA");

    frame_quality::Tracker monotonic;
    check(monotonic.assess(&d, 1000.0, 1010.0).status == Status::OK, "first frame OK");
    const auto back = monotonic.assess(&d, 900.0, 1020.0);
    check(back.status == Status::INVALID_DATA && mentions(back, "backwards"),
          "timestamp that goes backwards is INVALID_DATA");
    check(monotonic.assess(&d, 950.0, 1030.0).status == Status::INVALID_DATA,
          "a later but still older-than-latest timestamp stays invalid");
    check(monotonic.assess(&d, 1000.0, 1040.0).status == Status::OK,
          "same timestamp as the latest is accepted again");
}

void test_malformed_detections() {
    frame_quality::Tracker tracker;
    Detection few;
    few.conf = 0.9f;
    few.kpts.assign(10, Landmark{1.f, 1.f, 0.9f});
    check(tracker.assess(&few, 0.0, 10.0).status == Status::INVALID_DATA,
          "fewer than 17 keypoints is INVALID_DATA");
    Detection nan_conf = good_detection();
    nan_conf.conf = std::nanf("");
    check(tracker.assess(&nan_conf, 20.0, 30.0).status == Status::INVALID_DATA,
          "NaN person confidence is INVALID_DATA");
    Detection nan_point = good_detection();
    nan_point.kpts[PoseDetector::R_KNEE].x = std::nanf("");
    const auto a = tracker.assess(&nan_point, 40.0, 50.0);
    check(a.status == Status::LOW_CONFIDENCE && mentions(a, "right_knee"),
          "NaN keypoint coordinate makes that keypoint unreliable");
}

void test_priority_order() {
    frame_quality::Tracker tracker;
    Detection weak = good_detection(0.1f, 0.1f);
    check(tracker.assess(&weak, 0.0, 900.0).status == Status::STALE_DATA,
          "stale outranks low confidence");
    check(tracker.assess(&weak, 1000.0, 900.0).status == Status::INVALID_DATA,
          "invalid timestamp outranks everything");
}

void test_consecutive_bad_counter_and_recovery() {
    frame_quality::Tracker tracker;
    const Detection d = good_detection();
    tracker.assess(nullptr, 0.0, 10.0);
    tracker.assess(nullptr, 20.0, 30.0);
    check(tracker.consecutive_bad_frames() == 2, "two bad frames counted");
    check(tracker.assess(&d, 40.0, 50.0).status == Status::OK, "tracker recovers on a good frame");
    check(tracker.consecutive_bad_frames() == 0, "counter resets after a good frame");
}

void test_status_strings_are_stable() {
    check(std::string(frame_quality::to_string(Status::OK)) == "OK", "OK string");
    check(std::string(frame_quality::to_string(Status::NO_PERSON)) == "NO_PERSON",
          "NO_PERSON string");
    check(std::string(frame_quality::to_string(Status::LOW_CONFIDENCE)) == "LOW_CONFIDENCE",
          "LOW_CONFIDENCE string");
    check(std::string(frame_quality::to_string(Status::STALE_DATA)) == "STALE_DATA",
          "STALE_DATA string");
    check(std::string(frame_quality::to_string(Status::INVALID_DATA)) == "INVALID_DATA",
          "INVALID_DATA string");
}

}  // namespace

int main() {
    test_good_frame_is_ok();
    test_no_person();
    test_low_person_confidence();
    test_low_keypoint_confidence();
    test_upper_body_does_not_matter();
    test_hips_optional_when_fallback_exists();
    test_stale_data();
    test_invalid_timestamps();
    test_malformed_detections();
    test_priority_order();
    test_consecutive_bad_counter_and_recovery();
    test_status_strings_are_stable();
    if (failures == 0) std::cout << "frame_quality_tests: all passed\n";
    return failures == 0 ? 0 : 1;
}
