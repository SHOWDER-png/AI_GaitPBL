#!/usr/bin/env python3
"""Generate deterministic synthetic fixtures for every existing interface (SCRUM-350).

No hardware, camera or model is needed. Same seed, same bytes: the smoke tests check that.
Every fixture is listed in expected.json with the verdict the reference validators must give,
so a receiver or a new component can be tested against the same files.

    python3 make_synthetic_fixtures.py [--output DIR]
"""
from __future__ import annotations

import argparse
import copy
import json
import math
import random
from pathlib import Path

from contract_validators import (FRAMES_COLUMNS, GAIT_COLUMNS, IMU_COLUMNS,
                                 LOWER_BODY_POINTS, LOWER_BODY_SCHEMA)

SEED = 20261111
DEFAULT_OUTPUT = Path(__file__).resolve().parents[1] / "fixtures" / "synthetic"
GENERATED_AT = "2026-11-11T00:00:00.000000Z"


# ── lower-body-record.v1 ──────────────────────────────────────────────────────
def lower_body_record(frame: int, rng: random.Random, status: str = "valid",
                      person: bool = True) -> dict:
    sway = math.sin(frame / 6.0)
    base = {"left_hip": (640 - 60, 330), "right_hip": (640 + 60, 330),
            "left_knee": (640 - 60, 480), "right_knee": (640 + 60, 480),
            "left_ankle": (640 - 60, 640), "right_ankle": (640 + 60, 640)}
    points = []
    for name, index in LOWER_BODY_POINTS:
        x, y = base[name]
        if person and status != "no_person":
            x = round(x + 8 * sway + rng.uniform(-1, 1), 3)
            y = round(y + (12 * sway if "ankle" in name or "knee" in name else 0), 3)
            confidence = round(rng.uniform(0.82, 0.99), 4)
            point = {"name": name, "model_index": index,
                     "coordinates_px": {"x": x, "y": y}, "confidence": confidence,
                     "source_valid": True, "status": "valid", "valid": True, "reasons": [],
                     "duplicate_point": False, "duplicate_of": None}
            if status == "low_confidence" and "ankle" in name:
                point.update(confidence=0.31, source_valid=False, status="invalid",
                             valid=False, reasons=["low_confidence"])
        else:
            point = {"name": name, "model_index": index,
                     "coordinates_px": {"x": None, "y": None}, "confidence": None,
                     "source_valid": None, "status": "missing", "valid": False,
                     "reasons": ["no_person_detection", "keypoint_missing"],
                     "duplicate_point": False, "duplicate_of": None}
        points.append(point)
    return {
        "record_type": "lower_body", "schema_version": LOWER_BODY_SCHEMA,
        "generated_at_utc": GENERATED_AT,
        "source": {"source_run": "SYNTHETIC-20261111", "frame_id": str(frame),
                   "fixture_path": f"synthetic/frame_{frame}.png",
                   "image_path": f"/synthetic/frame_{frame}.png", "phase": "walking",
                   "timestamp_utc": f"2026-11-11T09:00:{frame // 30:02d}.{(frame % 30) * 33:03d}Z",
                   "elapsed_monotonic_ms": round(1000.0 + frame * 33.333, 3)},
        "image": {"width": 1280, "height": 720,
                  "coordinate_space": "pixel; origin top-left; x right; y down"},
        "thresholds": {"keypoint_confidence": 0.5},
        "detection": {"valid": person and status != "no_person",
                      "person_confidence": round(rng.uniform(0.85, 0.97), 4)
                      if person and status != "no_person" else None},
        "points": points,
    }


def dump_jsonl(records: list, path: Path, raw_tail: str = "") -> None:
    text = "".join(json.dumps(r, separators=(",", ":"), allow_nan=False) + "\n" for r in records)
    path.write_text(text + raw_tail, encoding="utf-8")


