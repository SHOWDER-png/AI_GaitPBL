#include "angle_calc.h"
#include <cmath>
#include <algorithm>

float ref_l = -1.f;
float ref_r = -1.f;

float calc_angle(const Landmark& a, const Landmark& b, const Landmark& c) {
    float bax = a.x - b.x, bay = a.y - b.y;
    float bcx = c.x - b.x, bcy = c.y - b.y;
    float dot   = bax*bcx + bay*bcy;
    float magBA = std::sqrt(bax*bax + bay*bay);
    float magBC = std::sqrt(bcx*bcx + bcy*bcy);
    if (magBA < 1e-6f || magBC < 1e-6f) return 0.f;
    float cosine = std::clamp(dot / (magBA * magBC), -1.f, 1.f);
    return std::acos(cosine) * 180.f / M_PI;
}

std::string classify_phase(float knee_angle) {
    if (knee_angle > 160.f) return "STANCE";
    if (knee_angle < 140.f) return "SWING";
    return "LOADING";
}

std::string classify_squat(float compression_ratio) {
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

    // ── Knee angles ───────────────────────────────────────────
    r.l_knee_angle = left_ok
        ? calc_angle(lm[11], lm[13], lm[15]) : -1.f;
    r.r_knee_angle = right_ok
        ? calc_angle(lm[12], lm[14], lm[16]) : -1.f;

    // ── Hip angles (shoulder → hip → knee) ───────────────────
    // Requires shoulder visible (ID 5=L.Shoulder, 6=R.Shoulder)
    bool l_hip_ok = left_ok  && lm[5].score > 0.3f;
    bool r_hip_ok = right_ok && lm[6].score > 0.3f;
    r.l_hip_angle = l_hip_ok
        ? calc_angle(lm[5],  lm[11], lm[13]) : -1.f;
    r.r_hip_angle = r_hip_ok
        ? calc_angle(lm[6],  lm[12], lm[14]) : -1.f;

    // ── Front-view compression ratio ──────────────────────────
    float l_leg_len = left_ok
        ? std::abs(lm[15].y - lm[11].y) : -1.f;
    float r_leg_len = right_ok
        ? std::abs(lm[16].y - lm[12].y) : -1.f;

    r.l_leg_compression = (ref_l > 0 && l_leg_len > 0)
        ? std::clamp(1.f - (l_leg_len / ref_l), 0.f, 1.f) : 0.f;
    r.r_leg_compression = (ref_r > 0 && r_leg_len > 0)
        ? std::clamp(1.f - (r_leg_len / ref_r), 0.f, 1.f) : 0.f;

    float avg_comp = (r.l_leg_compression + r.r_leg_compression) / 2.f;
    r.squat_phase  = classify_squat(avg_comp);

    r.l_phase = left_ok  ? r.squat_phase : "N/A";
    r.r_phase = right_ok ? r.squat_phase : "N/A";

    // ── Symmetry ──────────────────────────────────────────────
    // Use compression difference for front-view symmetry
    if (left_ok && right_ok) {
        float diff = std::abs(r.l_leg_compression - r.r_leg_compression);
        float avg  = (r.l_leg_compression + r.r_leg_compression) / 2.f + 1e-6f;
        r.symmetry_index = diff / avg * 100.f;

        // Fallback: if both legs at 0 compression (standing),
        // use knee angle difference instead
        if (avg_comp < 0.05f && r.l_knee_angle > 0 && r.r_knee_angle > 0) {
            float kavg = (r.l_knee_angle + r.r_knee_angle) / 2.f + 1e-6f;
            r.symmetry_index = std::abs(r.l_knee_angle - r.r_knee_angle)
                               / kavg * 100.f;
        }
    } else {
        r.symmetry_index = -1.f;
    }

    return r;
}