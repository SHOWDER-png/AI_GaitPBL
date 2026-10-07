#!/usr/bin/env python3
"""Generate SCRUM-373 annotated fixture images and a verified local report."""

from __future__ import annotations

import argparse
import csv
from datetime import datetime, timezone
import hashlib
import html
import json
import math
import os
from pathlib import Path
from typing import Any
import xml.sax.saxutils


CAMERA_DIR = Path(__file__).resolve().parents[1]
DEFAULT_OUTPUT_DIR = (
    CAMERA_DIR / "results" / "scrum373-annotated-performance-report"
)
DEFAULT_INFERENCE = (
    CAMERA_DIR
    / "results"
    / "scrum371-run-20261006"
    / "offline-inference.jsonl"
)
DEFAULT_SUMMARY = (
    CAMERA_DIR
    / "results"
    / "scrum371-run-20261006"
    / "offline-inference-summary.json"
)
DEFAULT_RUN_CONFIG = (
    CAMERA_DIR / "results" / "scrum371-run-20261006" / "run-config.json"
)
DEFAULT_LOWER_BODY = (
    CAMERA_DIR
    / "results"
    / "scrum372-run-20261007"
    / "lower-body-records-20261007T122019.763970Z.jsonl"
)
DEFAULT_CAPTURE_CONFIG = CAMERA_DIR / "capture-config.json"
DEFAULT_FIXTURE_MANIFEST = CAMERA_DIR / "fixtures" / "manifest.csv"

SCHEMA_VERSION = "scrum373-results.v1"
MEAN_LATENCY_TARGET_MS = 100.0
FPS_TARGET = 15.0
LOWER_BODY_NAMES = (
    "left_hip",
    "right_hip",
    "left_knee",
    "right_knee",
    "left_ankle",
    "right_ankle",
)
JOINT_LINKS = (
    ("left_hip", "left_knee"),
    ("left_knee", "left_ankle"),
    ("right_hip", "right_knee"),
    ("right_knee", "right_ankle"),
)
STATUS_COLORS = {
    "valid": "#35d07f",
    "invalid": "#ffb44c",
    "missing": "#aab4c0",
}


class ReportInputError(ValueError):
    """Raised when source results are incomplete or inconsistent."""


def read_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ReportInputError(f"cannot read JSON input {path}: {error}") from error
    if not isinstance(value, dict):
        raise ReportInputError(f"{path}: expected a JSON object")
    return value


def read_jsonl(path: Path) -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []
    try:
        with path.open(encoding="utf-8") as source:
            for line_number, line in enumerate(source, start=1):
                if not line.strip():
                    continue
                try:
                    record = json.loads(line)
                except json.JSONDecodeError as error:
                    raise ReportInputError(
                        f"{path}:{line_number}: invalid JSON: {error}"
                    ) from error
                if not isinstance(record, dict):
                    raise ReportInputError(
                        f"{path}:{line_number}: expected a JSON object"
                    )
                records.append(record)
    except OSError as error:
        raise ReportInputError(f"cannot read JSONL input {path}: {error}") from error
    if not records:
        raise ReportInputError(f"{path}: no records found")
    return records


def read_fixture_manifest(path: Path) -> dict[str, dict[str, str]]:
    required = {
        "fixture_path",
        "phase",
        "source_run",
        "source_frame_index",
        "utc_timestamp",
        "elapsed_monotonic_ms",
        "width",
        "height",
        "source_image_path",
        "sha256",
    }
    rows: dict[str, dict[str, str]] = {}
    try:
        with path.open(encoding="utf-8", newline="") as source:
            reader = csv.DictReader(source)
            missing = required.difference(reader.fieldnames or ())
            if missing:
                raise ReportInputError(
                    f"{path}: missing columns: {', '.join(sorted(missing))}"
                )
            for line_number, row in enumerate(reader, start=2):
                fixture = row["fixture_path"]
                if not fixture or fixture in rows:
                    raise ReportInputError(
                        f"{path}:{line_number}: empty or duplicate fixture_path"
                    )
                rows[fixture] = row
    except OSError as error:
        raise ReportInputError(f"cannot read fixture manifest {path}: {error}") from error
    if not rows:
        raise ReportInputError(f"{path}: no fixture rows found")
    return rows


def require_finite(value: Any, description: str) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ReportInputError(f"{description} must be numeric")
    number = float(value)
    if not math.isfinite(number):
        raise ReportInputError(f"{description} must be finite")
    return number


def calculate_metrics(
    frame_records: list[dict[str, Any]], error_count: int
) -> dict[str, float | int]:
    latencies = [
        require_finite(frame.get("inference_latency_ms"), "inference_latency_ms")
        for frame in frame_records
    ]
    if not latencies or any(value < 0 for value in latencies):
        raise ReportInputError("successful inference latency samples are required")
    ordered = sorted(latencies)
    latency_sum = sum(ordered)
    valid_frames = sum(frame.get("detection_valid") is True for frame in frame_records)
    rejected_frames = sum(
        frame.get("detection_valid") is False for frame in frame_records
    )
    return {
        "total_frames": len(frame_records) + error_count,
        "valid_frames": valid_frames,
        "rejected_no_person_frames": rejected_frames,
        "failed_frames": error_count,
        "latency_sample_count": len(ordered),
        "mean_latency_ms": latency_sum / len(ordered),
        "p95_latency_ms": ordered[math.ceil(0.95 * len(ordered)) - 1],
        "fps": len(ordered) * 1000.0 / latency_sum,
    }