def make_lower_body(output: Path, expected: dict) -> None:
    rng = random.Random(SEED)
    valid = [lower_body_record(i, rng) for i in range(30)]
    dump_jsonl(valid, output / "lower_body_valid.jsonl")
    expected["lower_body_valid.jsonl"] = {"kind": "lower_body", "valid": True}

    dump_jsonl([lower_body_record(i, rng, "no_person", person=False) for i in range(5)],
               output / "lower_body_no_person.jsonl")
    expected["lower_body_no_person.jsonl"] = {"kind": "lower_body", "valid": True}

    dump_jsonl([lower_body_record(i, rng, "low_confidence") for i in range(5)],
               output / "lower_body_low_confidence.jsonl")
    expected["lower_body_low_confidence.jsonl"] = {"kind": "lower_body", "valid": True}

    def broken(name: str, mutate, contains: str) -> None:
        records = [lower_body_record(i, rng) for i in range(3)]
        mutate(records)
        dump_jsonl(records, output / name)
        expected[name] = {"kind": "lower_body", "valid": False, "error_contains": contains}

    broken("lower_body_bad_schema_version.jsonl",
           lambda r: r[1].update(schema_version="lower-body-record.v2"), "schema_version")
    broken("lower_body_missing_point.jsonl",
           lambda r: r[1]["points"].pop(), "exactly 6")
    broken("lower_body_wrong_point_order.jsonl",
           lambda r: r[0]["points"].reverse(), "must be left_hip")
    broken("lower_body_valid_with_reason.jsonl",
           lambda r: r[2]["points"][0]["reasons"].append("low_confidence"), "valid but has reasons")
    broken("lower_body_confidence_out_of_range.jsonl",
           lambda r: r[0]["points"][3].update(confidence=1.7), "within 0..1")
    broken("lower_body_valid_below_threshold.jsonl",
           lambda r: r[0]["points"][3].update(confidence=0.2), "below the threshold")
    broken("lower_body_unknown_reason.jsonl",
           lambda r: r[0]["points"][0].update(status="invalid", valid=False, reasons=["cosmic_ray"]),
           "unknown reason")
    broken("lower_body_timestamp_backwards.jsonl",
           lambda r: r[2]["source"].update(elapsed_monotonic_ms=10.0), "goes backwards")
    broken("lower_body_no_person_with_valid_point.jsonl",
           lambda r: r[0]["detection"].update(valid=False, person_confidence=None),
           "must not contain valid points")

    nan_record = lower_body_record(0, rng)
    text = json.dumps(nan_record, separators=(",", ":")).replace(
        '"confidence":', '"confidence":NaN,"was":', 1)
    (output / "lower_body_nan_value.jsonl").write_text(text + "\n", encoding="utf-8")
    expected["lower_body_nan_value.jsonl"] = {"kind": "lower_body", "valid": False,
                                              "error_contains": "invalid JSON"}
    dump_jsonl([lower_body_record(0, rng)], output / "lower_body_truncated.jsonl",
               raw_tail='{"record_type":"lower_body","schema_ver')
    expected["lower_body_truncated.jsonl"] = {"kind": "lower_body", "valid": False,
                                              "error_contains": "invalid JSON"}
    (output / "lower_body_empty.jsonl").write_text("", encoding="utf-8")
    expected["lower_body_empty.jsonl"] = {"kind": "lower_body", "valid": False,
                                          "error_contains": "no records"}


# ── IMU CSV (Thingy:53 BMI270) ────────────────────────────────────────────────
def imu_rows(count: int, rng: random.Random, start_ms: int = 1000, step: int = 10) -> list:
    rows = []
    for i in range(count):
        t = start_ms + i * step
        swing = math.sin(i / 8.0)
        rows.append([t, round(0.4 * swing + rng.gauss(0, 0.02), 4),
                     round(rng.gauss(0, 0.02), 4), round(9.81 + rng.gauss(0, 0.03), 4),
                     round(0.8 * swing + rng.gauss(0, 0.01), 5),
                     round(rng.gauss(0, 0.01), 5), round(rng.gauss(0, 0.01), 5)])
    return rows


def write_csv(path: Path, header: list, rows: list, raw_lines: list | None = None) -> None:
    lines = [",".join(header)] + [",".join(str(v) for v in row) for row in rows]
    lines += raw_lines or []
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def make_imu(output: Path, expected: dict) -> None:
    rng = random.Random(SEED + 1)
    write_csv(output / "imu_valid.csv", IMU_COLUMNS, imu_rows(300, rng))
    expected["imu_valid.csv"] = {"kind": "imu_csv", "valid": True, "warnings": 0}

    gap = imu_rows(300, rng)
    for row in gap[150:]:
        row[0] += 250
    write_csv(output / "imu_gap.csv", IMU_COLUMNS, gap)
    expected["imu_gap.csv"] = {"kind": "imu_csv", "valid": True, "warnings": 1}

    write_csv(output / "imu_bad_header.csv", ["time", "ax", "ay", "az", "gx", "gy", "gz"],
              imu_rows(20, rng))
    expected["imu_bad_header.csv"] = {"kind": "imu_csv", "valid": False,
                                      "error_contains": "header must be"}
    write_csv(output / "imu_header_only.csv", IMU_COLUMNS, [])
    expected["imu_header_only.csv"] = {"kind": "imu_csv", "valid": False,
                                       "error_contains": "no samples"}
    back = imu_rows(20, rng)
    back[10][0] = 1000
    write_csv(output / "imu_time_backwards.csv", IMU_COLUMNS, back)
    expected["imu_time_backwards.csv"] = {"kind": "imu_csv", "valid": False,
                                          "error_contains": "goes backwards"}
    loud = imu_rows(20, rng)
    loud[5][1] = 50.0
    write_csv(output / "imu_accel_out_of_range.csv", IMU_COLUMNS, loud)
    expected["imu_accel_out_of_range.csv"] = {"kind": "imu_csv", "valid": False,
                                              "error_contains": "+/-2 g"}
    spin = imu_rows(20, rng)
    spin[5][4] = 20.0
    write_csv(output / "imu_gyro_out_of_range.csv", IMU_COLUMNS, spin)
    expected["imu_gyro_out_of_range.csv"] = {"kind": "imu_csv", "valid": False,
                                             "error_contains": "+/-500 dps"}
    write_csv(output / "imu_missing_column.csv", IMU_COLUMNS, imu_rows(20, rng),
              raw_lines=["2000,0.1,0.1,9.8,0.0,0.0"])
    expected["imu_missing_column.csv"] = {"kind": "imu_csv", "valid": False,
                                          "error_contains": "expected 7 columns"}
    write_csv(output / "imu_not_a_number.csv", IMU_COLUMNS, imu_rows(20, rng),
              raw_lines=["2000,0.1,nan,9.8,0.0,0.0,0.0"])
    expected["imu_not_a_number.csv"] = {"kind": "imu_csv", "valid": False,
                                        "error_contains": "not finite"}


