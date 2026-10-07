#include <iostream>
#include <fstream>
#include <sstream>
#include <chrono>
#include <csignal>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <opencv2/opencv.hpp>
#include "pose_detector.h"
#include "angle_calc.h"
#include "offline_inference.h"
#include "offline_validation.h"

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

namespace {

struct OfflineOptions {
    std::filesystem::path manifest = "fixtures/manifest.csv";
    std::filesystem::path output_prefix = "offline-inference";
    float person_threshold = 0.3f;
    float keypoint_threshold = 0.5f;
};

struct Fixture {
    std::string path;
    std::string phase;
    std::string frame_id;
    std::string source_run;
    int expected_width = 0;
    int expected_height = 0;
};

bool parse_csv_row(const std::string& line, std::vector<std::string>& fields) {
    fields.clear();
    std::string field;
    bool quoted = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char ch = line[i];
        if (quoted) {
            if (ch == '"' && i + 1 < line.size() && line[i + 1] == '"') {
                field += '"';
                ++i;
            } else if (ch == '"') {
                quoted = false;
            } else {
                field += ch;
            }
        } else if (ch == ',' ) {
            fields.push_back(field);
            field.clear();
        } else if (ch == '"' && field.empty()) {
            quoted = true;
        } else {
            field += ch;
        }
    }
    if (quoted) return false;
    fields.push_back(field);
    return true;
}

std::vector<Fixture> read_fixtures(const std::filesystem::path& manifest) {
    std::ifstream input(manifest);
    if (!input) {
        throw std::runtime_error("Cannot open fixture manifest: "
                                 + manifest.string());
    }

    std::string line;
    if (!std::getline(input, line)) {
        throw std::runtime_error("Fixture manifest is empty: "
                                 + manifest.string());
    }
    std::vector<std::string> header;
    if (!parse_csv_row(line, header)) {
        throw std::runtime_error("Invalid CSV header in: " + manifest.string());
    }
    std::unordered_map<std::string, std::size_t> columns;
    for (std::size_t i = 0; i < header.size(); ++i) columns[header[i]] = i;
    for (const char* required_column :
         {"fixture_path", "phase", "source_frame_index"}) {
        const std::string required(required_column);
        if (columns.find(required) == columns.end()) {
            throw std::runtime_error("Fixture manifest lacks required column '"
                                     + required + "'");
        }
    }

    const auto value = [&columns](const std::vector<std::string>& row,
                                  const std::string& name) -> std::string {
        const auto column = columns.find(name);
        if (column == columns.end() || column->second >= row.size()) return {};
        return row[column->second];
    };

    std::vector<Fixture> fixtures;
    std::size_t line_number = 1;
    while (std::getline(input, line)) {
        ++line_number;
        if (line.empty()) continue;
        std::vector<std::string> row;
        if (!parse_csv_row(line, row)) {
            throw std::runtime_error("Invalid CSV quoting at " + manifest.string()
                                     + ":" + std::to_string(line_number));
        }
        Fixture fixture;
        fixture.path = value(row, "fixture_path");
        fixture.phase = value(row, "phase");
        fixture.frame_id = value(row, "source_frame_index");
        fixture.source_run = value(row, "source_run");
        const std::string width = value(row, "width");
        const std::string height = value(row, "height");
        if (!width.empty()) fixture.expected_width = std::stoi(width);
        if (!height.empty()) fixture.expected_height = std::stoi(height);
        if (fixture.path.empty() || fixture.phase.empty()
            || fixture.frame_id.empty()) {
            throw std::runtime_error("Missing fixture path, phase, or frame id at "
                                     + manifest.string() + ":"
                                     + std::to_string(line_number));
        }
        fixtures.push_back(std::move(fixture));
    }
    if (fixtures.empty()) {
        throw std::runtime_error("Fixture manifest has no fixture rows: "
                                 + manifest.string());
    }
    return fixtures;
}

float parse_threshold(const std::string& text, const std::string& option) {
    std::size_t parsed = 0;
    float threshold = 0.0f;
    try {
        threshold = std::stof(text, &parsed);
    } catch (const std::exception&) {
        throw std::runtime_error("Invalid value for " + option + ": " + text);
    }
    if (parsed != text.size() || !std::isfinite(threshold)
        || threshold < 0.0f || threshold > 1.0f) {
        throw std::runtime_error(option + " must be a number from 0 to 1");
    }
    return threshold;
}

OfflineOptions parse_offline_options(int argc, char* argv[]) {
    OfflineOptions options;
    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        if (i + 1 >= argc) {
            throw std::runtime_error("Missing value after " + arg);
        }
        const std::string value = argv[++i];
        if (arg == "--manifest") options.manifest = value;
        else if (arg == "--output") options.output_prefix = value;
        else if (arg == "--threshold") {
            options.person_threshold = parse_threshold(value, arg);
        } else if (arg == "--keypoint-threshold") {
            options.keypoint_threshold = parse_threshold(value, arg);
        } else {
            throw std::runtime_error("Unknown offline option: " + arg);
        }
    }
    return options;
}

