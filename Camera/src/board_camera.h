#pragma once
// FRDM-i.MX95 MIPI-CSI camera pipeline (SCRUM-191).
//
// Measured on the board (11 Nov 2026, gst-launch + fpsdisplaysink, 8 s each):
//   libcamerasrc YUY2 1080p -> CPU videoconvert/videoscale -> BGR 720p :  2.5 fps, frame loss
//   libcamerasrc YUY2 1080p -> imxvideoconvert_g2d (HW) -> BGRx 720p   : 60 fps, no loss
//   libcamerasrc YUY2 2160p -> imxvideoconvert_g2d (HW) -> BGRx 720p   : 29.8 fps, no loss
// Rules learned:
//  * libcamera with the NXP NeoISP handler (LIBCAMERA_PIPELINES_MATCH_LIST=nxp/neo) delivers
//    1920x1080 and 3840x2160 only; 1280x720 fails with not-negotiated.
//  * format=YUY2 is required: without it libcamerasrc picks the raw R8 stream.
//  * A framerate inside the libcamerasrc caps fails with not-negotiated; set it after the
//    converter with videorate.
//  * Colour conversion/scaling must use the G2D hardware element; the CPU path is too slow.
#include <string>

namespace board_camera {

struct PipelineConfig {
    int capture_width = 1920;   // sensor/ISP output, must be 1920x1080 or 3840x2160
    int capture_height = 1080;
    int out_width = 1280;       // size delivered to the application
    int out_height = 720;
    int fps = 30;
    bool use_g2d = true;        // false = CPU fallback (about 2.5 fps, for diagnosis only)
};

inline bool capture_size_supported(int width, int height) {
    return (width == 1920 && height == 1080) || (width == 3840 && height == 2160);
}

// Returns an empty string when the configuration cannot work on the board.
inline std::string build_pipeline(const PipelineConfig& c) {
    if (!capture_size_supported(c.capture_width, c.capture_height)) return "";
    if (c.out_width <= 0 || c.out_height <= 0 || c.fps <= 0) return "";
    const std::string source =
        "libcamerasrc ! video/x-raw,format=YUY2,width=" + std::to_string(c.capture_width) +
        ",height=" + std::to_string(c.capture_height) + " ! ";
    const std::string size =
        ",width=" + std::to_string(c.out_width) + ",height=" + std::to_string(c.out_height);
    const std::string rate = "videorate ! video/x-raw,framerate=" + std::to_string(c.fps) + "/1";
    const std::string sink = " ! appsink drop=true max-buffers=1 sync=false";
    if (c.use_g2d) {
        return source + "imxvideoconvert_g2d ! video/x-raw,format=BGRx" + size + " ! " + rate +
               " ! videoconvert ! video/x-raw,format=BGR" + sink;
    }
    return source + "videoconvert ! videoscale ! video/x-raw,format=BGR" + size + " ! " + rate +
           sink;
}

}  // namespace board_camera