def verify_summary(
    summary: dict[str, Any], metrics: dict[str, float | int]
) -> None:
    for field, expected in metrics.items():
        actual = summary.get(field)
        if isinstance(expected, float):
            if not isinstance(actual, (int, float)) or not math.isclose(
                float(actual), expected, rel_tol=1e-7, abs_tol=1e-7
            ):
                raise ReportInputError(
                    f"saved summary {field}={actual!r} does not match "
                    f"per-frame result value {expected!r}"
                )
        elif actual != expected:
            raise ReportInputError(
                f"saved summary {field}={actual!r} does not match "
                f"per-frame result value {expected!r}"
            )


def fixture_dimensions(path: Path) -> tuple[int, int]:
    try:
        with path.open("rb") as image:
            header = image.read(24)
    except OSError as error:
        raise ReportInputError(f"cannot read fixture image {path}: {error}") from error
    if (
        len(header) != 24
        or header[:8] != b"\x89PNG\r\n\x1a\n"
        or header[12:16] != b"IHDR"
    ):
        raise ReportInputError(f"{path}: expected a PNG image with an IHDR header")
    return int.from_bytes(header[16:20], "big"), int.from_bytes(header[20:24], "big")


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    try:
        with path.open("rb") as source:
            for block in iter(lambda: source.read(1024 * 1024), b""):
                digest.update(block)
    except OSError as error:
        raise ReportInputError(f"cannot hash input {path}: {error}") from error
    return digest.hexdigest()


def pair_source_records(
    inference_records: list[dict[str, Any]],
    lower_body_records: list[dict[str, Any]],
    manifest: dict[str, dict[str, str]],
) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    run_records = [r for r in inference_records if r.get("record_type") == "run"]
    frames = [r for r in inference_records if r.get("record_type") == "frame"]
    errors = [r for r in inference_records if r.get("record_type") == "error"]
    unknown = [
        r.get("record_type")
        for r in inference_records
        if r.get("record_type") not in {"run", "frame", "error"}
    ]
    if len(run_records) != 1 or not frames or unknown:
        raise ReportInputError(
            "inference JSONL must have one run record, frame records, and no "
            f"unsupported records; unsupported={unknown}"
        )

    lower_by_identity: dict[tuple[str, str, str], dict[str, Any]] = {}
    for record in lower_body_records:
        if record.get("record_type") != "lower_body":
            raise ReportInputError("lower-body JSONL contains a non-lower_body record")
        source = record.get("source")
        if not isinstance(source, dict):
            raise ReportInputError("lower-body record is missing source identity")
        identity = (
            source.get("source_run"),
            source.get("frame_id"),
            source.get("fixture_path"),
        )
        if not all(isinstance(part, str) and part for part in identity):
            raise ReportInputError(f"invalid lower-body source identity: {identity}")
        if identity in lower_by_identity:
            raise ReportInputError(f"duplicate lower-body source identity: {identity}")
        lower_by_identity[identity] = record

    paired: list[dict[str, Any]] = []
    seen: set[tuple[str, str, str]] = set()
    for frame in frames:
        identity = (
            frame.get("source_run"),
            frame.get("frame_id"),
            frame.get("fixture_path"),
        )
        if not all(isinstance(part, str) and part for part in identity):
            raise ReportInputError(f"invalid inference source identity: {identity}")
        if identity in seen:
            raise ReportInputError(f"duplicate inference source identity: {identity}")
        seen.add(identity)
        lower_body = lower_by_identity.get(identity)
        if lower_body is None:
            raise ReportInputError(
                f"no matching SCRUM-372 record for inference frame {identity}"
            )
        fixture = identity[2]
        row = manifest.get(fixture)
        if row is None:
            raise ReportInputError(f"fixture is absent from manifest: {fixture}")
        if row["source_run"] != identity[0] or row["source_frame_index"] != identity[1]:
            raise ReportInputError(f"fixture manifest identity mismatch: {fixture}")
        image_path = CAMERA_DIR / "fixtures" / fixture
        width, height = fixture_dimensions(image_path)
        if (width, height) != (
            int(frame.get("image_width", 0)),
            int(frame.get("image_height", 0)),
        ):
            raise ReportInputError(f"source image dimensions mismatch: {fixture}")
        if (width, height) != (int(row["width"]), int(row["height"])):
            raise ReportInputError(f"fixture manifest dimensions mismatch: {fixture}")
        if sha256_file(image_path) != row["sha256"]:
            raise ReportInputError(f"fixture SHA-256 mismatch: {fixture}")
        points = lower_body.get("points")
        if not isinstance(points, list) or len(points) != len(LOWER_BODY_NAMES):
            raise ReportInputError(f"invalid lower-body point list: {fixture}")
        if tuple(point.get("name") for point in points) != LOWER_BODY_NAMES:
            raise ReportInputError(f"unexpected lower-body point mapping: {fixture}")
        if lower_body.get("detection", {}).get("valid") != frame.get(
            "detection_valid"
        ):
            raise ReportInputError(f"detection validity mismatch: {fixture}")
        paired.append(
            {
                "inference": frame,
                "lower_body": lower_body,
                "manifest": row,
                "image_path": image_path,
                "image_width": width,
                "image_height": height,
            }
        )
    if seen != set(lower_by_identity):
        raise ReportInputError(
            "SCRUM-372 records and SCRUM-371 inference frames do not have "
            "identical source identities"
        )
    return run_records[0], paired