void write_error_record(
    std::ostream& output, const Fixture& fixture,
    const std::string& image_path, const std::string& error)
{
    output << "{\"record_type\":\"error\",\"fixture_path\":\""
           << json_escape(fixture.path) << "\",\"image_path\":\""
           << json_escape(image_path) << "\",\"phase\":\""
           << json_escape(fixture.phase) << "\",\"frame_id\":\""
           << json_escape(fixture.frame_id) << "\",\"error\":\""
           << json_escape(error) << "\",\"detection_valid\":null}\n";
}

int run_offline(const OfflineOptions& options, const std::string& model_path) {
    const std::filesystem::path manifest_path =
        std::filesystem::absolute(options.manifest);
    const std::vector<Fixture> fixtures = read_fixtures(manifest_path);
    const std::filesystem::path model_file(model_path);
    if (!std::filesystem::is_regular_file(model_file)) {
        throw std::runtime_error("Model file not found: "
                                 + std::filesystem::absolute(model_file).string());
    }

    const std::filesystem::path raw_path =
        options.output_prefix.string() + ".jsonl";
    const std::filesystem::path summary_path =
        options.output_prefix.string() + "-summary.json";
    if (raw_path.has_parent_path()) {
        std::filesystem::create_directories(raw_path.parent_path());
    }
    if (summary_path.has_parent_path()) {
        std::filesystem::create_directories(summary_path.parent_path());
    }
    std::ofstream raw(raw_path);
    if (!raw) throw std::runtime_error("Cannot write raw results: " + raw_path.string());

    const std::uintmax_t model_bytes = std::filesystem::file_size(model_file);
    raw << std::setprecision(9)
        << "{\"record_type\":\"run\",\"model_path\":\""
        << json_escape(std::filesystem::absolute(model_file).string())
        << "\",\"model_bytes\":" << model_bytes
        << ",\"onnx_runtime_version\":\"" << json_escape(Ort::GetVersionString())
        << "\",\"opencv_version\":\"" << CV_VERSION
        << "\",\"manifest_path\":\"" << json_escape(manifest_path.string())
        << "\",\"person_detection_threshold\":" << options.person_threshold
        << ",\"person_detection_threshold_definition\":\"minimum detector "
           "person confidence; independent of keypoint confidence\","
        << "\"keypoint_confidence_threshold\":" << options.keypoint_threshold
        << ",\"keypoint_threshold_definition\":\"minimum per-keypoint score "
           "for the keypoint valid flag; does not filter detections\","
        << "\"timing_method\":\"steady_clock around PoseDetector::detect only; "
           "image decoding and output writing excluded\","
        << "\"latency_unit\":\"milliseconds\"}\n";

    std::size_t valid_frames = 0;
    std::size_t rejected_frames = 0;
    std::size_t failed_frames = 0;
    std::vector<double> latencies_ms;
    PoseDetector detector(model_path);
    for (const Fixture& fixture : fixtures) {
        const std::filesystem::path image_path =
            manifest_path.parent_path() / fixture.path;
        cv::Mat image = cv::imread(image_path.string(), cv::IMREAD_COLOR);
        if (image.empty()) {
            ++failed_frames;
            const std::string error = "image_read_failed";
            write_error_record(raw, fixture, image_path.string(), error);
            std::cerr << "Failed to read fixture image: " << image_path << '\n';
            continue;
        }

        try {
            const auto start = std::chrono::steady_clock::now();
            const std::vector<Detection> detections =
                detector.detect(image, options.person_threshold);
            const double latency_ms =
                std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - start).count();
            latencies_ms.push_back(latency_ms);
            if (detections.empty()) {
                ++rejected_frames;
                write_offline_frame_json(
                    raw, fixture.path, image_path.string(), fixture.phase,
                    fixture.frame_id, fixture.source_run,
                    options.person_threshold, options.keypoint_threshold,
                    image.cols, image.rows, latency_ms, nullptr);
            } else {
                ++valid_frames;
                write_offline_frame_json(
                    raw, fixture.path, image_path.string(), fixture.phase,
                    fixture.frame_id, fixture.source_run,
                    options.person_threshold, options.keypoint_threshold,
                    image.cols, image.rows, latency_ms, &detections.front());
            }
        } catch (const std::exception& error) {
            ++failed_frames;
            write_error_record(raw, fixture, image_path.string(),
                               std::string("inference_failed: ") + error.what());
            std::cerr << "Inference failed for " << image_path << ": "
                      << error.what() << '\n';
        }
    }
    raw.flush();
    if (!raw) throw std::runtime_error("Failed writing raw results: " + raw_path.string());
    raw.close();

    const auto metrics = offline_validation::calculate_metrics(
        fixtures.size(), valid_frames, rejected_frames, failed_frames,
        std::move(latencies_ms));
    std::ofstream summary(summary_path);
    if (!summary) {
        throw std::runtime_error("Cannot write summary: " + summary_path.string());
    }
    summary << std::setprecision(9)
            << "{\n  \"model_path\": \""
            << json_escape(std::filesystem::absolute(model_file).string())
            << "\",\n  \"model_bytes\": " << model_bytes
            << ",\n  \"onnx_runtime_version\": \""
            << json_escape(Ort::GetVersionString())
            << "\",\n  \"opencv_version\": \"" << CV_VERSION
            << "\",\n  \"manifest_path\": \"" << json_escape(manifest_path.string())
            << "\",\n  \"person_detection_threshold\": "
            << options.person_threshold
            << ",\n  \"person_detection_threshold_definition\": "
               "\"minimum detector person confidence; independent of keypoint confidence\""
            << ",\n  \"keypoint_confidence_threshold\": "
            << options.keypoint_threshold
            << ",\n  \"keypoint_threshold_definition\": "
               "\"minimum per-keypoint score for the keypoint valid flag; does not filter detections\""
            << ",\n  \"total_frames\": " << metrics.total_frames
            << ",\n  \"valid_frames\": " << metrics.valid_frames
            << ",\n  \"rejected_no_person_frames\": " << metrics.rejected_frames
            << ",\n  \"failed_frames\": " << metrics.failed_frames
            << ",\n  \"latency_sample_count\": " << metrics.latency_samples
            << ",\n  \"mean_latency_ms\": " << metrics.mean_latency_ms
            << ",\n  \"p95_latency_ms\": " << metrics.p95_latency_ms
            << ",\n  \"p95_method\": \"nearest-rank percentile over successful "
               "inference calls\"\n"
            << ",\n  \"fps\": " << metrics.fps
            << ",\n  \"fps_definition\": \"successful inference calls divided by "
               "the sum of their inference latencies in seconds\"\n"
            << ",\n  \"timing_method\": \"steady_clock around PoseDetector::detect "
               "only; image decoding and output writing excluded\"\n"
            << ",\n  \"latency_unit\": \"milliseconds\""
            << ",\n  \"p95_caveat\": \"Only " << metrics.latency_samples
            << " latency samples; p95 is statistically less reliable for a small "
               "fixture set.\"\n}\n";
    summary.flush();
    if (!summary) throw std::runtime_error("Failed writing summary: " + summary_path.string());

    std::cout << "Offline inference complete: " << fixtures.size()
              << " total, " << valid_frames << " valid, " << rejected_frames
              << " no-person, " << failed_frames << " failed\n"
              << "Raw results: " << raw_path << "\n"
              << "Summary: " << summary_path << "\n";
    return failed_frames == 0 ? 0 : 2;
}

