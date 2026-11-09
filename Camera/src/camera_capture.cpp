#include <opencv2/videoio.hpp>
#include <opencv2/imgcodecs.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

struct Options {
    int device = 0;
    int width = 1280;
    int height = 720;
    int fps = 30;
    int seconds_per_stage = 8;
    fs::path output;
    bool check_only = false;
};

void print_usage(const char* program) {
    std::cout << "Usage: " << program
              << " --output DIR [--device INDEX] [--width PX] [--height PX]"
                 " [--fps FPS] [--seconds N] [--check]\n"
              << "Capture sequence: standing, walking, then no_person.\n"
              << "--check opens the camera and reads one frame without saving.\n";
}

bool parse_positive_int(const std::string& value, int& result) {
    try {
        std::size_t used = 0;
        const int parsed = std::stoi(value, &used);
        if (used != value.size() || parsed <= 0) return false;
        result = parsed;
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool parse_options(int argc, char* argv[], Options& options) {
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--help" || argument == "-h") {
            print_usage(argv[0]);
            return false;
        }
        if (argument == "--check") {
            options.check_only = true;
            continue;
        }
        if (i + 1 >= argc) {
            std::cerr << "Missing value for " << argument << "\n";
            return false;
        }
        const std::string value = argv[++i];
        if (argument == "--output") {
            options.output = value;
        } else if (argument == "--device") {
            if (!parse_positive_int(value, options.device) && value != "0") {
                std::cerr << "Invalid camera device index: " << value << "\n";
                return false;
            }
            if (value == "0") options.device = 0;
        } else if (argument == "--width") {
            if (!parse_positive_int(value, options.width)) return false;
        } else if (argument == "--height") {
            if (!parse_positive_int(value, options.height)) return false;
        } else if (argument == "--fps") {
            if (!parse_positive_int(value, options.fps)) return false;
        } else if (argument == "--seconds") {
            if (!parse_positive_int(value, options.seconds_per_stage)) return false;
        } else {
            std::cerr << "Unknown option: " << argument << "\n";
            return false;
        }
    }
    if (!options.check_only && options.output.empty()) {
        std::cerr << "--output DIR is required unless --check is used.\n";
        return false;
    }
    return true;
}

std::string utc_timestamp() {
    const auto now = std::chrono::system_clock::now();
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
    gmtime_r(&time, &utc);
    std::ostringstream result;
    result << std::put_time(&utc, "%Y-%m-%dT%H:%M:%S") << '.'
           << std::setfill('0') << std::setw(3) << milliseconds.count() << 'Z';
    return result.str();
}

void report_camera(const cv::VideoCapture& camera, const cv::Mat& frame,
                   const Options& options) {
    std::cout << "Camera opened (device index " << options.device << ")\n"
              << "Requested: " << options.width << 'x' << options.height
              << " @ " << options.fps << " fps\n"
              << "Reported:  " << camera.get(cv::CAP_PROP_FRAME_WIDTH) << 'x'
              << camera.get(cv::CAP_PROP_FRAME_HEIGHT) << " @ "
              << camera.get(cv::CAP_PROP_FPS) << " fps\n"
              << "First frame: " << frame.cols << 'x' << frame.rows << "\n"
              << "Exposure property: " << camera.get(cv::CAP_PROP_EXPOSURE)
              << " (backend-dependent; record observed lighting/exposure separately)\n";
    if (frame.cols != options.width || frame.rows != options.height) {
        std::cout << "LIMITATION: requested resolution was not negotiated; actual frame is "
                  << frame.cols << 'x' << frame.rows << ".\n";
    }
    const double reported_fps = camera.get(cv::CAP_PROP_FPS);
    if (reported_fps > 0.0 && std::abs(reported_fps - options.fps) > 0.5) {
        std::cout << "LIMITATION: requested " << options.fps
                  << " fps, backend reports " << reported_fps << " fps.\n";
    }
}

struct StageResult {
    long long frames = 0;
    long long estimated_drops = 0;
    double observed_fps = 0.0;
};

