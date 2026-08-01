#include <iostream>
#include <fstream>
#include <chrono>
#include <opencv2/opencv.hpp>
#include "pose_detector.h"
#include "angle_calc.h"

int main(int argc, char* argv[]) {
    std::string model_path = "models/yolov8n-pose.onnx";
    int cam_index = 0;
    if (argc > 1) cam_index = std::stoi(argv[1]);

    PoseDetector detector(model_path);

    cv::VideoCapture cap(cam_index);
    if (!cap.isOpened()) {
        std::cerr << "Cannot open camera index " << cam_index << "\n";
        return 1;
    }
    cap.set(cv::CAP_PROP_FRAME_WIDTH,  1280);
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, 720);
    std::cout << "Camera opened\n";
    std::cout << "Controls: [Q] Quit  [C] Recalibrate\n";
    std::cout << ">> Stand straight and wait for calibration...\n";

    std::ofstream csv("gait_data.csv");
    csv << "ts_ms,l_knee,r_knee,l_hip,r_hip,symmetry,squat_phase,"
        << "l_compress,r_compress\n";

    cv::Mat frame;
    auto t0 = std::chrono::steady_clock::now();

    // ── Calibration state ─────────────────────────────────────
    bool  calibrated      = false;
    int   calib_countdown = 90;   // รอ 90 frame (~3 วิ) ก่อน calibrate
    int   calib_frames    = 0;    // frame ที่นับได้ขณะ calibrating
    float sum_l = 0.f, sum_r = 0.f;
    int   csv_save_counter = 0;

    // ── External calibration reset flag ──────────────────────
    extern float ref_l, ref_r;   // defined in angle_calc.cpp

    while (true) {
        if (!cap.read(frame) || frame.empty()) break;

        auto t1   = std::chrono::steady_clock::now();
        auto dets = detector.detect(frame, 0.3f);
        float infer_ms = std::chrono::duration<float, std::milli>(
                         std::chrono::steady_clock::now() - t1).count();

        // ── Calibration overlay ───────────────────────────────
        if (!calibrated) {
            // Countdown bar
            int bar_w = (int)((float)(90 - calib_countdown) / 90.f
                              * (frame.cols - 40));
            cv::rectangle(frame, {20, frame.rows - 30},
                          {20 + bar_w, frame.rows - 10},
                          {0, 200, 255}, -1);
            cv::rectangle(frame, {20, frame.rows - 30},
                          {frame.cols - 20, frame.rows - 10},
                          {100, 100, 100}, 1);

            std::string msg = "CALIBRATING — Stand straight ("
                + std::to_string(calib_countdown) + " frames left)";
            cv::putText(frame, msg, {20, frame.rows - 40},
                cv::FONT_HERSHEY_SIMPLEX, 0.65, {0, 200, 255}, 2);

            if (!dets.empty()) {
                auto& lm = dets[0].kpts;
                bool both_ok = lm[11].score > 0.5f && lm[15].score > 0.5f
                            && lm[12].score > 0.5f && lm[16].score > 0.5f;
                if (both_ok) {
                    calib_countdown--;
                    sum_l += std::abs(lm[15].y - lm[11].y);
                    sum_r += std::abs(lm[16].y - lm[12].y);
                    calib_frames++;
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
            int key = cv::waitKey(1);
            if (key == 'q' || key == 'Q') break;
            continue;   // skip detection display until calibrated
        }

        // ── Calibration done badge ────────────────────────────
        cv::putText(frame, "CALIBRATED", {frame.cols - 160, 25},
            cv::FONT_HERSHEY_SIMPLEX, 0.6, {0, 255, 0}, 2);

        // ── Detection & display ───────────────────────────────
        if (!dets.empty()) {
            auto& lm = dets[0].kpts;
            GaitResult g = compute_gait(lm);

            float ts = std::chrono::duration<float, std::milli>(
                       std::chrono::steady_clock::now() - t0).count();

            // CSV: write every frame, flush every 30 frames
            csv << ts << ","
                << g.l_knee_angle    << "," << g.r_knee_angle << ","
                << g.l_hip_angle     << "," << g.r_hip_angle  << ","
                << g.symmetry_index  << ","
                << g.squat_phase     << ","
                << g.l_leg_compression << "," << g.r_leg_compression << "\n";

            csv_save_counter++;
            if (csv_save_counter % 30 == 0) {
                csv.flush();
                // Show "Saved" badge briefly
                cv::putText(frame, "CSV saved", {frame.cols - 160, 50},
                    cv::FONT_HERSHEY_SIMPLEX, 0.55, {0, 255, 100}, 1);
            }

            // Draw keypoints (lower body only)
            for (int id : {11, 12, 13, 14, 15, 16})
                if (lm[id].score > 0.3f)
                    cv::circle(frame,
                        {(int)lm[id].x, (int)lm[id].y},
                        7, {0, 255, 0}, -1);

            // Draw skeleton
            auto line = [&](int a, int b) {
                if (lm[a].score > 0.3f && lm[b].score > 0.3f)
                    cv::line(frame,
                        {(int)lm[a].x, (int)lm[a].y},
                        {(int)lm[b].x, (int)lm[b].y},
                        {0, 200, 255}, 2);
            };
            line(11,12); line(11,13); line(13,15);
            line(12,14); line(14,16);

            // txt helper
            auto txt = [&](const std::string& s, int y,
                           cv::Scalar c = {0, 255, 0}) {
                cv::putText(frame, s, {20, y},
                    cv::FONT_HERSHEY_SIMPLEX, 0.7, c, 2);
            };

            // Overlay metrics
            txt("L Knee: " + (g.l_knee_angle >= 0
                ? std::to_string((int)g.l_knee_angle) + " deg" : "N/A"), 40);
            txt("R Knee: " + (g.r_knee_angle >= 0
                ? std::to_string((int)g.r_knee_angle) + " deg" : "N/A"), 70);
            txt("Phase: " + g.squat_phase, 100,
                g.squat_phase == "STANDING"   ? cv::Scalar{0,255,0}   :
                g.squat_phase == "SEMI_SQUAT" ? cv::Scalar{0,200,255} :
                                                cv::Scalar{0,100,255});
            txt("L compress: "
                + std::to_string((int)(g.l_leg_compression*100)) + "%", 130);
            txt("R compress: "
                + std::to_string((int)(g.r_leg_compression*100)) + "%", 160);
            txt("Symmetry: " + (g.symmetry_index >= 0
                ? std::to_string((int)g.symmetry_index) + "%" : "N/A"), 190,
                g.symmetry_index < 10.f
                    ? cv::Scalar{0,255,0} : cv::Scalar{0,100,255});
            txt("Infer: " + std::to_string((int)infer_ms) + " ms", 220,
                {180, 180, 180});

            // Hip warning
            if (lm[11].score < 0.5f || lm[12].score < 0.5f)
                cv::putText(frame,
                    "WARNING: Hip not visible",
                    {20, frame.rows - 50},
                    cv::FONT_HERSHEY_SIMPLEX, 0.65, {0,0,255}, 2);

        } else {
            cv::putText(frame, "No person detected", {20, 40},
                cv::FONT_HERSHEY_SIMPLEX, 0.8, {0, 0, 255}, 2);
        }

        // ── Controls hint ─────────────────────────────────────
        cv::putText(frame, "[Q] Quit  [C] Recalibrate",
            {20, frame.rows - 15},
            cv::FONT_HERSHEY_SIMPLEX, 0.55, {200, 200, 200}, 1);

        cv::imshow("Gait Detection [C++]", frame);

        int key = cv::waitKey(1);
        if (key == 'q' || key == 'Q') {
            std::cout << ">> [Q] pressed — saving and quitting...\n";
            break;
        }
        if (key == 'c' || key == 'C') {
            // Reset calibration
            calibrated      = false;
            calib_countdown = 90;
            calib_frames    = 0;
            sum_l = sum_r   = 0.f;
            ref_l = ref_r   = -1.f;
            std::cout << ">> [C] Recalibrating — stand straight...\n";
        }
    }

    cap.release();
    cv::destroyAllWindows();
    csv.flush();
    csv.close();
    std::cout << ">> Session ended. Data saved to: gait_data.csv\n";
    return 0;
}