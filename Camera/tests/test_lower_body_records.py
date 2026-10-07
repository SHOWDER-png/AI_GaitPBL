import copy
import sys
import tempfile
import unittest
from datetime import datetime, timezone
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

from lower_body_records import (  # noqa: E402
    InputError,
    KEYPOINTS,
    build_records,
    make_lower_body_record,
    write_timestamped_records,
)


GENERATED_AT = "2026-10-07T12:00:00.000000Z"
MANIFEST_KEY = ("capture-1", "42", "walking/frame_42.png")
MANIFEST = {
    MANIFEST_KEY: {
        "phase": "walking",
        "timestamp_utc": "2026-10-07T11:59:00.123Z",
        "elapsed_monotonic_ms": 1234.5,
    }
}


def frame_record(points=None, **overrides):
    points = points if points is not None else [
        {
            "index": index,
            "x": float(index * 10),
            "y": float(index * 20),
            "confidence": 0.8,
            "valid": True,
        }
        for _, index in KEYPOINTS
    ]
    frame = {
        "record_type": "frame",
        "source_run": MANIFEST_KEY[0],
        "frame_id": MANIFEST_KEY[1],
        "fixture_path": MANIFEST_KEY[2],
        "image_path": "/fixtures/walking/frame_42.png",
        "phase": "walking",
        "image_width": 1280,
        "image_height": 720,
        "keypoint_confidence_threshold": 0.5,
        "detection_valid": True,
        "person_confidence": 0.9,
        "keypoints": points,
    }
    frame.update(overrides)
    return frame