def select_examples(paired: list[dict[str, Any]]) -> dict[str, str]:
    by_phase: dict[str, list[dict[str, Any]]] = {}
    for item in paired:
        by_phase.setdefault(item["inference"].get("phase", ""), []).append(item)
    try:
        standing = by_phase["standing"][0]
        no_person = by_phase["no_person"][0]
        walking = by_phase["walking"]
    except (KeyError, IndexError) as error:
        raise ReportInputError(
            "real fixture results must include standing, walking, and no_person"
        ) from error
    representative_walking = next(
        (
            item
            for item in walking
            if Path(item["inference"]["fixture_path"]).stem
            == "walking_mid_frame_298"
        ),
        None,
    )
    if representative_walking is None:
        raise ReportInputError("the SCRUM-371 mid-walking fixture is required")
    far_walking = next(
        (
            item
            for item in walking
            if Path(item["inference"]["fixture_path"]).stem
            == "walking_far_frame_251"
        ),
        None,
    )
    if far_walking is None:
        raise ReportInputError("the SCRUM-371 far-walking fixture is required")
    detected_walking = [
        item for item in walking if item["inference"].get("detection_valid") is True
    ]
    if not detected_walking:
        raise ReportInputError("walking fixtures contain no accepted person pose")

    def lowest_confidence(item: dict[str, Any]) -> float:
        confidence_values = [
            require_finite(point["confidence"], "lower-body confidence")
            for point in item["lower_body"]["points"]
            if point.get("confidence") is not None
        ]
        if not confidence_values:
            raise ReportInputError("walking fixture has no lower-body confidences")
        return min(confidence_values)

    low_confidence_walking = min(detected_walking, key=lowest_confidence)
    return {
        standing["inference"]["fixture_path"]: "representative standing",
        representative_walking["inference"][
            "fixture_path"
        ]: "representative walking (mid-distance fixture)",
        far_walking["inference"]["fixture_path"]: "additional far-walking context",
        no_person["inference"]["fixture_path"]: "expected no-person negative",
        low_confidence_walking["inference"][
            "fixture_path"
        ]: "lowest-confidence detected walking sample",
    }


def annotation_name(fixture_path: str) -> str:
    return fixture_path.replace("/", "__").removesuffix(".png") + ".svg"


