#pragma once

#include <iosfwd>
#include <string>

#include "pose_detector.h"

void write_offline_frame_json(
    std::ostream& output,
    const std::string& fixture_path,
    const std::string& image_path,
    const std::string& phase,
    const std::string& frame_id,
    const std::string& source_run,
    float person_threshold,
    float keypoint_threshold,
    int image_width,
    int image_height,
    double latency_ms,
    const Detection* detection);

std::string json_escape(const std::string& value);
