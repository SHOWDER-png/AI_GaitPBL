#include "offline_inference.h"

#include <cmath>
#include <iomanip>
#include <ostream>

#include "offline_validation.h"

namespace {

void write_number_or_null(std::ostream& output, float value) {
    if (std::isfinite(value)) output << value;
    else output << "null";
}

}  // namespace

std::string json_escape(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (const unsigned char ch : value) {
        switch (ch) {
            case '"': escaped += "\\\""; break;
            case '\\': escaped += "\\\\"; break;
            case '\b': escaped += "\\b"; break;
            case '\f': escaped += "\\f"; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            default:
                if (ch < 0x20) {
                    const char hex[] = "0123456789abcdef";
                    escaped += "\\u00";
                    escaped += hex[(ch >> 4) & 0x0f];
                    escaped += hex[ch & 0x0f];
                } else {
                    escaped += static_cast<char>(ch);
                }
        }
    }
    return escaped;
}

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
    const Detection* detection)
{
    const bool valid = detection != nullptr;
    output << std::setprecision(9)
           << "{\"record_type\":\"frame\",\"fixture_path\":\""
           << json_escape(fixture_path)
           << "\",\"image_path\":\"" << json_escape(image_path)
           << "\",\"phase\":\"" << json_escape(phase)
           << "\",\"frame_id\":\"" << json_escape(frame_id)
           << "\",\"source_run\":\"" << json_escape(source_run)
           << "\",\"person_detection_threshold\":" << person_threshold
           << ",\"keypoint_confidence_threshold\":" << keypoint_threshold
           << ",\"detection_valid\":" << (valid ? "true" : "false")
           << ",\"person_confidence\":";
    if (valid) write_number_or_null(output, detection->conf);
    else output << "null";
    output << ",\"image_width\":" << image_width
           << ",\"image_height\":" << image_height
           << ",\"inference_latency_ms\":" << latency_ms
           << ",\"keypoints\":[";

    if (valid) {
        for (int index = 0; index < 17; ++index) {
            if (index > 0) output << ',';
            if (static_cast<std::size_t>(index) >= detection->kpts.size()) {
                output << "{\"index\":" << index
                       << ",\"x\":null,\"y\":null,\"confidence\":null,"
                          "\"valid\":false}";
                continue;
            }

            const Landmark& keypoint = detection->kpts[index];
            const bool keypoint_valid =
                std::isfinite(keypoint.x) && std::isfinite(keypoint.y)
                && offline_validation::confidence_meets_threshold(
                    keypoint.score, keypoint_threshold);
            output << "{\"index\":" << index << ",\"x\":";
            write_number_or_null(output, keypoint.x);
            output << ",\"y\":";
            write_number_or_null(output, keypoint.y);
            output << ",\"confidence\":";
            write_number_or_null(output, keypoint.score);
            output << ",\"valid\":" << (keypoint_valid ? "true" : "false")
                   << '}';
        }
    }
    output << "]}\n";
}