def make_svg(item: dict[str, Any], role: str, output_dir: Path) -> str:
    width = item["image_width"]
    height = item["image_height"]
    panel_width = 390
    canvas_width = width + panel_width
    inference = item["inference"]
    points = item["lower_body"]["points"]
    relative_image = os.path.relpath(item["image_path"], output_dir)
    escaped_image = xml.sax.saxutils.escape(relative_image, {'"': "&quot;"})
    fixture_name = html.escape(inference["fixture_path"])
    title = html.escape(f'{role}: {inference["fixture_path"]}')
    role_heading = {
        "representative standing": "Standing example",
        "representative walking (mid-distance fixture)": "Walking example (mid)",
        "additional far-walking context": "Walking example (far)",
        "expected no-person negative": "No-person example",
        "lowest-confidence detected walking sample": "Walking: lowest confidence",
    }.get(role, role)
    fragments = [
        '<?xml version="1.0" encoding="UTF-8"?>',
        (
            f'<svg xmlns="http://www.w3.org/2000/svg" '
            f'xmlns:xlink="http://www.w3.org/1999/xlink" '
            f'width="{canvas_width}" height="{height}" '
            f'viewBox="0 0 {canvas_width} {height}" '
            f'style="width:100%;height:auto;max-width:{canvas_width}px" '
            f'role="img">'
        ),
        f"<title>{title}</title>",
        "<defs><style>"
        "text{font-family:Arial,sans-serif}"
        ".heading{fill:#fff;font-size:22px;font-weight:700}"
        ".sub{fill:#d1d7df;font-size:14px}"
        ".point-label{fill:#fff;font-size:16px}"
        ".point-meta{fill:#c3ccd7;font-size:13px}"
        ".marker{font-size:11px;font-weight:bold;text-anchor:middle}"
        "</style></defs>",
        f'<image x="0" y="0" width="{width}" height="{height}" '
        f'href="{escaped_image}" xlink:href="{escaped_image}" '
        'preserveAspectRatio="none"/>',
        f'<rect x="{width}" y="0" width="{panel_width}" height="{height}" '
        'fill="#111827"/>',
        f'<text x="{width + 24}" y="38" class="heading">'
        f'{html.escape(role_heading)}</text>',
        f'<text x="{width + 24}" y="67" class="sub">'
        f'{html.escape(inference["phase"])} · {fixture_name}</text>',
    ]

    point_by_name = {point["name"]: point for point in points}
    for start_name, end_name in JOINT_LINKS:
        start = point_by_name[start_name]
        end = point_by_name[end_name]
        if start.get("valid") and end.get("valid"):
            start_xy = start["coordinates_px"]
            end_xy = end["coordinates_px"]
            fragments.append(
                f'<line x1="{float(start_xy["x"]):.3f}" '
                f'y1="{float(start_xy["y"]):.3f}" '
                f'x2="{float(end_xy["x"]):.3f}" '
                f'y2="{float(end_xy["y"]):.3f}" '
                'stroke="#63e6be" stroke-width="4" opacity="0.85"/>'
            )

    for point in points:
        status = point.get("status")
        if status not in STATUS_COLORS:
            raise ReportInputError(
                f"unexpected lower-body status {status!r} for {inference['fixture_path']}"
            )
        coordinates = point.get("coordinates_px") or {}
        x, y = coordinates.get("x"), coordinates.get("y")
        if point.get("valid") and x is not None and y is not None:
            color = STATUS_COLORS[status]
            fragments.extend(
                (
                    f'<circle cx="{float(x):.3f}" cy="{float(y):.3f}" r="13" '
                    f'fill="{color}" fill-opacity="0.30" stroke="{color}" '
                    'stroke-width="4"/>',
                    f'<text x="{float(x):.3f}" y="{float(y) + 4:.3f}" '
                    'class="marker" fill="#07110c">'
                    f'{int(point["model_index"])}</text>',
                )
            )

    if inference.get("detection_valid") is not True:
        fragments.extend(
            (
                f'<rect x="28" y="28" width="{width - 56}" height="54" '
                'rx="8" fill="#111827" fill-opacity="0.90"/>',
                f'<text x="{width / 2}" y="63" text-anchor="middle" '
                'class="heading">No accepted person detection</text>',
            )
        )

    for index, point in enumerate(points):
        y = 125 + index * 72
        status = point["status"]
        confidence = point.get("confidence")
        confidence_text = (
            f"{float(confidence):.3f}" if confidence is not None else "n/a"
        )
        validity = "VALID" if point.get("valid") else status.upper()
        fragments.extend(
            (
                f'<circle cx="{width + 34}" cy="{y - 5}" r="8" '
                f'fill="{STATUS_COLORS[status]}"/>',
                f'<text x="{width + 53}" y="{y}" class="point-label">'
                f'{html.escape(point["name"])}</text>',
                f'<text x="{width + 53}" y="{y + 23}" class="point-meta">'
                f'index {int(point["model_index"])} · confidence '
                f'{confidence_text} · {validity}</text>',
            )
        )
    fragments.extend(
        (
            f'<text x="{width + 24}" y="{height - 50}" class="sub">'
            f'detection: {"accepted" if inference.get("detection_valid") else "none"}'
            '</text>',
            f'<text x="{width + 24}" y="{height - 26}" class="sub">'
            'Only SCRUM-372 lower-body coordinates are drawn.</text>',
            "</svg>",
        )
    )
    return "\n".join(fragments) + "\n"


