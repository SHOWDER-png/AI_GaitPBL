// Real-model replay for the occlusion fallback (SCRUM-201). No camera or person needed.
//
//   ./occlusion_replay [manifest.csv] [model.onnx]       (run from the Camera/ directory)
//
// For every person fixture it runs the real pose model on
//   1. the original frame                      -> expected FULL
//   2. the frame with both shins blacked out   -> fed 100 ms later: both legs HELD (PARTIAL) with
//                                                 the frame-1 knee angles; 400 ms later still
//                                                 hidden: CAMERA_DEGRADED + IMU fallback
//   3. the frame with one leg blacked out      -> reported only (see the limitation below)
// Exit code 0 only if every check in 1 and 2 holds.
//
// Limitation measured with case 3: when only one leg is covered, YOLOv8n-pose still reports that
// leg with high confidence (it infers the hidden joints, 20-30 px off), so a confidence-based
// fallback cannot detect that kind of occlusion. The printed table records it on every run.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include "angle_calc.h"
#include "occlusion_fallback.h"
#include "pose_detector.h"

namespace {

int failures = 0;

void expect(bool ok, const std::string& fixture, const std::string& what) {
    if (!ok) {
        std::cout << "  FAIL " << fixture << ": " << what << '\n';
        ++failures;
    }
}

const Detection* first(const std::vector<Detection>& detections) {
    return detections.empty() ? nullptr : &detections.front();
}

}  // namespace

int main(int argc, char* argv[]) {
    const std::string manifest = argc > 1 ? argv[1] : "fixtures/manifest.csv";
    const std::string model = argc > 2 ? argv[2] : "models/yolov8n-pose.onnx";
    std::ifstream input(manifest);
    if (!input) {
        std::cerr << "ERROR: cannot open manifest " << manifest << '\n';
        return 2;
    }
    PoseDetector detector(model);
    std::string line;
    std::getline(input, line);  // header

    int people = 0;
    std::cout << "fixture,original,shins_hidden_100ms,shins_hidden_400ms,"
                 "one_leg_hidden_conf(knee/ankle),one_leg_knee_shift_px\n";
    while (std::getline(input, line)) {
        if (line.empty()) continue;
        const std::string fixture = line.substr(0, line.find(','));
        if (fixture.find("no_person") != std::string::npos) continue;
        const cv::Mat image = cv::imread("fixtures/" + fixture);
        if (image.empty()) {
            std::cout << "  FAIL cannot read fixtures/" << fixture << '\n';
            ++failures;
            continue;
        }
        ++people;

        // 1. Original frame.
        occlusion::Tracker tracker;
        const auto original = detector.detect(image, 0.3f);
        const Detection* d0 = first(original);
        if (d0 == nullptr) {
            expect(false, fixture, "no person on the original frame");
            continue;
        }
        const GaitResult g0 = compute_gait(d0->kpts);
        const auto r0 = tracker.update(d0, &g0, 0.0);
        expect(r0.mode == occlusion::Mode::FULL, fixture, "original frame is not FULL");
        const auto& k = d0->kpts;

        // 2. Both shins hidden (from just above the higher knee down).
        cv::Mat shins = image.clone();
        const int top = std::max(0, static_cast<int>(std::min(k[PoseDetector::L_KNEE].y,
                                                              k[PoseDetector::R_KNEE].y)) - 20);
        cv::rectangle(shins, cv::Rect(0, top, shins.cols, shins.rows - top), cv::Scalar(0, 0, 0),
                      cv::FILLED);
        const auto hidden = detector.detect(shins, 0.3f);
        const Detection* d1 = first(hidden);
        GaitResult g1;
        if (d1 != nullptr) g1 = compute_gait(d1->kpts);
        const auto r1 = tracker.update(d1, d1 ? &g1 : nullptr, 100.0);
        expect(r1.left.state == occlusion::LegState::HELD &&
                   r1.right.state == occlusion::LegState::HELD,
               fixture, "shins hidden 100 ms: both legs should be HELD");
        expect(std::fabs(r1.left.knee_deg - g0.l_knee_angle) < 1e-4f &&
                   std::fabs(r1.right.knee_deg - g0.r_knee_angle) < 1e-4f,
               fixture, "held knee angles differ from the last visible frame");
        expect(r1.symmetry_pct < 0.f, fixture, "symmetry reported from held legs");
        const auto r2 = tracker.update(d1, d1 ? &g1 : nullptr, 400.0);
        expect(r2.mode == occlusion::Mode::CAMERA_DEGRADED && r2.use_imu_fallback, fixture,
               "shins hidden 400 ms: expected CAMERA_DEGRADED with IMU fallback");

        // 3. One leg (the left) hidden: report what the model does.
        cv::Mat one = image.clone();
        const int lh = PoseDetector::L_HIP, lk = PoseDetector::L_KNEE, la = PoseDetector::L_ANKLE;
        const float x0 = std::min({k[lh].x, k[lk].x, k[la].x}) - 25.f;
        const float x1 = std::max({k[lh].x, k[lk].x, k[la].x}) + 25.f;
        const float y0 = std::min(k[lk].y, k[la].y) - 40.f;
        const float y1 = std::max(k[lk].y, k[la].y) + 40.f;
        cv::rectangle(one, cv::Point(static_cast<int>(x0), static_cast<int>(y0)),
                      cv::Point(static_cast<int>(x1), static_cast<int>(y1)), cv::Scalar(0, 0, 0),
                      cv::FILLED);
        const auto partial = detector.detect(one, 0.3f);
        char one_leg[64] = "no person";
        char shift[32] = "-";
        if (!partial.empty()) {
            const auto& q = partial.front().kpts;
            std::snprintf(one_leg, sizeof one_leg, "%.2f/%.2f", q[lk].score, q[la].score);
            std::snprintf(shift, sizeof shift, "%.0f",
                          std::hypot(q[lk].x - k[lk].x, q[lk].y - k[lk].y));
        }
        std::cout << fixture << ',' << occlusion::to_string(r0.mode) << ','
                  << occlusion::to_string(r1.mode) << " L" << occlusion::to_string(r1.left.state)
                  << " R" << occlusion::to_string(r1.right.state) << ','
                  << occlusion::to_string(r2.mode) << ',' << one_leg << ',' << shift << '\n';
    }
    if (people == 0) {
        std::cout << "  FAIL no person fixtures in " << manifest << '\n';
        ++failures;
    }
    std::cout << (failures == 0 ? "OCCLUSION REPLAY PASS" : "OCCLUSION REPLAY FAIL") << " ("
              << people << " person fixtures)\n";
    return failures == 0 ? 0 : 1;
}
