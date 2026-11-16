#!/usr/bin/env python3
"""Reference validators for the interfaces that exist today (SCRUM-348 / SCRUM-350).

Standard library only. Each validator returns (errors, warnings): `errors` mean the data
breaks the contract and must be rejected with that reason; `warnings` are data-quality notes.
The receiver (SCRUM-303/304) and the integration tests can reuse these rules.

    python3 contract_validators.py KIND PATH     KIND = lower_body | imu_csv | gait_csv | frames_csv
"""
from __future__ import annotations

import csv
import json
import math
import sys
from pathlib import Path
from typing import Any

LOWER_BODY_SCHEMA = "lower-body-record.v1"
LOWER_BODY_POINTS = (
    ("left_hip", 11), ("right_hip", 12), ("left_knee", 13),
    ("right_knee", 14), ("left_ankle", 15), ("right_ankle", 16),
)
POINT_STATUS = {"valid", "invalid", "missing"}
POINT_REASONS = {
    "no_person_detection", "keypoint_missing", "x_out_of_frame", "y_out_of_frame",
    "low_confidence", "duplicate_point",
}
IMU_COLUMNS = ["t_ms", "ax_mps2", "ay_mps2", "az_mps2", "gx_rads", "gy_rads", "gz_rads"]
GAIT_COLUMNS = ["ts_ms", "l_knee", "r_knee", "l_hip", "r_hip", "symmetry", "l_phase",
                "r_phase", "squat_phase", "l_compress", "r_compress", "infer_ms"]
FRAMES_COLUMNS = ["frame_index", "phase", "utc_timestamp", "elapsed_monotonic_ms", "width",
                  "height", "read_ms", "estimated_drops_since_previous", "image_path"]
SQUAT_PHASES = {"STANDING", "SEMI_SQUAT", "DEEP_SQUAT"}
IMU_GAP_WARNING_MS = 100        # about ten missed samples at the 100 Hz target
# BMI270 range limits from Nordic/firmware/experiments/imu_bmi270_c29/imu_config.yaml (+/-2 g, +/-500 dps), with margin
IMU_ACCEL_LIMIT_MPS2 = 2 * 9.80665 * 1.05
IMU_GYRO_LIMIT_RADS = 500 * math.pi / 180 * 1.05


def _num(value: Any) -> bool:
    return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value)


def _number_or_none(value: Any) -> bool:
    return value is None or _num(value)


def validate_lower_body_record(record: Any) -> list[str]:
    """Contract: lower-body-record.v1 (Camera/tools/lower_body_records.py)."""
    if not isinstance(record, dict):
        return ["record is not a JSON object"]
    errors: list[str] = []
    if record.get("record_type") != "lower_body":
        errors.append("record_type must be 'lower_body'")
    if record.get("schema_version") != LOWER_BODY_SCHEMA:
        errors.append(f"schema_version must be '{LOWER_BODY_SCHEMA}'")
    if not isinstance(record.get("generated_at_utc"), str):
        errors.append("generated_at_utc must be a string")

    source = record.get("source")
    if not isinstance(source, dict):
        errors.append("source must be an object")
    else:
        for key in ("source_run", "frame_id", "fixture_path", "image_path", "phase",
                    "timestamp_utc"):
            if not isinstance(source.get(key), str) or not source.get(key):
                errors.append(f"source.{key} must be a non-empty string")
        elapsed = source.get("elapsed_monotonic_ms")
        if not _num(elapsed) or elapsed < 0:
            errors.append("source.elapsed_monotonic_ms must be a finite number >= 0")

    image = record.get("image")
    if not isinstance(image, dict):
        errors.append("image must be an object")
    else:
        for key in ("width", "height"):
            value = image.get(key)
            if not isinstance(value, int) or isinstance(value, bool) or value <= 0:
                errors.append(f"image.{key} must be a positive integer")
        if not isinstance(image.get("coordinate_space"), str):
            errors.append("image.coordinate_space must be a string")

    thresholds = record.get("thresholds")
    threshold = thresholds.get("keypoint_confidence") if isinstance(thresholds, dict) else None
    if not _num(threshold):
        errors.append("thresholds.keypoint_confidence must be a finite number")

    detection = record.get("detection")
    if not isinstance(detection, dict) or not isinstance(detection.get("valid"), bool):
        errors.append("detection.valid must be a boolean")
        detection = {}
    elif detection["valid"] and not _num(detection.get("person_confidence")):
        errors.append("detection.person_confidence must be a number when detection.valid")
    elif not detection["valid"] and detection.get("person_confidence") is not None:
        errors.append("detection.person_confidence must be null when detection is not valid")

    points = record.get("points")
    if not isinstance(points, list) or len(points) != len(LOWER_BODY_POINTS):
        errors.append(f"points must be a list of exactly {len(LOWER_BODY_POINTS)} entries")
        return errors
    for position, (point, (name, index)) in enumerate(zip(points, LOWER_BODY_POINTS)):
        label = f"points[{position}]"
        if not isinstance(point, dict):
            errors.append(f"{label} must be an object")
            continue
        if point.get("name") != name or point.get("model_index") != index:
            errors.append(f"{label} must be {name} (model index {index})")
        status = point.get("status")
        if status not in POINT_STATUS:
            errors.append(f"{label}.status must be one of {sorted(POINT_STATUS)}")
        if point.get("valid") is not (status == "valid"):
            errors.append(f"{label}.valid must equal (status == 'valid')")
        coords = point.get("coordinates_px")
        if not isinstance(coords, dict) or not _number_or_none(coords.get("x")) \
                or not _number_or_none(coords.get("y")):
            errors.append(f"{label}.coordinates_px.x/y must be finite numbers or null")
        elif status == "valid" and (coords.get("x") is None or coords.get("y") is None):
            errors.append(f"{label} is valid but has null coordinates")
        confidence = point.get("confidence")
        if not _number_or_none(confidence) or (confidence is not None and not 0 <= confidence <= 1):
            errors.append(f"{label}.confidence must be null or within 0..1")
        elif status == "valid" and confidence is not None and _num(threshold) \
                and confidence < threshold:
            errors.append(f"{label} is valid but confidence is below the threshold")
        reasons = point.get("reasons")
        if not isinstance(reasons, list) or not all(isinstance(r, str) for r in reasons):
            errors.append(f"{label}.reasons must be a list of strings")
        else:
            unknown = [r for r in reasons if r not in POINT_REASONS and not r.startswith("missing_")]
            if unknown:
                errors.append(f"{label}.reasons has unknown reason(s) {unknown}")
            if status == "valid" and reasons:
                errors.append(f"{label} is valid but has reasons")
            if status in ("invalid", "missing") and not reasons:
                errors.append(f"{label} is {status} but has no reason")
        if not isinstance(point.get("duplicate_point"), bool):
            errors.append(f"{label}.duplicate_point must be a boolean")
    if detection.get("valid") is False and any(
            isinstance(p, dict) and p.get("status") == "valid" for p in points):
        errors.append("a no-person record must not contain valid points")
    return errors


