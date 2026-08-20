#include <iostream>
#include <fstream>
#include <sstream>
#include <chrono>
#include <csignal>
#include <opencv2/opencv.hpp>
#include "pose_detector.h"
#include "angle_calc.h"

volatile sig_atomic_t g_running = 1;
void signal_handler(int) { g_running = 0; }

// ── Validation test result ────────────────────────────────────
struct TestResult {
    std::string name;
    float l_knee, r_knee, symmetry;
    std::string phase;
    bool passed;
    std::string note;
};

// ── Check if test passes ──────────────────────────────────────
TestResult evaluate_test(
    const std::string& name,
    const GaitResult& g,
    float knee_min, float knee_max,
    float sym_threshold,      // < threshold = pass (Test A/B/C)
                              // > threshold = pass (Test D)
    bool sym_greater,         // true = symmetry must be > threshold
    const std::string& expected_phase)
{
    TestResult t;
    t.name    = name;
    t.l_knee  = g.l_knee_angle;
    t.r_knee  = g.r_knee_angle;
    t.symmetry = g.symmetry_index;
    t.phase   = g.squat_phase;

    bool knee_ok = (g.l_knee_angle >= knee_min && g.l_knee_angle <= knee_max)
                || (g.r_knee_angle >= knee_min && g.r_knee_angle <= knee_max);
    bool sym_ok  = sym_greater
                 ? (g.symmetry_index > sym_threshold)
                 : (g.symmetry_index >= 0 && g.symmetry_index < sym_threshold);
    bool phase_ok = (g.squat_phase == expected_phase);

    t.passed = knee_ok && sym_ok && phase_ok;
    t.note   = "";
    if (!knee_ok)  t.note += "knee_out_of_range ";
    if (!sym_ok)   t.note += "symmetry_fail ";
    if (!phase_ok) t.note += "phase_mismatch ";
    if (t.note.empty()) t.note = "OK";
    return t;
}