StageResult capture_stage(cv::VideoCapture& camera, std::ofstream& metadata,
                          const std::string& phase, const fs::path& output,
                          const Options& options, long long& global_frame_index,
                          const Clock::time_point& sequence_start) {
    StageResult result;
    const fs::path stage_dir = output / phase;
    fs::create_directories(stage_dir);

    std::cout << "Press Enter to record " << phase << " for "
              << options.seconds_per_stage << " seconds..." << std::flush;
    std::string line;
    std::getline(std::cin, line);

    const auto start = Clock::now();
    auto previous_frame_time = start;
    auto first_frame_time = start;
    auto last_frame_time = start;
    bool have_previous = false;
    cv::Mat frame;
    const double expected_period_ms = 1000.0 / options.fps;

    while (std::chrono::duration<double>(Clock::now() - start).count()
           < options.seconds_per_stage) {
        const auto read_start = Clock::now();
        if (!camera.read(frame) || frame.empty()) {
            throw std::runtime_error("Frame read failed during phase '" + phase +
                                     "' after " + std::to_string(result.frames) +
                                     " frames.");
        }
        const auto frame_time = Clock::now();
        if (have_previous && frame_time <= previous_frame_time) {
            throw std::runtime_error("Non-monotonic frame timestamp during phase '" +
                                     phase + "'.");
        }
        if (!have_previous) first_frame_time = frame_time;

        long long estimated_drops = 0;
        if (have_previous) {
            const double gap_ms = std::chrono::duration<double, std::milli>(
                frame_time - previous_frame_time).count();
            if (gap_ms > expected_period_ms * 1.5) {
                estimated_drops = std::max<long long>(
                    0, static_cast<long long>(std::llround(gap_ms / expected_period_ms)) - 1);
            }
        }

        ++global_frame_index;
        ++result.frames;
        result.estimated_drops += estimated_drops;
        const fs::path image_path = stage_dir /
            ("frame_" + std::to_string(global_frame_index) + ".png");
        if (!cv::imwrite(image_path.string(), frame)) {
            throw std::runtime_error("Could not write frame: " + image_path.string());
        }

        const double elapsed_ms = std::chrono::duration<double, std::milli>(
            frame_time - sequence_start).count();
        const double read_ms = std::chrono::duration<double, std::milli>(
            frame_time - read_start).count();
        metadata << global_frame_index << ',' << phase << ',' << utc_timestamp() << ','
                 << std::fixed << std::setprecision(3) << elapsed_ms << ','
                 << frame.cols << ',' << frame.rows << ',' << read_ms << ','
                 << estimated_drops << ',' << image_path.generic_string() << '\n';
        metadata.flush();
        if (!metadata) throw std::runtime_error("Failed writing frames.csv.");

        previous_frame_time = frame_time;
        last_frame_time = frame_time;
        have_previous = true;
    }

    if (result.frames > 1) {
        result.observed_fps = (result.frames - 1) /
            std::chrono::duration<double>(last_frame_time - first_frame_time).count();
    }
    return result;
}

int main(int argc, char* argv[]) {
    if (argc == 2 && (std::string(argv[1]) == "--help"
                      || std::string(argv[1]) == "-h")) {
        print_usage(argv[0]);
        return 0;
    }
    Options options;
    if (!parse_options(argc, argv, options)) return 2;

    cv::VideoCapture camera(options.device, cv::CAP_ANY);
    if (!camera.isOpened()) {
        std::cerr << "ERROR: Could not open camera device index " << options.device
                  << ". Check the connection, device index, and OS camera permission.\n";
        return 1;
    }
    camera.set(cv::CAP_PROP_FRAME_WIDTH, options.width);
    camera.set(cv::CAP_PROP_FRAME_HEIGHT, options.height);
    camera.set(cv::CAP_PROP_FPS, options.fps);

    cv::Mat initial_frame;
    if (!camera.read(initial_frame) || initial_frame.empty()) {
        std::cerr << "ERROR: Camera opened but failed to deliver the first frame.\n";
        return 1;
    }
    report_camera(camera, initial_frame, options);
    if (options.check_only) return 0;

    if (fs::exists(options.output)) {
        std::cerr << "ERROR: Output path already exists; refusing to overwrite originals: "
                  << options.output << "\n";
        return 1;
    }
    try {
        fs::create_directories(options.output);
    } catch (const fs::filesystem_error& error) {
        std::cerr << "ERROR: Cannot create output directory: " << error.what() << "\n";
        return 1;
    }

    std::ofstream metadata(options.output / "frames.csv");
    if (!metadata) {
        std::cerr << "ERROR: Cannot create frames.csv in " << options.output << "\n";
        return 1;
    }
    metadata << "frame_index,phase,utc_timestamp,elapsed_monotonic_ms,width,height,"
                "read_ms,estimated_drops_since_previous,image_path\n";

    const std::array<std::string, 3> phases = {"standing", "walking", "no_person"};
    long long global_frame_index = 0;
    const auto sequence_start = Clock::now();
    long long total_frames = 0;
    long long total_estimated_drops = 0;
    try {
        for (const auto& phase : phases) {
            const StageResult result = capture_stage(
                camera, metadata, phase, options.output, options,
                global_frame_index, sequence_start);
            total_frames += result.frames;
            total_estimated_drops += result.estimated_drops;
            std::cout << "\n" << phase << ": " << result.frames << " frames, "
                      << std::fixed << std::setprecision(2) << result.observed_fps
                      << " observed fps, " << result.estimated_drops
                      << " estimated dropped frames.\n";
            if (result.observed_fps > 0.0
                && result.observed_fps + 0.5 < options.fps) {
                std::cout << "LIMITATION: " << phase << " saved-frame throughput was "
                          << result.observed_fps << " fps; requested "
                          << options.fps << " fps.\n";
            }
        }
    } catch (const std::exception& error) {
        std::cerr << "ERROR: " << error.what() << "\n"
                  << "Partial capture and frames.csv were preserved at "
                  << options.output << "\n";
        return 1;
    }

    std::cout << "Capture complete: " << total_frames << " frames; "
              << total_estimated_drops << " estimated dropped frames.\n"
              << "Original PNG frames and frames.csv: " << options.output << "\n";
    return 0;
}