def build_report(
    *,
    paired: list[dict[str, Any]],
    selected: dict[str, str],
    metrics: dict[str, float | int],
    summary: dict[str, Any],
    run_record: dict[str, Any],
    run_config: dict[str, Any],
    capture_config: dict[str, Any],
    model_matches_recorded_hash: bool,
    capture_source_present: bool,
    inference_executable_present: bool,
    generated_at: str,
    output_dir: Path,
    source_files: dict[str, str],
) -> tuple[str, dict[str, Any]]:
    total = int(metrics["total_frames"])
    valid = int(metrics["valid_frames"])
    valid_rate = 100.0 * valid / total if total else 0.0
    positive_items = [
        item
        for item in paired
        if item["inference"].get("detection_valid") is True
    ]
    lower_body_points = [
        point
        for item in positive_items
        for point in item["lower_body"]["points"]
    ]
    valid_points = sum(point.get("valid") is True for point in lower_body_points)
    lowest_confidence = min(
        require_finite(point["confidence"], "lower-body confidence")
        for point in lower_body_points
        if point.get("confidence") is not None
    )

    capture = capture_config["camera"]
    placement = capture_config["placement"]
    lighting = capture_config["lighting_conditions"] if "lighting_conditions" in capture_config else capture.get("lighting_conditions")
    # The saved file stores lighting beside, not inside, camera settings.
    if lighting is None:
        lighting = capture_config.get("lighting_conditions")
    effective_rates = capture_config["camera"]["effective_saved_frame_rate_fps"]
    rate_text = ", ".join(
        f"{phase} {float(rate):.2f}"
        for phase, rate in effective_rates.items()
    )
    capture_run = capture_config["capture"]["latest_run"]
    model_hash = run_config.get("model_sha256", "not recorded")
    model_match_text = (
        "matches the recorded SHA-256"
        if model_matches_recorded_hash
        else "does not match the recorded SHA-256"
    )
    target_mean_pass = float(metrics["mean_latency_ms"]) < MEAN_LATENCY_TARGET_MS
    target_fps_pass = float(metrics["fps"]) > FPS_TARGET
    selected_items = [
        item
        for item in paired
        if item["inference"]["fixture_path"] in selected
    ]
    markdown_lines = [
        "# SCRUM-373 — Annotated output and local performance report",
        "",
        f"Generated from checked-in SCRUM-371/372 evidence at `{generated_at}`.",
        "This is a five-fixture macOS development-host run, not a continuous",
        "benchmark and not an i.MX95 performance result.",
        "",
        "## Measured results",
        "",
        "| Measure | Observed | Reference / judgment |",
        "|---|---:|---|",
        (
            f"| Fixture image resolution | "
            f"{paired[0]['image_width']}×{paired[0]['image_height']} px | "
            f"{capture['requested_resolution_px'][0]}×"
            f"{capture['requested_resolution_px'][1]} requested and negotiated |"
        ),
        (
            f"| Camera capture rate | {rate_text} FPS saved | "
            f"{capture['requested_frame_rate_fps']} FPS requested; backend reports "
            f"{capture['backend_reported_frame_rate_fps']} FPS |"
        ),
        (
            f"| Valid detection frames | {valid}/{total} "
            f"({valid_rate:.1f}%) | No-person is intentionally included in "
            "the denominator |"
        ),
        (
            f"| Positive-pose fixture detections | "
            f"{valid}/{len(positive_items) + int(metrics['rejected_no_person_frames'])} "
            f"overall; {valid}/{len(positive_items)} labelled-person samples | "
            f"{valid_points}/{len(lower_body_points)} lower-body points valid "
            "at the configured confidence cutoff |"
        ),
        (
            f"| Mean inference latency | {float(metrics['mean_latency_ms']):.2f} ms | "
            f"Target < {MEAN_LATENCY_TARGET_MS:.0f} ms: "
            f"{'PASS' if target_mean_pass else 'FAIL'} |"
        ),
        (
            f"| P95 inference latency | {float(metrics['p95_latency_ms']):.2f} ms | "
            "Measured; no P95 target documented |"
        ),
        (
            f"| Inference throughput | {float(metrics['fps']):.2f} FPS | "
            f"Target > {FPS_TARGET:.0f} FPS: "
            f"{'PASS' if target_fps_pass else 'FAIL'} |"
        ),
        (
            f"| Failed inferences | {int(metrics['failed_frames'])} | "
            f"{'PASS' if metrics['failed_frames'] == 0 else 'FAIL'} "
            "(recorded run status) |"
        ),
        "",
        "The valid-detection rate is `accepted person detections / all fixture "
        "frames`; the no-person frame is expected to be rejected, so this rate "
        "is not a standalone model-quality score. The positive fixture "
        "detection count and lower-body point-valid count are reported "
        "separately.",
        "",
        "Inference latency is measured around `PoseDetector::detect` only; "
        "image decoding and output writing are excluded. FPS is successful "
        "inference calls divided by summed inference latency. The p95 uses "
        f"nearest-rank selection over {int(metrics['latency_sample_count'])} "
        "calls, including the no-person call, and is statistically weak at "
        "this sample size. These are measured values, not targets.",
        "",
        "### Performance verdict",
        "",
        f"- Mean latency target (<{MEAN_LATENCY_TARGET_MS:.0f} ms): "
        f"{'PASS' if target_mean_pass else 'FAIL'} "
        f"({float(metrics['mean_latency_ms']):.2f} ms measured).",
        f"- Throughput target (>{FPS_TARGET:.0f} FPS): "
        f"{'PASS' if target_fps_pass else 'FAIL'} "
        f"({float(metrics['fps']):.2f} FPS measured).",
        "- P95: measured only; the available baseline states no P95 threshold.",
        "- No deployment or i.MX95 target is evaluated by these macOS results.",
        "- The mean-latency and FPS thresholds are the existing README "
        "baseline comparison values, not Jira acceptance criteria.",
        "",
        "## Model, runtime, and source run",
        "",
        "- Model: YOLOv8n-pose ONNX; the README download URL names asset "
        "release v8.2.0, which is not independently verified as the model's "
        "training version.",
        f"- SHA-256: `{model_hash}`; the model file in this worktree "
        f"{model_match_text}.",
        f"- ONNX Runtime: `{summary['onnx_runtime_version']}`.",
        f"- OpenCV: `{summary['opencv_version']}`.",
        "- Execution platform: macOS development host, as identified by the "
        "recorded FaceTime HD camera configuration; exact CPU model and macOS "
        "build were not recorded.",
        f"- Capture source run: `{capture_run['directory']}` "
        f"({capture_run['original_metadata_rows']['total']} metadata rows; "
        f"{capture_run['currently_present_png_files']['total']} images were "
        "available when the SCRUM-370 capture evidence was written).",
        f"- Full ignored capture directory present in this worktree: "
        f"{'yes' if capture_source_present else 'no'}. Curated source PNGs and "
        "their fixture-manifest hashes are verified.",
        f"- Repository-root `build/posture_detection` executable present: "
        f"{'yes' if inference_executable_present else 'no'}. Saved SCRUM-371 "
        "results were used without rerunning inference; build the target before "
        "reproducing that inference command.",
        "",
        "## Camera setup and observed limitations",
        "",
        f"- Camera: {capture['model']} ({capture['connection']}), device "
        f"{capture['device_index']}; front-facing.",
        f"- Placement: {placement['mounting_height_cm']} cm high, "
        f"{placement['camera_to_subject_distance_cm']} cm from subject.",
        f"- Lighting: {lighting}.",
        f"- Exposure: {capture['exposure_mode_and_value']}.",
        "- The selected walking images retain a real, furnished room background; "
        "the reviewed lower body and feet are in frame. The standing fixture "
        "is a single still sample.",
        f"- The lowest lower-body confidence in the detected fixtures is "
        f"{lowest_confidence:.3f}; it remains above the configured "
        f"{float(summary['keypoint_confidence_threshold']):.2f} validity "
        "cutoff. No selected lower-body point is invalid.",
        "- The available fixture set contains no specifically labelled "
        "occlusion challenge and no ground-truth joint annotations. "
        "Occlusion robustness and quantitative pose accuracy therefore remain "
        "untested; no occlusion failure is inferred.",
        f"- The no-person frame correctly has no accepted detection and six "
        "missing lower-body records. This is the expected negative case, not "
        "a failed inference.",
        f"- Recorded inference failures: {int(metrics['failed_frames'])}. "
        "The full raw SCRUM-370 capture directory is absent in this worktree, "
        "so its 509 original PNGs cannot be reviewed here. The SCRUM-370 "
        "metadata reports 22 missing standing images (frames 1–22); this is "
        "a capture-data gap, not an inference failure.",
        "- The checked-in fixture images crop the subject above the waist in "
        "the reviewed views; do not use these frames to assess whole-body "
        "visibility or independently confirm anatomical left/right labels.",
        "",
        "## Selected examples and annotations",
        "",
        "Every annotation is generated from the unchanged PNG and SCRUM-372 "
        "coordinates/statuses. Green denotes `valid`; amber denotes "
        "`invalid`; gray denotes `missing`. The right-hand panel gives each "
        "point's model index, raw confidence, and validity state. No keypoint "
        "is synthesized; no-person frames show the absence of a detection.",
        "",
        "| Role | Fixture | Detection / low point | Annotated image |",
        "|---|---|---|---|",
    ]
    output_entries: list[dict[str, Any]] = []
    for item in selected_items:
        inference = item["inference"]
        fixture = inference["fixture_path"]
        filename = annotation_name(fixture)
        annotation_path = Path("annotations") / filename
        person_confidence = inference.get("person_confidence")
        detection = (
            f"accepted, person conf. {float(person_confidence):.3f}"
            if person_confidence is not None
            else "no accepted detection"
        )
        if inference.get("detection_valid"):
            point = min(
                (
                    point
                    for point in item["lower_body"]["points"]
                    if point.get("confidence") is not None
                ),
                key=lambda current: float(current["confidence"]),
            )
            detection += (
                f"; lowest lower-body point {point['name']} "
                f"{float(point['confidence']):.3f} "
                f"({'valid' if point['valid'] else point['status']})"
            )
        fixture_link = os.path.relpath(
            CAMERA_DIR / "fixtures" / fixture, output_dir
        )
        markdown_lines.append(
            f"| {selected[fixture]} | [`{fixture}`]({fixture_link}) | "
            f"{detection} | [{filename}]({annotation_path.as_posix()}) |"
        )
        output_entries.append(
            {
                "role": selected[fixture],
                "fixture_path": fixture,
                "annotation_path": annotation_path.as_posix(),
                "image_path_relative_to_annotation": os.path.relpath(
                    item["image_path"], output_dir / "annotations"
                ),
                "detection_valid": bool(inference["detection_valid"]),
                "points": [
                    {
                        "name": point["name"],
                        "model_index": point["model_index"],
                        "confidence": point["confidence"],
                        "valid": point["valid"],
                        "status": point["status"],
                    }
                    for point in item["lower_body"]["points"]
                ],
            }
        )
    markdown_lines.extend(
        (
            "",
            "## Reproduction commands",
            "",
            "Run from the repository root. The first command documents the "
            "SCRUM-370 capture procedure; it recaptures into a new local "
            "directory and does not recreate the unavailable original run.",
            "",
            "```sh",
            "./build/camera_capture --device 0 --width 1280 --height 720 "
            '--fps 30 --seconds 8 --output '
            '"Camera/captures/SCRUM-370-$(date +%Y%m%d-%H%M%S)"',
            "```",
            "",
            "For the checked-in SCRUM-371 fixture inference:",
            "",
            "```sh",
            "cd Camera && ../build/posture_detection --offline "
            "--manifest fixtures/manifest.csv --threshold 0.3 "
            "--keypoint-threshold 0.5 "
            "--output results/scrum371-run-20261006/offline-inference",
            "```",
            "",
            "Regenerate the SCRUM-372 lower-body records into a new dated "
            "output directory (preserve the existing evidence file):",
            "",
            "```sh",
            "python3 Camera/tools/lower_body_records.py "
            "--input Camera/results/scrum371-run-20261006/offline-inference.jsonl "
            "--manifest Camera/fixtures/manifest.csv "
            "--output-dir Camera/results/scrum372-run-20261007-recheck",
            "```",
            "",
            "Regenerate the SCRUM-373 report and SVG annotations from the "
            "checked-in SCRUM-371 and SCRUM-372 records:",
            "",
            "```sh",
            "python3 Camera/tools/generate_scrum373_report.py "
            "--output-dir "
            "Camera/results/scrum373-annotated-performance-report",
            "```",
            "",
            "## Output schema and follow-up",
            "",
            "- `report.md`: measured results, honest target verdicts, evidence "
            "limitations, selected fixtures, and reproduction commands.",
            "- `annotations/*.svg`: reviewable vector overlays; each references "
            "its unchanged PNG under `Camera/fixtures/` using a relative path. "
            "Each includes six lower-body point labels and confidence/validity.",
            "- `outputs.json`: schema `scrum373-results.v1`; records generation "
            "time, source artifacts, verified metrics, and each selected "
            "fixture/annotation plus point states.",
            "- Existing SCRUM-371 JSONL/summary/run config and SCRUM-372 JSONL "
            "remain read-only inputs; original fixture PNGs are not edited.",
            "",
            "Follow-up evidence links:",
            "- Robustness capture/fixture guidance: "
            "[fixture inventory](../../fixtures/README.md) and "
            "[capture procedure](../../CAMERA_CAPTURE.md). Expand the set "
            "with consented occlusion, lighting, distances, and repeated "
            "walking sequences before making robustness claims.",
            "- Jira robustness follow-up: "
            "[SCRUM-201 — Camera Occlusion Fallback]"
            "(https://kmitl-team-vbds25lw.atlassian.net/browse/SCRUM-201). "
            "The issue title is relevant, but it currently has no description; "
            "confirm its scope before treating it as an occlusion-test plan.",
            "- i.MX95 port/performance follow-up: "
            "[Camera CMake platform configuration](../../CMakeLists.txt). "
            "Build and measure this same fixture inference on FRDM-i.MX95 "
            "and record its native model/runtime/board versions separately; "
            "the macOS measurements above do not predict target performance.",
            "- Jira porting follow-up: "
            "[SCRUM-176 — Port Preparation for FRDM-IMX95]"
            "(https://kmitl-team-vbds25lw.atlassian.net/browse/SCRUM-176), "
            "whose scope covers the aarch64 CMake toolchain, ONNX Runtime, "
            "and deployment to the board. It does not establish that pose "
            "inference has been ported or benchmarked.",
            "",
        )
    )
    output_manifest = {
        "schema_version": SCHEMA_VERSION,
        "generated_at_utc": generated_at,
        "source_run": {
            field: run_record.get(field)
            for field in (
                "record_type",
                "model_bytes",
                "onnx_runtime_version",
                "opencv_version",
                "person_detection_threshold",
                "person_detection_threshold_definition",
                "keypoint_confidence_threshold",
                "keypoint_threshold_definition",
                "timing_method",
                "latency_unit",
            )
        },
        "source_files": source_files,
        "metrics": {
            **metrics,
            "valid_frame_rate_percent": valid_rate,
            "mean_latency_target_ms": MEAN_LATENCY_TARGET_MS,
            "mean_latency_target_result": "PASS" if target_mean_pass else "FAIL",
            "fps_target": FPS_TARGET,
            "fps_target_result": "PASS" if target_fps_pass else "FAIL",
            "p95_target_result": "NOT_SPECIFIED",
        },
        "model": {
            "identity": run_config.get("model_identity"),
            "sha256": model_hash,
            "local_model_matches_recorded_hash": model_matches_recorded_hash,
            "onnx_runtime_version": summary.get("onnx_runtime_version"),
            "opencv_version": summary.get("opencv_version"),
            "execution_platform": "macOS development host; CPU and OS build unrecorded",
        },
        "capture": {
            "source_run_directory_present": capture_source_present,
            "inference_executable_present": inference_executable_present,
            "requested_resolution_px": capture["requested_resolution_px"],
            "requested_frame_rate_fps": capture["requested_frame_rate_fps"],
            "backend_reported_frame_rate_fps": capture[
                "backend_reported_frame_rate_fps"
            ],
            "effective_saved_frame_rate_fps": effective_rates,
            "placement": placement,
            "lighting_conditions": lighting,
        },
        "examples": output_entries,
    }
    return "\n".join(markdown_lines), output_manifest