int main(int argc, char* argv[]) {
    std::signal(SIGINT,  signal_handler);
    std::signal(SIGTERM, signal_handler);

    std::string model_path = "models/yolov8n-pose.onnx";
    int cam_index = (argc > 1) ? std::stoi(argv[1]) : 0;

    PoseDetector detector(model_path);

    cv::VideoCapture cap(cam_index);
    if (!cap.isOpened()) {
        std::cerr << "Cannot open camera index " << cam_index << "\n";
        return 1;
    }
    cap.set(cv::CAP_PROP_FRAME_WIDTH,  1280);
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, 720);
    cap.set(cv::CAP_PROP_FPS, 30);

    std::cout << "Camera opened\n";
    std::cout << "Controls:\n";
    std::cout << "  [Q] Quit and save\n";
    std::cout << "  [C] Recalibrate\n";
    std::cout << "  [A] Log Test A (Standing straight)\n";
    std::cout << "  [B] Log Test B (Semi-squat)\n";
    std::cout << "  [E] Log Test C (Deep squat)\n";  // C taken by recalib
    std::cout << "  [D] Log Test D (One leg raised)\n";
    std::cout << "  [R] Print validation report\n";
    std::cout << ">> Stand straight and wait for calibration...\n";

    // CSV data logger
    std::ofstream csv("gait_data.csv");
    csv << "ts_ms,l_knee,r_knee,l_hip,r_hip,symmetry,"
        << "l_phase,r_phase,squat_phase,l_compress,r_compress,infer_ms\n";

    // Validation report logger
    std::ofstream report("validation_report.txt");
    report << "=== Camera Placement Validation Report ===\n";
    report << "Camera spec: height 90-100 cm, distance 2-3 m, front view\n\n";

    std::vector<TestResult> test_results;

    cv::Mat frame;
    auto t0 = std::chrono::steady_clock::now();

    // Calibration state
    bool  calibrated      = false;
    int   calib_countdown = 90;
    int   calib_frames    = 0;
    float sum_l = 0.f, sum_r = 0.f;
    int   csv_counter = 0;

    extern float ref_l, ref_r;

    // Current gait result (shared between loop iterations)
    GaitResult current_g;

    while (g_running) {
        if (!cap.read(frame) || frame.empty()) break;

        auto t1      = std::chrono::steady_clock::now();
        auto dets    = detector.detect(frame, 0.3f);
        float inf_ms = std::chrono::duration<float, std::milli>(
                       std::chrono::steady_clock::now() - t1).count();

        // ── Calibration phase ─────────────────────────────────
        if (!calibrated) {
            int bar_w = (int)((float)(90 - calib_countdown) / 90.f
                              * (frame.cols - 40));
            cv::rectangle(frame, {20, frame.rows-30},
                          {20+bar_w, frame.rows-10}, {0,200,255}, -1);
            cv::rectangle(frame, {20, frame.rows-30},
                          {frame.cols-20, frame.rows-10}, {100,100,100}, 1);
            cv::putText(frame,
                "CALIBRATING — Stand straight ("
                + std::to_string(calib_countdown) + " frames left)",
                {20, frame.rows-40},
                cv::FONT_HERSHEY_SIMPLEX, 0.65, {0,200,255}, 2);

            if (!dets.empty()) {
                auto& lm = dets[0].kpts;
                bool ok = lm[11].score > 0.5f && lm[15].score > 0.5f
                       && lm[12].score > 0.5f && lm[16].score > 0.5f;
                if (ok) {
                    sum_l += std::abs(lm[15].y - lm[11].y);
                    sum_r += std::abs(lm[16].y - lm[12].y);
                    calib_frames++;
                    calib_countdown--;
                }
                if (calib_countdown <= 0 && calib_frames > 0) {
                    ref_l = sum_l / calib_frames;
                    ref_r = sum_r / calib_frames;
                    calibrated = true;
                    std::cout << ">> Calibration complete! "
                              << "ref_l=" << ref_l
                              << " ref_r=" << ref_r << "\n";
                }
            }

            cv::imshow("Gait Detection [C++]", frame);
            if (cv::waitKey(1) == 'q') break;
            continue;
        }

        // ── Detection phase ───────────────────────────────────
        cv::putText(frame, "CALIBRATED",
            {frame.cols-160, 25},
            cv::FONT_HERSHEY_SIMPLEX, 0.6, {0,255,0}, 2);

        if (!dets.empty()) {
            auto& lm = dets[0].kpts;
            current_g = compute_gait(lm);

            float ts = std::chrono::duration<float, std::milli>(
                       std::chrono::steady_clock::now() - t0).count();

            // Log CSV
            csv << ts                          << ","
                << current_g.l_knee_angle      << ","
                << current_g.r_knee_angle      << ","
                << current_g.l_hip_angle       << ","
                << current_g.r_hip_angle       << ","
                << current_g.symmetry_index    << ","
                << current_g.l_phase           << ","
                << current_g.r_phase           << ","
                << current_g.squat_phase       << ","
                << current_g.l_leg_compression << ","
                << current_g.r_leg_compression << ","
                << inf_ms                      << "\n";

            if (++csv_counter % 30 == 0) {
                csv.flush();
                cv::putText(frame, "CSV saved",
                    {frame.cols-160, 50},
                    cv::FONT_HERSHEY_SIMPLEX, 0.55, {0,255,100}, 1);
            }

            // Draw keypoints
            for (int id : {11,12,13,14,15,16})
                if (lm[id].score > 0.3f)
                    cv::circle(frame,
                        {(int)lm[id].x, (int)lm[id].y},
                        7, {0,255,0}, -1);

            // Draw skeleton
            auto line = [&](int a, int b) {
                if (lm[a].score > 0.3f && lm[b].score > 0.3f)
                    cv::line(frame,
                        {(int)lm[a].x, (int)lm[a].y},
                        {(int)lm[b].x, (int)lm[b].y},
                        {0,200,255}, 2);
            };
            line(11,12); line(11,13); line(13,15);
            line(12,14); line(14,16);

            // Overlay metrics
            auto txt = [&](const std::string& s, int y,
                           cv::Scalar c = {0,255,0}) {
                cv::putText(frame, s, {20,y},
                    cv::FONT_HERSHEY_SIMPLEX, 0.7, c, 2);
            };
            txt("L Knee: " + (current_g.l_knee_angle >= 0
                ? std::to_string((int)current_g.l_knee_angle)+" deg"
                : "N/A"), 40);
            txt("R Knee: " + (current_g.r_knee_angle >= 0
                ? std::to_string((int)current_g.r_knee_angle)+" deg"
                : "N/A"), 70);
            txt("Phase: " + current_g.squat_phase, 100,
                current_g.squat_phase == "STANDING"   ? cv::Scalar{0,255,0}   :
                current_g.squat_phase == "SEMI_SQUAT" ? cv::Scalar{0,200,255} :
                                                        cv::Scalar{0,100,255});
            txt("L compress: "
                + std::to_string((int)(current_g.l_leg_compression*100))+"%", 130);
            txt("R compress: "
                + std::to_string((int)(current_g.r_leg_compression*100))+"%", 160);
            txt("Symmetry: " + (current_g.symmetry_index >= 0
                ? std::to_string((int)current_g.symmetry_index)+"%"
                : "N/A"), 190,
                current_g.symmetry_index < 10.f
                    ? cv::Scalar{0,255,0} : cv::Scalar{0,100,255});
            txt("Infer: "+std::to_string((int)inf_ms)+" ms", 220, {180,180,180});

            if (lm[11].score < 0.5f || lm[12].score < 0.5f)
                cv::putText(frame, "WARNING: Hip not visible",
                    {20, frame.rows-50},
                    cv::FONT_HERSHEY_SIMPLEX, 0.65, {0,0,255}, 2);

        } else {
            cv::putText(frame, "No person detected", {20,40},
                cv::FONT_HERSHEY_SIMPLEX, 0.8, {0,0,255}, 2);
        }

        // Show logged tests count
        if (!test_results.empty()) {
            cv::putText(frame,
                "Tests logged: " + std::to_string(test_results.size()) + "/4",
                {20, frame.rows-50},
                cv::FONT_HERSHEY_SIMPLEX, 0.55, {255,200,0}, 1);
        }

        cv::putText(frame,
            "[A]TestA [B]TestB [E]TestC [D]TestD [R]Report [C]Recalib [Q]Quit",
            {20, frame.rows-15},
            cv::FONT_HERSHEY_SIMPLEX, 0.45, {200,200,200}, 1);

        cv::imshow("Gait Detection [C++]", frame);

        // ── Key handling ──────────────────────────────────────
        int key = cv::waitKey(1);

        if (key == 'q' || key == 'Q') {
            std::cout << ">> [Q] Quitting...\n";
            break;
        }

        if (key == 'c' || key == 'C') {
            calibrated = false; calib_countdown = 90;
            calib_frames = 0;   sum_l = sum_r = 0.f;
            ref_l = ref_r = -1.f;
            std::cout << ">> [C] Recalibrating...\n";
        }

        // ── Test A: Standing straight ─────────────────────────
        if (key == 'a' || key == 'A') {
            auto t = evaluate_test(
                "Test A — Standing Straight",
                current_g,
                170.f, 185.f,   // knee range
                5.f,            // symmetry < 5%
                false,          // sym_greater = false
                "STANDING");
            test_results.push_back(t);
            std::cout << (t.passed ? "✅" : "❌")
                      << " Test A: L=" << (int)t.l_knee
                      << " R=" << (int)t.r_knee
                      << " Sym=" << (int)t.symmetry
                      << "% Phase=" << t.phase
                      << " [" << t.note << "]\n";

            report << "Test A — Standing Straight\n";
            report << "  L Knee: " << t.l_knee << " deg (expect 170-185)\n";
            report << "  R Knee: " << t.r_knee << " deg (expect 170-185)\n";
            report << "  Symmetry: " << t.symmetry << "% (expect <5%)\n";
            report << "  Phase: " << t.phase << " (expect STANDING)\n";
            report << "  Result: " << (t.passed ? "PASS" : "FAIL")
                   << " [" << t.note << "]\n\n";
        }

        // ── Test B: Semi-squat ────────────────────────────────
        if (key == 'b' || key == 'B') {
            auto t = evaluate_test(
                "Test B — Semi-squat",
                current_g,
                130.f, 165.f,
                15.f,
                false,
                "SEMI_SQUAT");
            test_results.push_back(t);
            std::cout << (t.passed ? "✅" : "❌")
                      << " Test B: L=" << (int)t.l_knee
                      << " R=" << (int)t.r_knee
                      << " Sym=" << (int)t.symmetry
                      << "% Phase=" << t.phase
                      << " [" << t.note << "]\n";

            report << "Test B — Semi-squat (~30 deg)\n";
            report << "  L Knee: " << t.l_knee << " deg (expect 130-165)\n";
            report << "  R Knee: " << t.r_knee << " deg (expect 130-165)\n";
            report << "  Symmetry: " << t.symmetry << "% (expect <15%)\n";
            report << "  Phase: " << t.phase << " (expect SEMI_SQUAT)\n";
            report << "  Result: " << (t.passed ? "PASS" : "FAIL")
                   << " [" << t.note << "]\n\n";
        }

        // ── Test C: Deep squat (key E to avoid conflict with C=Recalib)
        if (key == 'e' || key == 'E') {
            auto t = evaluate_test(
                "Test C — Deep Squat",
                current_g,
                70.f, 110.f,
                15.f,
                false,
                "DEEP_SQUAT");
            test_results.push_back(t);
            std::cout << (t.passed ? "✅" : "❌")
                      << " Test C: L=" << (int)t.l_knee
                      << " R=" << (int)t.r_knee
                      << " Sym=" << (int)t.symmetry
                      << "% Phase=" << t.phase
                      << " [" << t.note << "]\n";

            report << "Test C — Deep Squat (~90 deg)\n";
            report << "  L Knee: " << t.l_knee << " deg (expect 70-110)\n";
            report << "  R Knee: " << t.r_knee << " deg (expect 70-110)\n";
            report << "  Symmetry: " << t.symmetry << "% (expect <15%)\n";
            report << "  Phase: " << t.phase << " (expect DEEP_SQUAT)\n";
            report << "  Result: " << (t.passed ? "PASS" : "FAIL")
                   << " [" << t.note << "]\n\n";
        }

        // ── Test D: One leg raised ────────────────────────────
        if (key == 'd' || key == 'D') {
            // For one-leg-raised: symmetry must be > 30%
            // phase check is relaxed (any phase ok)
            TestResult t;
            t.name     = "Test D — One Leg Raised";
            t.l_knee   = current_g.l_knee_angle;
            t.r_knee   = current_g.r_knee_angle;
            t.symmetry = current_g.symmetry_index;
            t.phase    = current_g.squat_phase;
            t.passed   = (current_g.symmetry_index > 30.f);
            t.note     = t.passed ? "asymmetry_detected" : "symmetry_too_low";
            test_results.push_back(t);

            std::cout << (t.passed ? "✅" : "❌")
                      << " Test D: Sym=" << (int)t.symmetry
                      << "% (need >30%) [" << t.note << "]\n";

            report << "Test D — One Leg Raised\n";
            report << "  L Knee: " << t.l_knee << " deg\n";
            report << "  R Knee: " << t.r_knee << " deg\n";
            report << "  Symmetry: " << t.symmetry << "% (expect >30%)\n";
            report << "  Result: " << (t.passed ? "PASS" : "FAIL")
                   << " [" << t.note << "]\n\n";
        }

        // ── Print summary report ──────────────────────────────
        if (key == 'r' || key == 'R') {
            std::cout << "\n=== Validation Report ===\n";
            int pass = 0;
            for (auto& t : test_results) {
                std::cout << (t.passed ? "✅ PASS" : "❌ FAIL")
                          << " " << t.name
                          << " | Sym=" << (int)t.symmetry
                          << "% Phase=" << t.phase << "\n";
                if (t.passed) pass++;
            }
            std::cout << "Overall: " << pass << "/"
                      << test_results.size() << " passed\n\n";

            report << "=== Summary ===\n";
            report << "Total: " << pass << "/"
                   << test_results.size() << " passed\n";
            report << "Camera spec: height 90-100 cm, "
                   << "distance 2-3 m, front view\n";
            report.flush();
            std::cout << ">> Report saved to validation_report.txt\n";
        }
    }

    cap.release();
    cv::destroyAllWindows();
    csv.flush();  csv.close();
    report.flush(); report.close();
    std::cout << ">> Session ended.\n";
    std::cout << ">> Data: gait_data.csv\n";
    std::cout << ">> Validation: validation_report.txt\n";
    return 0;
}