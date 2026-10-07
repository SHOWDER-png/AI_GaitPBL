#!/usr/bin/env python3
"""Create timestamped lower-body records from immutable offline inference output."""

from __future__ import annotations

import argparse
import csv
from datetime import datetime, timezone
import json
import math
from pathlib import Path
from typing import Any, Iterable


CAMERA_DIR = Path(__file__).resolve().parents[1]
DEFAULT_INPUT = (
    CAMERA_DIR
    / "results"
    / "scrum371-run-20261006"
    / "offline-inference.jsonl"
)
DEFAULT_MANIFEST = CAMERA_DIR / "fixtures" / "manifest.csv"
KEYPOINTS = (
    ("left_hip", 11),
    ("right_hip", 12),
    ("left_knee", 13),
    ("right_knee", 14),
    ("left_ankle", 15),
    ("right_ankle", 16),
)
SCHEMA_VERSION = "lower-body-record.v1"


class InputError(ValueError):
    """Raised when an input file cannot be safely interpreted."""


def _parse_constant(value: str) -> None:
    raise InputError(f"non-standard JSON number {value!r} is not supported")


def read_inference_records(path: Path) -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []
    with path.open(encoding="utf-8") as source:
        for line_number, line in enumerate(source, start=1):
            if not line.strip():
                continue
            try:
                record = json.loads(line, parse_constant=_parse_constant)
            except (json.JSONDecodeError, InputError) as error:
                raise InputError(f"{path}:{line_number}: {error}") from error
            if not isinstance(record, dict):
                raise InputError(f"{path}:{line_number}: expected a JSON object")
            records.append(record)
    if not records:
        raise InputError(f"{path}: no JSON records found")
    return records


def read_manifest(path: Path) -> dict[tuple[str, str, str], dict[str, Any]]:
    rows: dict[tuple[str, str, str], dict[str, Any]] = {}
    with path.open(encoding="utf-8", newline="") as source:
        reader = csv.DictReader(source)
        required = {
            "fixture_path",
            "phase",
            "source_run",
            "source_frame_index",
            "utc_timestamp",
            "elapsed_monotonic_ms",
        }
        missing_columns = required.difference(reader.fieldnames or ())
        if missing_columns:
            raise InputError(
                f"{path}: missing manifest columns: {', '.join(sorted(missing_columns))}"
            )
        for line_number, row in enumerate(reader, start=2):
            key = (
                row["source_run"],
                row["source_frame_index"],
                row["fixture_path"],
            )
            if key in rows:
                raise InputError(f"{path}:{line_number}: duplicate manifest key {key}")
            if not row["utc_timestamp"] or not row["elapsed_monotonic_ms"]:
                raise InputError(f"{path}:{line_number}: capture timestamp is missing")
            try:
                elapsed_ms = float(row["elapsed_monotonic_ms"])
            except ValueError as error:
                raise InputError(
                    f"{path}:{line_number}: invalid elapsed_monotonic_ms"
                ) from error
            if not math.isfinite(elapsed_ms):
                raise InputError(
                    f"{path}:{line_number}: elapsed_monotonic_ms is not finite"
                )
            rows[key] = {
                "phase": row["phase"],
                "timestamp_utc": row["utc_timestamp"],
                "elapsed_monotonic_ms": elapsed_ms,
            }
    return rows


def _finite_number(value: Any, field: str) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise InputError(f"{field} must be a JSON number")
    try:
        number = float(value)
    except OverflowError as error:
        raise InputError(f"{field} must be finite") from error
    if not math.isfinite(number):
        raise InputError(f"{field} must be finite")
    return number


