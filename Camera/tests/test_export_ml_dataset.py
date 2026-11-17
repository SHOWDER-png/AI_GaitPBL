"""Tests for Camera/tools/export_ml_dataset.py (SCRUM-213). Standard library only.

    python3 -B -m unittest Camera/tests/test_export_ml_dataset.py -v
"""
import contextlib
import csv
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "Camera" / "tools"))

import export_ml_dataset as ex  # noqa: E402

SYNTHETIC = ROOT / "Integration" / "fixtures" / "synthetic"
HEADER = ",".join(ex.cv.GAIT_COLUMNS)


def read_rows(path):
    with Path(path).open(encoding="utf-8", newline="") as source:
        return list(csv.DictReader(source))


def gait_row(t, l_knee=170.0, r_knee=171.0, l_hip=-1, r_hip=-1, sym=2.0,
             phase="STANDING", lc=0.01, rc=0.02, infer=80.0):
    return f"{t},{l_knee},{r_knee},{l_hip},{r_hip},{sym},{phase},{phase},{phase},{lc},{rc},{infer}"


class ExportTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def write(self, name, lines):
        path = self.dir / name
        path.write_text("\n".join(lines) + "\n", encoding="utf-8")
        return path

    def run_export(self, *paths, extra=()):
        out = self.dir / "out" / "ml.csv"
        stdout, stderr = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
            code = ex.main([*map(str, paths), "--output", str(out), *extra])
        return code, out, stdout.getvalue() + stderr.getvalue()

    # ── normal paths ──────────────────────────────────────────
    def test_valid_synthetic_session_exports(self):
        code, out, log = self.run_export(SYNTHETIC / "gait_valid.csv")
        self.assertEqual(code, 0, log)
        self.assertEqual(ex.validate_ml_csv(out), [])
        rows = read_rows(out)
        source = read_rows(SYNTHETIC / "gait_valid.csv")
        self.assertEqual(len(rows), len(source))
        self.assertIn("30.03 Hz", log)          # 33.3 ms interval
        meta = json.loads(out.with_suffix(".meta.json").read_text(encoding="utf-8"))
        self.assertEqual(meta["schema_version"], ex.SCHEMA_VERSION)
        self.assertEqual(meta["rows"], len(rows))
        self.assertEqual(len(meta["sessions"][0]["source_sha256"]), 64)

    def test_not_visible_becomes_empty_with_valid_flag_zero(self):
        path = self.write("s.csv", [HEADER] + [gait_row(i * 50, l_knee=-1, sym=-1)
                                               for i in range(20)])
        code, out, log = self.run_export(path)
        self.assertEqual(code, 0, log)
        row = read_rows(out)[0]
        self.assertEqual(row["l_knee_deg"], "")
        self.assertEqual(row["l_knee_valid"], "0")
        self.assertEqual(row["r_knee_deg"], "171.0000")
        self.assertEqual(row["r_knee_valid"], "1")
        self.assertEqual(row["symmetry_valid"], "0")
        self.assertEqual(row["dt_ms"], "")       # first sample has no interval

    def test_phase_ids_and_label(self):
        lines = [HEADER] + [gait_row(i * 50, phase=p)
                            for i, p in enumerate(["STANDING", "SEMI_SQUAT", "DEEP_SQUAT"] * 5)]
        code, out, log = self.run_export(self.write("s.csv", lines), extra=["--label", "normal"])
        self.assertEqual(code, 0, log)
        rows = read_rows(out)
        self.assertEqual([r["squat_phase_id"] for r in rows[:3]], ["0", "1", "2"])
        self.assertTrue(all(r["label"] == "normal" for r in rows))

    def test_two_sessions_are_concatenated_with_own_ids(self):
        a = self.write("a.csv", [HEADER] + [gait_row(i * 50) for i in range(12)])
        b = self.write("b.csv", [HEADER] + [gait_row(i * 50) for i in range(15)])
        code, out, log = self.run_export(a, b)
        self.assertEqual(code, 0, log)
        rows = read_rows(out)
        self.assertEqual(len(rows), 27)
        self.assertEqual(len({r["session_id"] for r in rows}), 2)
        self.assertEqual(ex.validate_ml_csv(out), [])   # time restarts per session: allowed

    def test_gap_is_a_warning_not_a_rejection(self):
        times = [i * 50 for i in range(10)] + [450 + 1000 + i * 50 for i in range(10)]
        code, out, log = self.run_export(self.write("s.csv", [HEADER] + [gait_row(t) for t in times]))
        self.assertEqual(code, 0, log)
        self.assertIn("1 gap(s)", log)

    # ── failure paths: nothing is written ─────────────────────
    def assert_rejected(self, path, expected):
        code, out, log = self.run_export(path)
        self.assertEqual(code, 1, log)
        self.assertIn(expected, log)
        self.assertFalse(out.exists(), "a rejected export must not write output")

    def test_empty_file_rejected(self):
        self.assert_rejected(self.write("blank.csv", []), "header must be")
        self.assert_rejected(self.write("header_only.csv", [HEADER]), "only 0 samples")
        empty = self.dir / "empty.csv"
        empty.write_bytes(b"")
        self.assert_rejected(empty, "file is empty")

    def test_missing_file_rejected(self):
        self.assert_rejected(self.dir / "nope.csv", "file not found")

    def test_contract_violations_rejected(self):
        for name, expected in (("gait_bad_header.csv", "header must be"),
                               ("gait_bad_phase.csv", "squat_phase"),
                               ("gait_time_backwards.csv", "goes backwards"),
                               ("gait_knee_out_of_range.csv", "0..180"),
                               ("gait_compress_out_of_range.csv", "within 0..1")):
            with self.subTest(name):
                self.assert_rejected(SYNTHETIC / name, expected)

    def test_non_numeric_rejected(self):
        lines = [HEADER] + [gait_row(i * 50) for i in range(12)]
        lines[5] = lines[5].replace("170.0", "abc", 1)
        self.assert_rejected(self.write("s.csv", lines), "not a number")

    def test_low_sampling_rate_rejected(self):
        lines = [HEADER] + [gait_row(i * 500) for i in range(12)]    # 2 Hz
        self.assert_rejected(self.write("s.csv", lines), "below the 5.0 Hz minimum")

    def test_duplicate_timestamps_rejected(self):
        lines = [HEADER] + [gait_row(i * 50) for i in range(12)] + [gait_row(550)]
        self.assert_rejected(self.write("s.csv", lines), "repeated ts_ms")

    def test_too_few_samples_rejected(self):
        lines = [HEADER] + [gait_row(i * 50) for i in range(3)]
        self.assert_rejected(self.write("s.csv", lines), "only 3 samples")

    def test_same_session_twice_rejected(self):
        path = self.write("s.csv", [HEADER] + [gait_row(i * 50) for i in range(12)])
        code, out, log = self.run_export(path, path)
        self.assertEqual(code, 1)
        self.assertIn("given twice", log)

    def test_label_with_comma_is_usage_error(self):
        path = self.write("s.csv", [HEADER] + [gait_row(i * 50) for i in range(12)])
        code, _, _ = self.run_export(path, extra=["--label", "a,b"])
        self.assertEqual(code, 2)

    # ── the schema checker itself catches broken files ────────
    def test_validator_catches_flag_mismatch_and_time(self):
        path = self.write("s.csv", [HEADER] + [gait_row(i * 50) for i in range(12)])
        code, out, _ = self.run_export(path)
        self.assertEqual(code, 0)
        rows = out.read_text(encoding="utf-8").splitlines()
        broken = rows[1].split(",")
        broken[ex.ML_HEADER.index("l_knee_valid")] = "0"
        rows[1] = ",".join(broken)
        rows.append(rows[2])                                 # repeated t_ms
        out.write_text("\n".join(rows) + "\n", encoding="utf-8")
        errors = ex.validate_ml_csv(out)
        self.assertTrue(any("disagrees" in e for e in errors), errors)
        self.assertTrue(any("must increase" in e for e in errors), errors)


if __name__ == "__main__":
    unittest.main()