def _reject_constant(constant: str) -> None:
    raise ValueError(f"non-standard JSON constant {constant}")


def validate_lower_body_file(path: Path) -> tuple[list[str], list[str]]:
    errors: list[str] = []
    warnings: list[str] = []
    latest: dict[str, float] = {}
    count = 0
    with Path(path).open(encoding="utf-8") as source:
        for number, line in enumerate(source, start=1):
            if not line.strip():
                continue
            count += 1
            try:
                record = json.loads(line, parse_constant=_reject_constant)
            except ValueError as error:
                errors.append(f"line {number}: invalid JSON ({error})")
                continue
            problems = validate_lower_body_record(record)
            errors.extend(f"line {number}: {p}" for p in problems)
            if not problems:
                run = record["source"]["source_run"]
                elapsed = record["source"]["elapsed_monotonic_ms"]
                if run in latest and elapsed < latest[run]:
                    errors.append(f"line {number}: elapsed_monotonic_ms goes backwards in run {run}")
                latest[run] = max(elapsed, latest.get(run, elapsed))
    if count == 0:
        errors.append("file contains no records")
    return errors, warnings


def _read_csv(path: Path, columns: list[str]) -> tuple[list[list[str]], list[str]]:
    with Path(path).open(encoding="utf-8", newline="") as source:
        rows = list(csv.reader(source))
    if not rows:
        return [], ["file is empty"]
    if rows[0] != columns:
        return [], [f"header must be {','.join(columns)}; found {','.join(rows[0])}"]
    return rows[1:], []


def _floats(row: list[str], numeric: list[int], line: int, errors: list[str]) -> dict[int, float]:
    values: dict[int, float] = {}
    for index in numeric:
        try:
            value = float(row[index])
        except ValueError:
            errors.append(f"line {line}: column {index + 1} is not a number ({row[index]!r})")
            continue
        if not math.isfinite(value):
            errors.append(f"line {line}: column {index + 1} is not finite")
            continue
        values[index] = value
    return values


