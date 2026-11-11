// Host-side tests for the FRDM-i.MX95 camera pipeline builder (SCRUM-191).
// These run on macOS/Linux without a camera; they guard the constraints measured on the
// board: 1080p/2160p only, YUY2 (not raw R8), no framerate in the libcamerasrc caps, and the
// G2D hardware converter instead of CPU conversion.
#include <iostream>
#include <string>

#include "board_camera.h"

namespace {

int failures = 0;

void check(bool condition, const char* description) {
    if (!condition) {
        std::cerr << "FAIL: " << description << '\n';
        ++failures;
    }
}

bool contains(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

void test_default_pipeline_uses_isp_stream_and_hardware_converter() {
    const std::string p = board_camera::build_pipeline(board_camera::PipelineConfig{});
    check(contains(p, "libcamerasrc ! video/x-raw,format=YUY2,width=1920,height=1080 !"),
          "captures 1920x1080 from the ISP YUY2 stream, not raw R8");
    check(!contains(p, "height=1080,framerate"),
          "framerate is not placed in the libcamerasrc caps (not-negotiated on the board)");
    check(contains(p, "imxvideoconvert_g2d ! video/x-raw,format=BGRx,width=1280,height=720"),
          "scales to 1280x720 with the G2D hardware converter");
    check(!contains(p, "videoscale"), "default pipeline does not use the slow CPU scaler");
    check(contains(p, "videorate ! video/x-raw,framerate=30/1"),
          "frame rate is limited to 30 fps after conversion");
    check(contains(p, "videoconvert ! video/x-raw,format=BGR"),
          "application receives 3-channel BGR frames");
    check(contains(p, "appsink drop=true max-buffers=1 sync=false"),
          "pipeline ends in a non-blocking appsink");
    check(!contains(p, "libcamerasrc ! video/x-raw,format=YUY2,width=1280"),
          "never asks libcamera for 1280x720 directly");
}

void test_cpu_fallback_pipeline() {
    board_camera::PipelineConfig c;
    c.use_g2d = false;
    const std::string p = board_camera::build_pipeline(c);
    check(contains(p, "videoconvert ! videoscale ! video/x-raw,format=BGR,width=1280,height=720"),
          "CPU fallback converts and scales in software");
    check(!contains(p, "imxvideoconvert_g2d"), "CPU fallback does not reference the G2D element");
}

void test_4k_capture_is_allowed() {
    board_camera::PipelineConfig c;
    c.capture_width = 3840;
    c.capture_height = 2160;
    const std::string p = board_camera::build_pipeline(c);
    check(contains(p, "width=3840,height=2160"), "3840x2160 capture is accepted");
}

void test_unsupported_capture_size_is_rejected() {
    board_camera::PipelineConfig c;
    c.capture_width = 1280;
    c.capture_height = 720;
    check(board_camera::build_pipeline(c).empty(),
          "1280x720 capture is rejected because the board cannot negotiate it");
    check(!board_camera::capture_size_supported(0, 0), "0x0 is not a supported size");
}

void test_invalid_output_or_fps_is_rejected() {
    board_camera::PipelineConfig c;
    c.out_width = 0;
    check(board_camera::build_pipeline(c).empty(), "zero output width is rejected");
    c = board_camera::PipelineConfig{};
    c.fps = 0;
    check(board_camera::build_pipeline(c).empty(), "zero fps is rejected");
    c = board_camera::PipelineConfig{};
    c.out_height = -5;
    check(board_camera::build_pipeline(c).empty(), "negative output height is rejected");
}

}  // namespace

int main() {
    test_default_pipeline_uses_isp_stream_and_hardware_converter();
    test_cpu_fallback_pipeline();
    test_4k_capture_is_allowed();
    test_unsupported_capture_size_is_rejected();
    test_invalid_output_or_fps_is_rejected();
    if (failures == 0) std::cout << "board_camera_tests: all passed\n";
    return failures == 0 ? 0 : 1;
}
