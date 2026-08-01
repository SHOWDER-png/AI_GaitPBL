#pragma once
#include "pose_detector.h"
#include <string>

extern float ref_l;
extern float ref_r;

// เพิ่มใน angle_calc.h
struct GaitResult {
    float l_knee_angle,  r_knee_angle;
    float l_hip_angle,   r_hip_angle;
    float symmetry_index;
    std::string l_phase, r_phase;

    // Front view metrics (ใหม่)
    float hip_drop_ratio;      // hip ลงมาเท่าไหร่ (0=ยืนตรง, 1=นั่งลง)
    float l_leg_compression;   // ขาซ้ายสั้นลงเท่าไหร่
    float r_leg_compression;   // ขาขวาสั้นลงเท่าไหร่
    std::string squat_phase;   // STANDING / SEMI_SQUAT / DEEP_SQUAT
};

float      calc_angle(const Landmark& a, const Landmark& b, const Landmark& c);
std::string classify_phase(float knee_angle);
GaitResult  compute_gait(const std::vector<Landmark>& lm);