def validate_imu_csv(path: Path) -> tuple[list[str], list[str]]:
    """Contract: Thingy:53 BMI270 export (Nordic/firmware/experiments/imu_bmi270_c29/imu_config.yaml)."""
    rows, errors = _read_csv(path, IMU_COLUMNS)
    warnings: list[str] = []
    if errors:
        return errors, warnings
    if not rows:
        return ["file has a header but no samples"], warnings
    previous = None
    gaps = 0
    for line, row in enumerate(rows, start=2):
        if len(row) != len(IMU_COLUMNS):
            errors.append(f"line {line}: expected {len(IMU_COLUMNS)} columns, found {len(row)}")
            continue
        values = _floats(row, list(range(7)), line, errors)
        if len(values) != 7:
            continue
        if values[0] < 0 or values[0] != int(values[0]):
            errors.append(f"line {line}: t_ms must be a whole number >= 0")
        if previous is not None:
            if values[0] < previous:
                errors.append(f"line {line}: t_ms goes backwards ({values[0]:.0f} < {previous:.0f})")
            elif values[0] - previous > IMU_GAP_WARNING_MS:
                gaps += 1
        previous = values[0]
        if any(abs(values[i]) > IMU_ACCEL_LIMIT_MPS2 for i in (1, 2, 3)):
            errors.append(f"line {line}: acceleration beyond the configured +/-2 g range")
        if any(abs(values[i]) > IMU_GYRO_LIMIT_RADS for i in (4, 5, 6)):
            errors.append(f"line {line}: angular rate beyond the configured +/-500 dps range")
    if gaps:
        warnings.append(f"{gaps} gap(s) longer than {IMU_GAP_WARNING_MS} ms between samples")
    return errors, warnings


def validate_gait_csv(path: Path) -> tuple[list[str], list[str]]:
    """Contract: Camera/gait_data.csv written by main.cpp. -1 means 'not visible/invalid'."""
    rows, errors = _read_csv(path, GAIT_COLUMNS)
    warnings: list[str] = []
    if errors:
        return errors, warnings
    numeric = [0, 1, 2, 3, 4, 5, 9, 10, 11]
    previous = None
    for line, row in enumerate(rows, start=2):
        if len(row) != len(GAIT_COLUMNS):
            errors.append(f"line {line}: expected {len(GAIT_COLUMNS)} columns, found {len(row)}")
            continue
        values = _floats(row, numeric, line, errors)
        if len(values) != len(numeric):
            continue
        if previous is not None and values[0] < previous:
            errors.append(f"line {line}: ts_ms goes backwards")
        previous = values[0]
        for column in (1, 2, 3, 4):
            if not (values[column] == -1 or 0 <= values[column] <= 180):
                errors.append(f"line {line}: {GAIT_COLUMNS[column]} must be -1 or 0..180 degrees")
        if values[5] != -1 and values[5] < 0:
            errors.append(f"line {line}: symmetry must be -1 or >= 0")
        for column in (9, 10):
            if not 0 <= values[column] <= 1:
                errors.append(f"line {line}: {GAIT_COLUMNS[column]} must be within 0..1")
        if values[11] < 0:
            errors.append(f"line {line}: infer_ms must be >= 0")
        if row[8] not in SQUAT_PHASES:
            errors.append(f"line {line}: squat_phase must be one of {sorted(SQUAT_PHASES)}")
    if not rows:
        warnings.append("file has a header but no rows")
    return errors, warnings


def validate_frames_csv(path: Path) -> tuple[list[str], list[str]]:
    """Contract: capture log written by Camera/src/camera_capture.cpp."""
    rows, errors = _read_csv(path, FRAMES_COLUMNS)
    warnings: list[str] = []
    if errors:
        return errors, warnings
    previous_elapsed = None
    for line, row in enumerate(rows, start=2):
        if len(row) != len(FRAMES_COLUMNS):
            errors.append(f"line {line}: expected {len(FRAMES_COLUMNS)} columns, found {len(row)}")
            continue
        values = _floats(row, [0, 3, 4, 5, 6, 7], line, errors)
        if len(values) != 6:
            continue
        if previous_elapsed is not None and values[3] <= previous_elapsed:
            errors.append(f"line {line}: elapsed_monotonic_ms must increase")
        previous_elapsed = values[3]
        if values[4] <= 0 or values[5] <= 0 or values[7] < 0:
            errors.append(f"line {line}: width/height must be > 0 and drops >= 0")
    return errors, warnings


VALIDATORS = {
    "lower_body": validate_lower_body_file,
    "imu_csv": validate_imu_csv,
    "gait_csv": validate_gait_csv,
    "frames_csv": validate_frames_csv,
}


def main(argv: list[str] | None = None) -> int:
    args = sys.argv[1:] if argv is None else argv
    if len(args) != 2 or args[0] not in VALIDATORS:
        print(__doc__)
        return 2
    errors, warnings = VALIDATORS[args[0]](Path(args[1]))
    for warning in warnings:
        print(f"warning: {warning}")
    for error in errors:
        print(f"error: {error}")
    print("PASS" if not errors else f"FAIL ({len(errors)} error(s))")
    return 0 if not errors else 1


if __name__ == "__main__":
    raise SystemExit(main())
