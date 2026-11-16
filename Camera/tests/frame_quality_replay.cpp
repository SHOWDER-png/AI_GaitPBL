// Replay command for SCRUM-380: runs the real pose model over the curated fixtures and
// prints the frame-quality verdict for each, then re-checks the same detections as stale.
//
//   ./frame_quality_replay [manifest.csv] [model.onnx]      (run from the Camera/ directory)
//
// Exit code 0 only if the no_person fixture is NO_PERSON, every person fixture is not
// NO_PERSON, and every late (+800 ms) result is STALE_DATA.
#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <opencv2/imgcodecs.hpp>

#include "frame_quality.h"
#include "pose_detector.h"

int main(int argc, char* argv[]) {
    const std::string manifest = argc > 1 ? argv[1] : "fixtures/manifest.csv";
    const std::string model = argc > 2 ? argv[2] : "models/yolov8n-pose.onnx";

    std::ifstream input(manifest);
    if (!input) {
        std::cerr << "ERROR: cannot open manifest " << manifest << "\n";
        return 2;
    }
    PoseDetector detector(model);
    frame_quality::Config config;
    config.hips_required = false;                 // same setting as main.cpp

    std::string line;
    std::getline(input, line);                    // header
    int failures = 0;
    int index = 0;
    std::cout << "fixture,verdict,valid_lower_body,late_verdict,reason\n";
    while (std::getline(input, line)) {
        if (line.empty()) continue;
        const std::string relative = line.substr(0, line.find(','));
        const cv::Mat image = cv::imread("fixtures/" + relative);
        if (image.empty()) {
            std::cerr << "ERROR: cannot read fixtures/" << relative << "\n";
            ++failures;
            continue;
        }
        const auto started = std::chrono::steady_clock::now();
        const auto detections = detector.detect(image, 0.3f);
        const double inference_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started).count();
        const Detection* detection = detections.empty() ? nullptr : &detections[0];

        const double capture_ms = 1000.0 * (++index);
        frame_quality::Tracker fresh(config);
        const auto now = fresh.assess(detection, capture_ms, capture_ms + inference_ms);
        frame_quality::Tracker late(config);
        const auto old = late.assess(detection, capture_ms, capture_ms + 800.0);

        std::cout << relative << ',' << frame_quality::to_string(now.status) << ','
                  << now.valid_lower_body << ',' << frame_quality::to_string(old.status) << ','
                  << (now.reasons.empty() ? "-" : now.reasons.front()) << '\n';

        const bool expect_empty = relative.find("no_person") != std::string::npos;
        if (expect_empty && now.status != frame_quality::Status::NO_PERSON) ++failures;
        if (!expect_empty && now.status == frame_quality::Status::NO_PERSON) ++failures;
        if (old.status != frame_quality::Status::STALE_DATA) ++failures;
    }
    std::cout << (failures == 0 ? "REPLAY PASS" : "REPLAY FAIL") << " (" << index
              << " fixtures)\n";
    return failures == 0 ? 0 : 1;
}
