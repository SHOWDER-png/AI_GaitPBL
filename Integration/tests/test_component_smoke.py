"""Component smoke tests (SCRUM-350). Standard library only, no hardware.

    python3 -B -m unittest discover -s Integration/tests -v        (from the repository root)
"""
import hashlib
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "Integration" / "tools"
SYNTHETIC = ROOT / "Integration" / "fixtures" / "synthetic"
sys.path.insert(0, str(TOOLS))

import contract_validators as cv  # noqa: E402
import make_synthetic_fixtures as generator  # noqa: E402


def run(*args):
    return subprocess.run([sys.executable, "-B", *map(str, args)], capture_output=True,
                          text=True, cwd=ROOT)


def digest(directory: Path) -> dict:
    return {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
            for p in sorted(directory.iterdir()) if p.is_file()}


class SyntheticFixtureVerdicts(unittest.TestCase):
    """Every synthetic fixture must get exactly the verdict recorded in expected.json."""

    def test_every_fixture_matches_expected(self):
        expected = json.loads((SYNTHETIC / "expected.json").read_text(encoding="utf-8"))
        self.assertGreaterEqual(len(expected), 30)
        for name, rule in sorted(expected.items()):
            with self.subTest(fixture=name):
                errors, warnings = cv.VALIDATORS[rule["kind"]](SYNTHETIC / name)
                self.assertEqual(not errors, rule["valid"], f"{name}: {errors[:3]}")
                if rule["valid"] and "warnings" in rule:
                    self.assertEqual(len(warnings), rule["warnings"])
                if not rule["valid"]:
                    self.assertTrue(any(rule["error_contains"] in e for e in errors),
                                    f"{name}: expected an error containing "
                                    f"{rule['error_contains']!r}, got {errors[:3]}")

    def test_every_fixture_file_is_listed(self):
        expected = json.loads((SYNTHETIC / "expected.json").read_text(encoding="utf-8"))
        files = {p.name for p in SYNTHETIC.iterdir() if p.is_file()} - {"expected.json"}
        self.assertEqual(files, set(expected))

    def test_generator_is_deterministic_and_matches_committed_files(self):
        with tempfile.TemporaryDirectory() as first, tempfile.TemporaryDirectory() as second:
            generator.generate(Path(first))
            generator.generate(Path(second))
            self.assertEqual(digest(Path(first)), digest(Path(second)))
            self.assertEqual(digest(Path(first)), digest(SYNTHETIC),
                             "committed fixtures differ from the generator output")


class ValidatorEdgeCases(unittest.TestCase):
    def test_non_object_record(self):
        self.assertTrue(cv.validate_lower_body_record([1, 2]))
        self.assertTrue(cv.validate_lower_body_record(None))

    def test_good_record_has_no_errors(self):
        record = generator.lower_body_record(1, generator.random.Random(1))
        self.assertEqual(cv.validate_lower_body_record(record), [])

    def test_one_missing_field_is_reported(self):
        record = generator.lower_body_record(1, generator.random.Random(1))
        del record["source"]["frame_id"]
        self.assertTrue(any("source.frame_id" in e for e in cv.validate_lower_body_record(record)))

    def test_missing_file_raises(self):
        with self.assertRaises(OSError):
            cv.validate_imu_csv(SYNTHETIC / "does_not_exist.csv")


class ContractDrift(unittest.TestCase):
    def test_repository_matches_frozen_contracts(self):
        result = run(TOOLS / "check_contracts.py")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("NO DRIFT", result.stdout)

    def test_a_changed_contract_is_detected(self):
        contract = json.loads((ROOT / "Integration/contracts/frozen_contracts.json")
                              .read_text(encoding="utf-8"))
        contract["interfaces"]["gait_csv"]["columns"].append("new_column")
        contract["interfaces"]["lower_body_record"]["schema_version"] = "lower-body-record.v2"
        with tempfile.TemporaryDirectory() as folder:
            changed = Path(folder) / "changed.json"
            changed.write_text(json.dumps(contract), encoding="utf-8")
            result = run(TOOLS / "check_contracts.py", changed)
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("DRIFT DETECTED", result.stdout)
        self.assertIn("FAIL  gait_csv header", result.stdout)
        self.assertIn("FAIL  lower_body schema_version", result.stdout)


class CameraToReceiverSmoke(unittest.TestCase):
    """Real camera-side producer output must satisfy the contract the receiver will enforce."""

    def test_lower_body_producer_output_is_valid(self):
        inference = ROOT / "Camera/results/scrum371-run-20261006/offline-inference.jsonl"
        manifest = ROOT / "Camera/fixtures/manifest.csv"
        if not inference.exists() or not manifest.exists():
            self.skipTest("offline inference results not present on this machine")
        with tempfile.TemporaryDirectory() as folder:
            result = run(ROOT / "Camera/tools/lower_body_records.py", "--input", inference,
                         "--manifest", manifest, "--output-dir", folder)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            produced = list(Path(folder).glob("lower-body-records-*.jsonl"))
            self.assertEqual(len(produced), 1)
            errors, _ = cv.validate_lower_body_file(produced[0])
            self.assertEqual(errors, [])
            phases = {json.loads(line)["source"]["phase"]
                      for line in produced[0].read_text(encoding="utf-8").splitlines()}
            self.assertEqual(phases, {"standing", "walking", "no_person"})

    def test_producer_rejects_an_empty_inference_file(self):
        with tempfile.TemporaryDirectory() as folder:
            empty = Path(folder) / "empty.jsonl"
            empty.write_text("", encoding="utf-8")
            result = run(ROOT / "Camera/tools/lower_body_records.py", "--input", empty,
                         "--output-dir", Path(folder) / "out")
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("no JSON records", result.stderr + result.stdout)


class ImuSmoke(unittest.TestCase):
    def test_real_thingy_captures_are_valid(self):
        files = sorted((ROOT / "Nordic/firmware/experiments/imu_bmi270_c29/data").glob("*.csv"))
        self.assertGreaterEqual(len(files), 1)
        for path in files:
            with self.subTest(capture=path.name):
                errors, _ = cv.validate_imu_csv(path)
                self.assertEqual(errors, [])


if __name__ == "__main__":
    unittest.main()