void print_usage(const char* program) {
    std::cout << "Usage:\n  " << program
              << " --offline [--manifest fixtures/manifest.csv]"
                 " [--threshold 0.3] [--keypoint-threshold 0.5]"
                 " [--output offline-inference]\n"
              << "  " << program << " [camera-index]\n";
}

}  // namespace

int main(int argc, char* argv[]) {
    std::signal(SIGINT,  signal_handler);
    std::signal(SIGTERM, signal_handler);

    if (argc > 1 && std::string(argv[1]) == "--help") {
        print_usage(argv[0]);
        return 0;
    }
    if (argc > 1 && std::string(argv[1]) == "--offline") {
        try {
            return run_offline(
                parse_offline_options(argc, argv), "models/yolov8n-pose.onnx");
        } catch (const std::exception& error) {
            std::cerr << "Offline inference error: " << error.what() << '\n';
            return 1;
        }
    }

    std::string model_path = "models/yolov8n-pose.onnx";

    PoseDetector detector(model_path);

#if defined(__linux__)
    // FRDM-IMX95: capture through libcamera/NeoISP via GStreamer.
    // Requires LIBCAMERA_PIPELINES_MATCH_LIST=nxp/neo to be set in the
    // environment (see deployment guide) so libcamera selects the
    // ISP-capable pipeline handler instead of the raw imx8-isi handler.
    std::string pipeline =
        "libcamerasrc ! video/x-raw,format=NV12,width=1280,height=720,"
        "framerate=30/1 ! videoconvert ! video/x-raw,format=BGR ! "
        "appsink drop=true max-buffers=1 sync=false";
    cv::VideoCapture cap(pipeline, cv::CAP_GSTREAMER);
#else
    int cam_index = (argc > 1) ? std::stoi(argv[1]) : 0;
    cv::VideoCapture cap(cam_index);
    cap.set(cv::CAP_PROP_FRAME_WIDTH,  1280);
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, 720);
    cap.set(cv::CAP_PROP_FPS, 30);
#endif
    if (!cap.isOpened()) {
        std::cerr << "Cannot open camera\n";
        return 1;
    }

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

            // Virtual hip indicator
            if (current_g.l_hip_virtual || current_g.r_hip_virtual) {
                cv::putText(frame, "HIP: virtual fallback active",
                    {20, frame.rows-70},
                    cv::FONT_HERSHEY_SIMPLEX, 0.55, {255,165,0}, 1);
            }

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