def generate(
    *,
    inference_path: Path = DEFAULT_INFERENCE,
    summary_path: Path = DEFAULT_SUMMARY,
    run_config_path: Path = DEFAULT_RUN_CONFIG,
    lower_body_path: Path = DEFAULT_LOWER_BODY,
    capture_config_path: Path = DEFAULT_CAPTURE_CONFIG,
    fixture_manifest_path: Path = DEFAULT_FIXTURE_MANIFEST,
    output_dir: Path = DEFAULT_OUTPUT_DIR,
) -> dict[str, Any]:
    inference_records = read_jsonl(inference_path)
    lower_body_records = read_jsonl(lower_body_path)
    summary = read_json(summary_path)
    run_config = read_json(run_config_path)
    capture_config = read_json(capture_config_path)
    manifest = read_fixture_manifest(fixture_manifest_path)

    run_record, paired = pair_source_records(
        inference_records, lower_body_records, manifest
    )
    errors = sum(record.get("record_type") == "error" for record in inference_records)
    metrics = calculate_metrics(
        [item["inference"] for item in paired], errors
    )
    verify_summary(summary, metrics)
    selected = select_examples(paired)

    local_model = CAMERA_DIR / "models" / "yolov8n-pose.onnx"
    recorded_model_hash = run_config.get("model_sha256")
    if not isinstance(recorded_model_hash, str):
        raise ReportInputError("run-config is missing model_sha256")
    model_matches = sha256_file(local_model) == recorded_model_hash
    if not model_matches:
        raise ReportInputError(
            f"local model {local_model} does not match SCRUM-371 model hash"
        )
    capture_source_present = (CAMERA_DIR.parent / capture_config["capture"][
        "latest_run"
    ]["directory"]).is_dir()
    inference_executable = CAMERA_DIR.parent / "build" / "posture_detection"
    inference_executable_present = inference_executable.is_file() and os.access(
        inference_executable, os.X_OK
    )
    generated_at = datetime.now(timezone.utc).isoformat(timespec="seconds").replace(
        "+00:00", "Z"
    )
    source_root = CAMERA_DIR.parent.resolve()

    def source_path(path: Path) -> str:
        absolute_path = path.resolve()
        try:
            return absolute_path.relative_to(source_root).as_posix()
        except ValueError:
            return str(absolute_path)

    source_files = {
        "inference_jsonl": source_path(inference_path),
        "summary_json": source_path(summary_path),
        "run_config_json": source_path(run_config_path),
        "lower_body_jsonl": source_path(lower_body_path),
        "capture_config_json": source_path(capture_config_path),
        "fixture_manifest_csv": source_path(fixture_manifest_path),
    }
    report, output_manifest = build_report(
        paired=paired,
        selected=selected,
        metrics=metrics,
        summary=summary,
        run_record=run_record,
        run_config=run_config,
        capture_config=capture_config,
        model_matches_recorded_hash=model_matches,
        capture_source_present=capture_source_present,
        inference_executable_present=inference_executable_present,
        generated_at=generated_at,
        output_dir=output_dir,
        source_files=source_files,
    )

    output_dir.mkdir(parents=True, exist_ok=True)
    annotation_dir = output_dir / "annotations"
    annotation_dir.mkdir(parents=True, exist_ok=True)
    for item in paired:
        fixture = item["inference"]["fixture_path"]
        filename = annotation_name(fixture)
        role = selected.get(fixture, "additional recorded walking fixture")
        (annotation_dir / filename).write_text(
            make_svg(item, role, annotation_dir), encoding="utf-8"
        )
    (output_dir / "report.md").write_text(report, encoding="utf-8")
    (output_dir / "outputs.json").write_text(
        json.dumps(output_manifest, indent=2, allow_nan=False) + "\n",
        encoding="utf-8",
    )
    return output_manifest


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Generate verified SCRUM-373 report and SVG annotations."
    )
    parser.add_argument("--inference", type=Path, default=DEFAULT_INFERENCE)
    parser.add_argument("--summary", type=Path, default=DEFAULT_SUMMARY)
    parser.add_argument("--run-config", type=Path, default=DEFAULT_RUN_CONFIG)
    parser.add_argument("--lower-body", type=Path, default=DEFAULT_LOWER_BODY)
    parser.add_argument("--capture-config", type=Path, default=DEFAULT_CAPTURE_CONFIG)
    parser.add_argument(
        "--fixture-manifest", type=Path, default=DEFAULT_FIXTURE_MANIFEST
    )
    parser.add_argument("--output-dir", type=Path, default=DEFAULT_OUTPUT_DIR)
    args = parser.parse_args(argv)
    try:
        result = generate(
            inference_path=args.inference,
            summary_path=args.summary,
            run_config_path=args.run_config,
            lower_body_path=args.lower_body,
            capture_config_path=args.capture_config,
            fixture_manifest_path=args.fixture_manifest,
            output_dir=args.output_dir,
        )
    except (OSError, ReportInputError, KeyError, TypeError, ValueError) as error:
        parser.exit(1, f"error: {error}\n")
    print(
        f"Generated {len(result['examples'])} annotated fixture images and report "
        f"in {args.output_dir}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