def _positive_int(value: Any, field: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or value <= 0:
        raise InputError(f"{field} must be a positive integer")
    return value


def _source_points(frame: dict[str, Any]) -> dict[int, dict[str, Any]]:
    source_points = frame.get("keypoints")
    if not isinstance(source_points, list):
        raise InputError("frame keypoints must be an array")
    points: dict[int, dict[str, Any]] = {}
    for point in source_points:
        if not isinstance(point, dict):
            raise InputError("each source keypoint must be an object")
        index = point.get("index")
        if isinstance(index, bool) or not isinstance(index, int):
            raise InputError("each source keypoint must have an integer index")
        if index in points:
            raise InputError(f"duplicate source keypoint index {index}")
        points[index] = point
    return points


def _make_point(
    name: str,
    model_index: int,
    raw: dict[str, Any] | None,
    detection_valid: bool,
    width: int,
    height: int,
    confidence_threshold: float,
) -> dict[str, Any]:
    x = raw.get("x") if raw else None
    y = raw.get("y") if raw else None
    confidence = raw.get("confidence") if raw else None
    reasons: list[str] = []
    if not detection_valid:
        reasons.append("no_person_detection")
    if raw is None:
        reasons.append("keypoint_missing")
    else:
        for field, value in (("x", x), ("y", y), ("confidence", confidence)):
            if value is None:
                reasons.append(f"missing_{field}")
            else:
                _finite_number(value, f"{name}.{field}")

        if x is not None:
            numeric_x = _finite_number(x, f"{name}.x")
            if numeric_x < 0 or numeric_x >= width:
                reasons.append("x_out_of_frame")
        if y is not None:
            numeric_y = _finite_number(y, f"{name}.y")
            if numeric_y < 0 or numeric_y >= height:
                reasons.append("y_out_of_frame")
        if confidence is not None:
            numeric_confidence = _finite_number(
                confidence, f"{name}.confidence"
            )
            if numeric_confidence < confidence_threshold:
                reasons.append("low_confidence")

    if raw is None or any(
        value is None for value in (x, y, confidence)
    ):
        status = "missing"
    elif any(
        reason.endswith("_out_of_frame") or reason == "low_confidence"
        for reason in reasons
    ):
        status = "invalid"
    elif not detection_valid:
        status = "missing"
    else:
        status = "valid"

    source_valid = raw.get("valid") if raw else None
    if source_valid is not None and not isinstance(source_valid, bool):
        raise InputError(f"{name}.valid must be a boolean or null")
    return {
        "name": name,
        "model_index": model_index,
        "coordinates_px": {"x": x, "y": y},
        "confidence": confidence,
        "source_valid": source_valid,
        "status": status,
        "valid": status == "valid",
        "reasons": reasons,
        "duplicate_point": False,
        "duplicate_of": None,
    }


def _flag_exact_duplicates(points: list[dict[str, Any]]) -> None:
    groups: dict[tuple[float, float], list[dict[str, Any]]] = {}
    for point in points:
        if not point["valid"]:
            continue
        coordinates = point["coordinates_px"]
        key = (float(coordinates["x"]), float(coordinates["y"]))
        groups.setdefault(key, []).append(point)

    for group in groups.values():
        if len(group) < 2:
            continue
        canonical = min(group, key=lambda point: point["model_index"])
        for point in group:
            point["duplicate_point"] = True
            point["reasons"].append("duplicate_point")
            if point is not canonical:
                point["duplicate_of"] = canonical["name"]


def make_lower_body_record(
    frame: dict[str, Any],
    manifest: dict[tuple[str, str, str], dict[str, Any]],
    generated_at_utc: str,
) -> dict[str, Any]:
    if frame.get("record_type") != "frame":
        raise InputError("expected an inference frame record")
    identity = (
        frame.get("source_run"),
        frame.get("frame_id"),
        frame.get("fixture_path"),
    )
    if not all(isinstance(part, str) and part for part in identity):
        raise InputError("frame is missing source_run, frame_id, or fixture_path")
    capture = manifest.get(identity)
    if capture is None:
        raise InputError(f"frame has no matching capture manifest row: {identity}")

    width = _positive_int(frame.get("image_width"), "image_width")
    height = _positive_int(frame.get("image_height"), "image_height")
    threshold = _finite_number(
        frame.get("keypoint_confidence_threshold"),
        "keypoint_confidence_threshold",
    )
    if not isinstance(frame.get("detection_valid"), bool):
        raise InputError("detection_valid must be a boolean")
    if frame["detection_valid"] and frame.get("person_confidence") is not None:
        _finite_number(frame["person_confidence"], "person_confidence")
    elif frame["detection_valid"]:
        raise InputError("a valid detection must have person_confidence")

    indexed_points = _source_points(frame)
    points = [
        _make_point(
            name,
            index,
            indexed_points.get(index),
            frame["detection_valid"],
            width,
            height,
            threshold,
        )
        for name, index in KEYPOINTS
    ]
    _flag_exact_duplicates(points)
    return {
        "record_type": "lower_body",
        "schema_version": SCHEMA_VERSION,
        "generated_at_utc": generated_at_utc,
        "source": {
            "source_run": identity[0],
            "frame_id": identity[1],
            "fixture_path": identity[2],
            "image_path": frame.get("image_path"),
            "phase": frame.get("phase", capture["phase"]),
            "timestamp_utc": capture["timestamp_utc"],
            "elapsed_monotonic_ms": capture["elapsed_monotonic_ms"],
        },
        "image": {
            "width": width,
            "height": height,
            "coordinate_space": "pixel; origin top-left; x right; y down",
        },
        "thresholds": {"keypoint_confidence": threshold},
        "detection": {
            "valid": frame["detection_valid"],
            "person_confidence": frame.get("person_confidence"),
        },
        "points": points,
    }


def build_records(
    inference_records: Iterable[dict[str, Any]],
    manifest: dict[tuple[str, str, str], dict[str, Any]],
    generated_at_utc: str,
) -> list[dict[str, Any]]:
    inference_records = list(inference_records)
    frames = [
        record
        for record in inference_records
        if record.get("record_type") == "frame"
    ]
    if not frames:
        raise InputError("inference input contains no frame records")
    unknown = [
        record.get("record_type")
        for record in inference_records
        if record.get("record_type") not in {"run", "frame"}
    ]
    if unknown:
        raise InputError(f"unsupported inference record types: {unknown}")
    return [
        make_lower_body_record(frame, manifest, generated_at_utc)
        for frame in frames
    ]


def write_timestamped_records(
    records: list[dict[str, Any]], output_directory: Path, now: datetime
) -> Path:
    timestamp = now.astimezone(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
    output_directory.mkdir(parents=True, exist_ok=True)
    output_path = output_directory / f"lower-body-records-{timestamp}.jsonl"
    with output_path.open("x", encoding="utf-8") as output:
        for record in records:
            output.write(
                json.dumps(record, separators=(",", ":"), allow_nan=False) + "\n"
            )
    return output_path


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Extract timestamped lower-body keypoint records."
    )
    parser.add_argument("--input", type=Path, default=DEFAULT_INPUT)
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    parser.add_argument("--output-dir", type=Path)
    args = parser.parse_args(argv)

    now = datetime.now(timezone.utc)
    generated_at = now.isoformat(timespec="microseconds").replace("+00:00", "Z")
    output_directory = args.output_dir or (
        CAMERA_DIR
        / "results"
        / f"scrum372-run-{now.strftime('%Y%m%d')}"
    )
    try:
        raw_records = read_inference_records(args.input)
        manifest = read_manifest(args.manifest)
        records = build_records(raw_records, manifest, generated_at)
        output_path = write_timestamped_records(records, output_directory, now)
    except (OSError, InputError) as error:
        parser.exit(1, f"error: {error}\n")

    print(f"Wrote {len(records)} lower-body records to {output_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
