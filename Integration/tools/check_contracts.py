#!/usr/bin/env python3
"""Check the repository against contracts/frozen_contracts.json (SCRUM-348).

Detects drift: if a producer changes a CSV header, a schema version or a recorded component
version without the contract being updated (and re-approved), this exits 1.

    python3 check_contracts.py [contracts.json]   # exit 0 = no drift, 1 = drift
    (a check is reported as SKIP when its data file is not present on this machine)
"""
from __future__ import annotations

import csv
import json
import re
import sys
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))

import contract_validators as cv  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
CONTRACTS = ROOT / "Integration" / "contracts" / "frozen_contracts.json"
results: list[tuple[str, str, str]] = []


def record(status: str, name: str, detail: str = "") -> None:
    results.append((status, name, detail))


def cpp_csv_header(source: Path, stream: str) -> list[str] | None:
    """Join the string literals of the first `stream << "..."` statement into a CSV header."""
    text = source.read_text(encoding="utf-8")
    start = re.search(re.escape(stream) + r"\s*<<\s*\"", text)
    if not start:
        return None
    statement = text[start.start():text.index(";", start.start())]
    literals = re.findall(r'"((?:[^"\\]|\\.)*)"', statement)
    header = "".join(literals).replace("\\n", "").strip()
    return header.split(",")


def compare(name: str, found, expected) -> None:
    if found == expected:
        record("PASS", name)
    else:
        record("FAIL", name, f"expected {expected}, found {found}")


def main() -> int:
    contracts_path = Path(sys.argv[1]) if len(sys.argv) > 1 else CONTRACTS
    contract = json.loads(contracts_path.read_text(encoding="utf-8"))
    ifaces = contract["interfaces"]

    compare("gait_csv header (main.cpp)",
            cpp_csv_header(ROOT / "Camera/src/main.cpp", "csv"), ifaces["gait_csv"]["columns"])
    compare("gait_csv columns match reference validator", cv.GAIT_COLUMNS,
            ifaces["gait_csv"]["columns"])
    compare("frames_csv header (camera_capture.cpp)",
            cpp_csv_header(ROOT / "Camera/src/camera_capture.cpp", "metadata"),
            ifaces["frames_csv"]["columns"])
    compare("frames_csv columns match reference validator", cv.FRAMES_COLUMNS,
            ifaces["frames_csv"]["columns"])

    manifest = ROOT / "Camera/fixtures/manifest.csv"
    with manifest.open(encoding="utf-8", newline="") as source:
        compare("fixture manifest header", next(csv.reader(source)),
                ifaces["fixture_manifest_csv"]["columns"])

    producer = (ROOT / "Camera/tools/lower_body_records.py").read_text(encoding="utf-8")
    match = re.search(r'^SCHEMA_VERSION\s*=\s*"([^"]+)"', producer, re.MULTILINE)
    compare("lower_body schema_version (producer)", match.group(1) if match else None,
            ifaces["lower_body_record"]["schema_version"])
    compare("lower_body schema_version (validator)", cv.LOWER_BODY_SCHEMA,
            ifaces["lower_body_record"]["schema_version"])

    exporter = (ROOT / "Camera/tools/export_ml_dataset.py").read_text(encoding="utf-8")
    match = re.search(r'^SCHEMA_VERSION\s*=\s*"([^"]+)"', exporter, re.MULTILINE)
    compare("camera ML export schema_version", match.group(1) if match else None,
            ifaces["camera_gait_ml_csv"]["schema_version"])
    block = re.search(r"^ML_COLUMNS[^=]*=\s*\[(.*?)^\]", exporter, re.MULTILINE | re.DOTALL)
    compare("camera ML export columns",
            re.findall(r'\("(\w+)", "\w+"\)', block.group(1)) if block else None,
            ifaces["camera_gait_ml_csv"]["columns"])

    imu = (ROOT / "Nordic/firmware/experiments/imu_bmi270_c29/imu_config.yaml").read_text(encoding="utf-8")
    columns = re.search(r'^csv_columns:\s*"([^"]+)"', imu, re.MULTILINE)
    compare("imu_csv columns (imu_config.yaml)",
            [c.strip() for c in columns.group(1).split(",")] if columns else None,
            ifaces["imu_csv"]["columns"])
    compare("imu_csv columns match reference validator", cv.IMU_COLUMNS,
            ifaces["imu_csv"]["columns"])
    rate = re.search(r"^\s*target_data_rate_hz:\s*(\d+)", imu, re.MULTILINE)
    compare("imu target rate (Hz)", int(rate.group(1)) if rate else None,
            ifaces["imu_csv"]["target_rate_hz"])
    for label, key in (("accelerometer", "accelerometer_range"), ("gyroscope", "gyroscope_range")):
        found = re.search(rf"^\s*{label}:\s*\n\s*range:\s*\"([^\"]+)\"", imu, re.MULTILINE)
        compare(f"imu {label} range", found.group(1) if found else None, ifaces["imu_csv"][key])

    summary = ROOT / "Camera/results/scrum371-run-20261006/offline-inference-summary.json"
    if summary.exists():
        data = json.loads(summary.read_text(encoding="utf-8"))
        macos = contract["components"]["macos_dev"]
        compare("macOS ONNX Runtime version", data.get("onnx_runtime_version"),
                macos["onnx_runtime_version"])
        compare("macOS OpenCV version", data.get("opencv_version"), macos["opencv_version"])
    else:
        record("SKIP", "macOS component versions", "offline-inference-summary.json not present")

    model = ROOT / contract["components"]["model"]["file"]
    if model.exists():
        compare("pose model size (bytes)", model.stat().st_size,
                contract["components"]["model"]["bytes"])
    else:
        record("SKIP", "pose model size", "model file not present")

    for path in sorted((ROOT / "Nordic/firmware/experiments/imu_bmi270_c29/data").glob("*.csv")):
        errors, _ = cv.validate_imu_csv(path)
        record("FAIL" if errors else "PASS", f"real IMU capture {path.name}",
               errors[0] if errors else "")
    for path in sorted((ROOT / "Camera/results").glob("scrum372-run-*/lower-body-records-*.jsonl")):
        errors, _ = cv.validate_lower_body_file(path)
        record("FAIL" if errors else "PASS", f"real lower-body records {path.name}",
               errors[0] if errors else "")
    for path in sorted((ROOT / "Camera/captures").glob("*/frames.csv")):
        errors, _ = cv.validate_frames_csv(path)
        record("FAIL" if errors else "PASS", f"real capture log {path.parent.name}",
               errors[0] if errors else "")

    failed = 0
    for status, name, detail in results:
        print(f"{status:4}  {name}" + (f"  ({detail})" if detail else ""))
        failed += status == "FAIL"
    print(f"\n{'NO DRIFT' if not failed else 'DRIFT DETECTED'}: "
          f"{sum(r[0] == 'PASS' for r in results)} passed, {failed} failed, "
          f"{sum(r[0] == 'SKIP' for r in results)} skipped")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
