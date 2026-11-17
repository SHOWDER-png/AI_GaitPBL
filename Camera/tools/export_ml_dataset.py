#!/usr/bin/env python3
"""Export camera gait logs to the ML training schema (SCRUM-213).

Reads one or more `gait_data.csv` session logs written by `posture_detection`
(interface I4, Integration/INTERFACE_CONTRACTS.md), validates them with the
reference validator, checks the sampling rate, and writes one ML-ready CSV in
schema `camera-gait-ml.v1` plus a JSON sidecar describing the run.

    python3 -B Camera/tools/export_ml_dataset.py SESSION.csv [SESSION.csv ...] \
        --output Camera/results/ml-export/camera_gait_ml.csv

Standard library only. Exit codes: 0 = exported, 1 = rejected (nothing written),
2 = usage error. A session is rejected, never repaired: a bad header, a non-numeric
value, time going backwards or a sampling rate below the minimum stops the export
with the reason, because silently fixing training data hides capture bugs.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import statistics
import sys
from datetime import datetime, timezone
from pathlib import Path

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "Integration" / "tools"))

import contract_validators as cv  # noqa: E402

SCHEMA_VERSION = "camera-gait-ml.v1"

# Output columns, in order, with their type. "float" cells are empty when the
# value was not measured; the matching *_valid column says why the cell is empty.
ML_COLUMNS: list[tuple[str, str]] = [
    ("session_id", "str"),
    ("sample_index", "int"),
    ("t_ms", "float"),
    ("dt_ms", "float"),
    ("l_knee_deg", "float"),
    ("r_knee_deg", "float"),
    ("l_hip_deg", "float"),
    ("r_hip_deg", "float"),
    ("symmetry_pct", "float"),
    ("l_compress", "float"),
    ("r_compress", "float"),
    ("l_knee_valid", "int01"),
    ("r_knee_valid", "int01"),
    ("l_hip_valid", "int01"),
    ("r_hip_valid", "int01"),
    ("symmetry_valid", "int01"),
    ("squat_phase", "category"),
    ("squat_phase_id", "int"),
    ("infer_ms", "float"),
    ("label", "str"),
]
ML_HEADER = [name for name, _ in ML_COLUMNS]
SQUAT_PHASE_IDS = {"STANDING": 0, "SEMI_SQUAT": 1, "DEEP_SQUAT": 2}
NOT_VISIBLE = -1.0

# Sampling-rate rules. The camera runs at ~10 FPS on the M1 baseline (README) and
# targets 15+ FPS; below 5 Hz the per-frame gait features are too sparse to train on.
DEFAULT_MIN_RATE_HZ = 5.0
DEFAULT_GAP_FACTOR = 3.0     # an interval > 3x the median interval is a gap (dropped frames)
DEFAULT_MIN_SAMPLES = 10


class ExportError(Exception):
    """A session breaks the contract or the sampling rules."""


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1 << 16), b""):
            digest.update(block)
    return digest.hexdigest()


def sampling_stats(times_ms: list[float], gap_factor: float) -> dict:
    intervals = [b - a for a, b in zip(times_ms, times_ms[1:])]
    positive = [i for i in intervals if i > 0]
    if not positive:
        return {"samples": len(times_ms), "duration_ms": 0.0, "median_interval_ms": None,
                "rate_hz": None, "jitter_ms": None, "gaps": 0, "duplicate_timestamps":
                len(intervals) - len(positive)}
    median = statistics.median(positive)
    gaps = [i for i in positive if i > gap_factor * median]
    return {
        "samples": len(times_ms),
        "duration_ms": round(times_ms[-1] - times_ms[0], 3),
        "median_interval_ms": round(median, 3),
        "rate_hz": round(1000.0 / median, 3),
        "jitter_ms": round(statistics.pstdev(positive), 3),
        "gaps": len(gaps),
        "longest_gap_ms": round(max(gaps), 3) if gaps else None,
        "duplicate_timestamps": len(intervals) - len(positive),
    }


def read_session(path: Path, args: argparse.Namespace) -> tuple[list[dict], dict]:
    if not path.is_file():
        raise ExportError(f"{path}: file not found")
    if path.stat().st_size == 0:
        raise ExportError(f"{path}: file is empty (no header) - was the session saved with Q?")
    errors, warnings = cv.validate_gait_csv(path)
    if errors:
        shown = "; ".join(errors[:5]) + (f"; ... {len(errors) - 5} more" if len(errors) > 5 else "")
        raise ExportError(f"{path}: contract I4 violated: {shown}")
    with path.open(encoding="utf-8", newline="") as source:
        rows = list(csv.DictReader(source))
    if len(rows) < args.min_samples:
        raise ExportError(f"{path}: only {len(rows)} samples, need at least {args.min_samples}")

    times = [float(row["ts_ms"]) for row in rows]
    stats = sampling_stats(times, args.gap_factor)
    if stats["duplicate_timestamps"]:
        raise ExportError(f"{path}: {stats['duplicate_timestamps']} repeated ts_ms value(s); "
                          "each sample needs its own timestamp")
    if stats["rate_hz"] is None or stats["rate_hz"] < args.min_rate_hz:
        raise ExportError(f"{path}: sampling rate {stats['rate_hz']} Hz is below the "
                          f"{args.min_rate_hz} Hz minimum")
    if stats["gaps"]:
        warnings.append(f"{stats['gaps']} gap(s) longer than {args.gap_factor}x the median "
                        f"interval (longest {stats['longest_gap_ms']} ms); dt_ms marks them")
    return rows, {"warnings": warnings, "sampling": stats}


def to_ml_rows(session_id: str, rows: list[dict], label: str) -> list[list[str]]:
    out: list[list[str]] = []
    previous = None
    for index, row in enumerate(rows):
        t_ms = float(row["ts_ms"])
        values = {}
        for src, dst in (("l_knee", "l_knee_deg"), ("r_knee", "r_knee_deg"),
                         ("l_hip", "l_hip_deg"), ("r_hip", "r_hip_deg"),
                         ("symmetry", "symmetry_pct")):
            number = float(row[src])
            values[dst] = None if number == NOT_VISIBLE else number
        phase = row["squat_phase"]
        out.append([
            session_id,
            str(index),
            f"{t_ms:.3f}",
            "" if previous is None else f"{t_ms - previous:.3f}",
            *("" if values[k] is None else f"{values[k]:.4f}"
              for k in ("l_knee_deg", "r_knee_deg", "l_hip_deg", "r_hip_deg", "symmetry_pct")),
            f"{float(row['l_compress']):.4f}",
            f"{float(row['r_compress']):.4f}",
            *("0" if values[k] is None else "1"
              for k in ("l_knee_deg", "r_knee_deg", "l_hip_deg", "r_hip_deg", "symmetry_pct")),
            phase,
            str(SQUAT_PHASE_IDS[phase]),
            f"{float(row['infer_ms']):.3f}",
            label,
        ])
        previous = t_ms
    return out


def validate_ml_csv(path: Path) -> list[str]:
    """Check an exported file against camera-gait-ml.v1 (used by tests and by consumers)."""
    errors: list[str] = []
    with path.open(encoding="utf-8", newline="") as source:
        reader = csv.reader(source)
        header = next(reader, None)
        if header != ML_HEADER:
            return [f"header must be {','.join(ML_HEADER)}"]
        last: dict[str, float] = {}
        for line, row in enumerate(reader, start=2):
            if len(row) != len(ML_HEADER):
                errors.append(f"line {line}: expected {len(ML_HEADER)} columns")
                continue
            cell = dict(zip(ML_HEADER, row))
            for name, kind in ML_COLUMNS:
                text = cell[name]
                if kind == "float" and text != "":
                    try:
                        if not math.isfinite(float(text)):
                            raise ValueError
                    except ValueError:
                        errors.append(f"line {line}: {name} is not a finite number")
                elif kind in ("int", "int01"):
                    if not text.isdigit() or (kind == "int01" and text not in ("0", "1")):
                        errors.append(f"line {line}: {name} must be {kind}")
            for value, flag in (("l_knee_deg", "l_knee_valid"), ("r_knee_deg", "r_knee_valid"),
                                ("l_hip_deg", "l_hip_valid"), ("r_hip_deg", "r_hip_valid"),
                                ("symmetry_pct", "symmetry_valid")):
                if (cell[value] != "") != (cell[flag] == "1"):
                    errors.append(f"line {line}: {flag} disagrees with {value}")
            if cell["squat_phase"] not in SQUAT_PHASE_IDS or \
                    cell["squat_phase_id"] != str(SQUAT_PHASE_IDS.get(cell["squat_phase"], -1)):
                errors.append(f"line {line}: squat_phase/squat_phase_id mismatch")
            try:
                t_ms = float(cell["t_ms"])
            except ValueError:
                continue
            session = cell["session_id"]
            if session in last and t_ms <= last[session]:
                errors.append(f"line {line}: t_ms must increase within session {session}")
            last[session] = t_ms
    return errors


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("sessions", nargs="+", type=Path, help="gait_data.csv files (I4)")
    parser.add_argument("--output", type=Path, required=True, help="ML CSV to write")
    parser.add_argument("--label", default="", help="label for every row (empty = unlabeled)")
    parser.add_argument("--min-rate-hz", type=float, default=DEFAULT_MIN_RATE_HZ)
    parser.add_argument("--gap-factor", type=float, default=DEFAULT_GAP_FACTOR)
    parser.add_argument("--min-samples", type=int, default=DEFAULT_MIN_SAMPLES)
    args = parser.parse_args(argv)

    if "," in args.label or "\n" in args.label:
        print("error: --label must not contain commas or newlines", file=sys.stderr)
        return 2

    seen: set[str] = set()
    all_rows: list[list[str]] = []
    sessions_meta = []
    try:
        for path in args.sessions:
            session_id = path.resolve().parent.name + "/" + path.stem
            if session_id in seen:
                raise ExportError(f"{path}: session id {session_id} given twice")
            seen.add(session_id)
            rows, meta = read_session(path, args)
            all_rows.extend(to_ml_rows(session_id, rows, args.label))
            sessions_meta.append({"session_id": session_id, "source": str(path),
                                  "source_sha256": sha256(path), **meta})
    except ExportError as error:
        print(f"REJECTED: {error}", file=sys.stderr)
        return 1

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", encoding="utf-8", newline="") as target:
        writer = csv.writer(target, lineterminator="\n")
        writer.writerow(ML_HEADER)
        writer.writerows(all_rows)
    problems = validate_ml_csv(args.output)
    if problems:   # self-check: should never happen; fail loudly if it does
        print("INTERNAL ERROR: exported file fails its own schema: " + problems[0],
              file=sys.stderr)
        return 1

    sidecar = args.output.with_suffix(".meta.json")
    sidecar.write_text(json.dumps({
        "schema_version": SCHEMA_VERSION,
        "generated_at_utc": datetime.now(timezone.utc).isoformat(timespec="seconds"),
        "columns": [{"name": n, "type": t} for n, t in ML_COLUMNS],
        "missing_value": "empty cell; see the matching *_valid column",
        "squat_phase_ids": SQUAT_PHASE_IDS,
        "time_base": "t_ms = ts_ms from posture_detection: ms since program start, per session",
        "rules": {"min_rate_hz": args.min_rate_hz, "gap_factor": args.gap_factor,
                  "min_samples": args.min_samples},
        "rows": len(all_rows),
        "sessions": sessions_meta,
    }, indent=2) + "\n", encoding="utf-8")

    for meta in sessions_meta:
        s = meta["sampling"]
        print(f"OK {meta['session_id']}: {s['samples']} samples, {s['rate_hz']} Hz "
              f"(jitter {s['jitter_ms']} ms, gaps {s['gaps']})")
        for warning in meta["warnings"]:
            print(f"  warning: {warning}")
    print(f"Exported {len(all_rows)} rows -> {args.output} (+ {sidecar.name})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
