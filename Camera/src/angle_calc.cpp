#include "angle_calc.h"
#include <cmath>
#include <algorithm>

float ref_l = -1.f;
float ref_r = -1.f;

float calc_angle(const Landmark& a, const Landmark& b, const Landmark& c) {
    float bax = a.x - b.x, bay = a.y - b.y;
    float bcx = c.x - b.x, bcy = c.y - b.y;
    float dot    = bax*bcx + bay*bcy;
    float magBA  = std::sqrt(bax*bax + bay*bay);
    float magBC  = std::sqrt(bcx*bcx + bcy*bcy);
    if (magBA < 1e-6f || magBC < 1e-6f) return 0.f;
    float cosine = std::clamp(dot / (magBA * magBC), -1.f, 1.f);
    return std::acos(cosine) * 180.f / M_PI;
}

std::string classify_phase(float knee_angle) {
    if (knee_angle > 160.f) return "STANCE";
    if (knee_angle < 140.f) return "SWING";
    return "LOADING";
}

// เพิ่มใน angle_calc.cpp

std::string classify_squat(float compression_ratio) {
    // compression_ratio = 1 - (current_length / standing_length)
    // 0.0  = ยืนตรง
    // 0.15 = ย่อเล็กน้อย
    // 0.30 = ย่อปานกลาง
    // 0.50 = นั่งลงมาก
    if (compression_ratio < 0.10f) return "STANDING";
    if (compression_ratio < 0.25f) return "SEMI_SQUAT";
    return "DEEP_SQUAT";
}

GaitResult compute_gait(const std::vector<Landmark>& lm) {
    GaitResult r;

    bool left_ok  = lm[11].score > 0.5f
                 && lm[13].score > 0.5f
                 && lm[15].score > 0.5f;
    bool right_ok = lm[12].score > 0.5f
                 && lm[14].score > 0.5f
                 && lm[16].score > 0.5f;

    // ── Knee angle (ยังเก็บไว้แต่ไม่ใช้ classify phase) ──────
    r.l_knee_angle = left_ok  ? calc_angle(lm[11], lm[13], lm[15]) : -1.f;
    r.r_knee_angle = right_ok ? calc_angle(lm[12], lm[14], lm[16]) : -1.f;

    // ── Front view: ใช้ Y distance แทน ──────────────────────
    // Hip Y → Ankle Y = ความยาวขาที่เห็นจากกล้อง
    float l_leg_len = left_ok
        ? std::abs(lm[15].y - lm[11].y)  // Ankle.y - Hip.y
        : -1.f;
    float r_leg_len = right_ok
        ? std::abs(lm[16].y - lm[12].y)
        : -1.f;

    // ── Calibrate standing length ─────────────────────────────
    // ครั้งแรกที่รัน ระบบจะ calibrate จากท่ายืนตรง
    if (ref_l < 0 && l_leg_len > 0) ref_l = l_leg_len;  // บันทึกค่าอ้างอิง
    if (ref_r < 0 && r_leg_len > 0) ref_r = r_leg_len;

    // ── Compression ratio ─────────────────────────────────────
    r.l_leg_compression = (ref_l > 0 && l_leg_len > 0)
        ? 1.f - (l_leg_len / ref_l) : 0.f;
    r.r_leg_compression = (ref_r > 0 && r_leg_len > 0)
        ? 1.f - (r_leg_len / ref_r) : 0.f;

    float avg_compression = (r.l_leg_compression + r.r_leg_compression) / 2.f;
    r.squat_phase = classify_squat(avg_compression);

    // ── Gait phase (ใช้ compression แทน angle) ───────────────
    r.l_phase = left_ok  ? r.squat_phase : "N/A";
    r.r_phase = right_ok ? r.squat_phase : "N/A";

    // ── Symmetry ──────────────────────────────────────────────
    if (r.l_knee_angle > 0 && r.r_knee_angle > 0) {
        float avg = (r.l_knee_angle + r.r_knee_angle) / 2.f + 1e-6f;
        r.symmetry_index = std::abs(r.l_knee_angle - r.r_knee_angle)
                           / avg * 100.f;
    } else {
        r.symmetry_index = -1.f;
    }

    return r;
}