class LowerBodyRecordTests(unittest.TestCase):
    def test_verified_coco_mapping_and_capture_timestamp(self):
        result = make_lower_body_record(frame_record(), MANIFEST, GENERATED_AT)
        self.assertEqual(
            [(point["name"], point["model_index"]) for point in result["points"]],
            [
                ("left_hip", 11),
                ("right_hip", 12),
                ("left_knee", 13),
                ("right_knee", 14),
                ("left_ankle", 15),
                ("right_ankle", 16),
            ],
        )
        self.assertEqual(
            result["source"]["timestamp_utc"], "2026-10-07T11:59:00.123Z"
        )
        self.assertEqual(result["generated_at_utc"], GENERATED_AT)

    def test_raw_input_values_are_preserved(self):
        source = frame_record()
        before = copy.deepcopy(source)
        result = make_lower_body_record(source, MANIFEST, GENERATED_AT)
        self.assertEqual(source, before)
        original_hip = next(p for p in source["keypoints"] if p["index"] == 11)
        result_hip = result["points"][0]
        self.assertEqual(
            result_hip["coordinates_px"],
            {"x": original_hip["x"], "y": original_hip["y"]},
        )
        self.assertEqual(result_hip["confidence"], original_hip["confidence"])
        self.assertEqual(result_hip["source_valid"], original_hip["valid"])

    def test_no_detection_marks_every_point_missing_with_reason(self):
        result = make_lower_body_record(
            frame_record(
                points=[],
                detection_valid=False,
                person_confidence=None,
            ),
            MANIFEST,
            GENERATED_AT,
        )
        self.assertEqual(len(result["points"]), 6)
        for point in result["points"]:
            self.assertEqual(point["status"], "missing")
            self.assertFalse(point["valid"])
            self.assertIsNone(point["coordinates_px"]["x"])
            self.assertIn("no_person_detection", point["reasons"])
            self.assertIn("keypoint_missing", point["reasons"])

    def test_missing_individual_point_is_explicit(self):
        points = [
            {"index": index, "x": 20.0, "y": 30.0, "confidence": 0.8}
            for _, index in KEYPOINTS
            if index != 11
        ]
        result = make_lower_body_record(
            frame_record(points=points), MANIFEST, GENERATED_AT
        )
        hip = result["points"][0]
        self.assertEqual(hip["status"], "missing")
        self.assertEqual(hip["reasons"], ["keypoint_missing"])
        self.assertIsNone(hip["coordinates_px"]["x"])

    def test_out_of_frame_and_low_confidence_reasons_preserve_raw_values(self):
        points = [
            {"index": index, "x": 20.0, "y": 30.0, "confidence": 0.8}
            for _, index in KEYPOINTS
        ]
        points[0].update(x=-0.1, y=720, confidence=0.49)
        result = make_lower_body_record(
            frame_record(points=points), MANIFEST, GENERATED_AT
        )
        hip = result["points"][0]
        self.assertEqual(hip["status"], "invalid")
        self.assertFalse(hip["valid"])
        self.assertEqual(hip["coordinates_px"], {"x": -0.1, "y": 720})
        self.assertEqual(hip["confidence"], 0.49)
        self.assertEqual(
            hip["reasons"],
            ["x_out_of_frame", "y_out_of_frame", "low_confidence"],
        )

    def test_bounds_are_inclusive_at_zero_but_exclusive_at_image_size(self):
        points = [
            {
                "index": index,
                "x": float(index * 10),
                "y": float(index * 20),
                "confidence": 0.5,
            }
            for _, index in KEYPOINTS
        ]
        points[0].update(x=1280, y=719)
        result = make_lower_body_record(
            frame_record(points=points), MANIFEST, GENERATED_AT
        )
        self.assertEqual(result["points"][0]["reasons"], ["x_out_of_frame"])
        self.assertTrue(result["points"][1]["valid"])
        self.assertEqual(result["points"][1]["reasons"], [])

    def test_exact_duplicate_rule_flags_all_members_without_invalidating_them(self):
        points = [
            {"index": index, "x": float(index), "y": 30.0, "confidence": 0.8}
            for _, index in KEYPOINTS
        ]
        points[0].update(x=100.0, y=200.0)
        points[1].update(x=100.0, y=200.0)
        points[2].update(x=100.00001, y=200.0)
        result = make_lower_body_record(
            frame_record(points=points), MANIFEST, GENERATED_AT
        )
        left_hip, right_hip = result["points"][:2]
        near_point = result["points"][2]
        self.assertTrue(left_hip["duplicate_point"])
        self.assertIsNone(left_hip["duplicate_of"])
        self.assertTrue(right_hip["duplicate_point"])
        self.assertEqual(right_hip["duplicate_of"], "left_hip")
        self.assertIn("duplicate_point", right_hip["reasons"])
        self.assertTrue(left_hip["valid"])
        self.assertTrue(right_hip["valid"])
        self.assertFalse(near_point["duplicate_point"])

    def test_duplicate_source_indices_are_rejected(self):
        points = frame_record()["keypoints"]
        points.append(dict(points[0]))
        with self.assertRaisesRegex(InputError, "duplicate source keypoint index"):
            make_lower_body_record(
                frame_record(points=points), MANIFEST, GENERATED_AT
            )

    def test_missing_capture_timestamp_match_is_rejected(self):
        with self.assertRaisesRegex(InputError, "no matching capture manifest row"):
            make_lower_body_record(
                frame_record(frame_id="unmatched"), MANIFEST, GENERATED_AT
            )

    def test_run_record_is_skipped_and_all_frame_records_are_converted(self):
        records = build_records(
            [
                {"record_type": "run"},
                frame_record(),
                frame_record(frame_id="42", fixture_path=MANIFEST_KEY[2]),
            ],
            MANIFEST,
            GENERATED_AT,
        )
        self.assertEqual(len(records), 2)
        self.assertTrue(all(r["record_type"] == "lower_body" for r in records))

    def test_output_is_timestamped_and_never_overwritten(self):
        fixed_now = datetime(2026, 10, 7, 12, 0, tzinfo=timezone.utc)
        record = make_lower_body_record(frame_record(), MANIFEST, GENERATED_AT)
        with tempfile.TemporaryDirectory() as temporary_directory:
            output_directory = Path(temporary_directory)
            output_path = write_timestamped_records(
                [record], output_directory, fixed_now
            )
            self.assertEqual(
                output_path.name,
                "lower-body-records-20261007T120000.000000Z.jsonl",
            )
            with self.assertRaises(FileExistsError):
                write_timestamped_records([record], output_directory, fixed_now)


if __name__ == "__main__":
    unittest.main()