# ── gait_data.csv (Camera main.cpp) ───────────────────────────────────────────
def gait_rows(count: int, rng: random.Random) -> list:
    rows = []
    for i in range(count):
        knee = round(165 + 12 * math.sin(i / 5.0) + rng.uniform(-1, 1), 2)
        rows.append([round(i * 33.3, 1), knee, round(knee + rng.uniform(-3, 3), 2), -1, -1,
                     round(rng.uniform(0, 8), 2), "STAND", "STAND", "STANDING",
                     round(rng.uniform(0, 0.08), 3), round(rng.uniform(0, 0.08), 3),
                     round(rng.uniform(70, 90), 1)])
    return rows


def make_gait(output: Path, expected: dict) -> None:
    rng = random.Random(SEED + 2)
    write_csv(output / "gait_valid.csv", GAIT_COLUMNS, gait_rows(60, rng))
    expected["gait_valid.csv"] = {"kind": "gait_csv", "valid": True}

    not_visible = gait_rows(10, rng)
    for row in not_visible[4:]:
        row[1:6] = [-1, -1, -1, -1, -1]
    write_csv(output / "gait_not_visible.csv", GAIT_COLUMNS, not_visible)
    expected["gait_not_visible.csv"] = {"kind": "gait_csv", "valid": True}

    write_csv(output / "gait_bad_header.csv", GAIT_COLUMNS[:-1], [r[:-1] for r in gait_rows(5, rng)])
    expected["gait_bad_header.csv"] = {"kind": "gait_csv", "valid": False,
                                       "error_contains": "header must be"}
    bad_phase = gait_rows(10, rng)
    bad_phase[3][8] = "FLYING"
    write_csv(output / "gait_bad_phase.csv", GAIT_COLUMNS, bad_phase)
    expected["gait_bad_phase.csv"] = {"kind": "gait_csv", "valid": False,
                                      "error_contains": "squat_phase"}
    bad_angle = gait_rows(10, rng)
    bad_angle[2][1] = 250
    write_csv(output / "gait_knee_out_of_range.csv", GAIT_COLUMNS, bad_angle)
    expected["gait_knee_out_of_range.csv"] = {"kind": "gait_csv", "valid": False,
                                              "error_contains": "0..180"}
    bad_ratio = gait_rows(10, rng)
    bad_ratio[2][9] = 1.4
    write_csv(output / "gait_compress_out_of_range.csv", GAIT_COLUMNS, bad_ratio)
    expected["gait_compress_out_of_range.csv"] = {"kind": "gait_csv", "valid": False,
                                                  "error_contains": "within 0..1"}
    back = gait_rows(10, rng)
    back[6][0] = 5.0
    write_csv(output / "gait_time_backwards.csv", GAIT_COLUMNS, back)
    expected["gait_time_backwards.csv"] = {"kind": "gait_csv", "valid": False,
                                           "error_contains": "goes backwards"}


# ── frames.csv (camera capture log) ───────────────────────────────────────────
def make_frames(output: Path, expected: dict) -> None:
    rng = random.Random(SEED + 3)
    rows = [[i + 1, "walking", f"2026-11-11T09:00:00.{(i * 33) % 1000:03d}Z",
             round(1000 + i * 33.3, 3), 1280, 720, round(rng.uniform(25, 35), 3), 0,
             f"/synthetic/frame_{i + 1}.png"] for i in range(40)]
    write_csv(output / "frames_valid.csv", FRAMES_COLUMNS, rows)
    expected["frames_valid.csv"] = {"kind": "frames_csv", "valid": True}
    stuck = copy.deepcopy(rows)
    stuck[10][3] = stuck[9][3]
    write_csv(output / "frames_time_not_increasing.csv", FRAMES_COLUMNS, stuck)
    expected["frames_time_not_increasing.csv"] = {"kind": "frames_csv", "valid": False,
                                                  "error_contains": "must increase"}


def generate(output: Path) -> dict:
    output.mkdir(parents=True, exist_ok=True)
    expected: dict = {}
    make_lower_body(output, expected)
    make_imu(output, expected)
    make_gait(output, expected)
    make_frames(output, expected)
    (output / "expected.json").write_text(
        json.dumps(expected, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return expected


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    args = parser.parse_args()
    expected = generate(args.output)
    print(f"Wrote {len(expected)} fixtures + expected.json to {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
