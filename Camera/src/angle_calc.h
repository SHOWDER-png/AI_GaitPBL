#pragma once
#include "pose_detector.h"
#include <string>
#include <vector>

struct GaitResult {
    // Knee angles (degrees, -1 = not visible)
    float l_knee_angle = -1.f;
    float r_knee_angle = -1.f;

    // Hip angles (reserved for future use)
    float l_hip_angle  = -1.f;
    float r_hip_angle  = -1.f;

    // Symmetry (%, -1 = invalid)
    float symmetry_index = -1.f;

    // Gait phase per leg
    std::string l_phase;
    std::string r_phase;

    // Front-view squat detection
    float l_leg_compression = 0.f;  // 0 = standing, 1 = fully squatted
    float r_leg_compression = 0.f;
    std::string squat_phase;        // STANDING / SEMI_SQUAT / DEEP_SQUAT
};

// Calibration reference (set during standing calibration)
extern float ref_l;
extern float ref_r;

float       calc_angle(const Landmark& a, const Landmark& b, const Landmark& c);
std::string classify_phase(float knee_angle);
std::string classify_squat(float compression_ratio);
GaitResult  compute_gait(const std::vector<Landmark>& lm);