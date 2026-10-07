import json
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

from generate_scrum373_report import (  # noqa: E402
    DEFAULT_OUTPUT_DIR,
    generate,
)


class Scrum373ReportTests(unittest.TestCase):
    def test_report_and_annotations_use_saved_fixture_evidence(self):
        with tempfile.TemporaryDirectory(
            dir=DEFAULT_OUTPUT_DIR.parent
        ) as temporary_directory:
            output_dir = Path(temporary_directory)
            manifest = generate(output_dir=output_dir)

            self.assertEqual(manifest["schema_version"], "scrum373-results.v1")
            self.assertEqual(manifest["metrics"]["total_frames"], 5)
            self.assertEqual(manifest["metrics"]["valid_frames"], 4)
            self.assertAlmostEqual(manifest["metrics"]["mean_latency_ms"], 78.7820748)
            self.assertAlmostEqual(manifest["metrics"]["p95_latency_ms"], 88.507875)
            self.assertAlmostEqual(manifest["metrics"]["fps"], 12.6932428)
            self.assertEqual(manifest["metrics"]["mean_latency_target_result"], "PASS")
            self.assertEqual(manifest["metrics"]["fps_target_result"], "FAIL")
            self.assertFalse(manifest["capture"]["source_run_directory_present"])
            self.assertFalse(manifest["capture"]["inference_executable_present"])
            self.assertTrue(manifest["model"]["local_model_matches_recorded_hash"])

            report = (output_dir / "report.md").read_text(encoding="utf-8")
            self.assertIn("78.78 ms", report)
            self.assertIn("88.51 ms", report)
            self.assertIn("12.69 FPS", report)
            self.assertIn("not an i.MX95 performance result", report)
            self.assertIn("22 missing standing images", report)
            self.assertIn("SCRUM-201 — Camera Occlusion Fallback", report)
            self.assertIn("SCRUM-176 — Port Preparation for FRDM-IMX95", report)
            self.assertEqual(len(list((output_dir / "annotations").glob("*.svg"))), 5)
            for svg_path in (output_dir / "annotations").glob("*.svg"):
                svg = ET.parse(svg_path).getroot()
                image = svg.find("{http://www.w3.org/2000/svg}image")
                self.assertIsNotNone(image)
                image_href = image.attrib["href"]
                self.assertTrue((svg_path.parent / image_href).resolve().is_file())
                self.assertEqual(
                    image.attrib[
                        "{http://www.w3.org/1999/xlink}href"
                    ],
                    image_href,
                )

            examples = {example["fixture_path"]: example for example in manifest["examples"]}
            low_confidence = examples["walking/walking_near_frame_346.png"]
            self.assertEqual(
                low_confidence["role"],
                "lowest-confidence detected walking sample",
            )
            self.assertAlmostEqual(
                min(point["confidence"] for point in low_confidence["points"]),
                0.835633397,
            )
            no_person = examples["no_person/no_person_frame_439.png"]
            self.assertFalse(no_person["detection_valid"])
            self.assertTrue(all(not point["valid"] for point in no_person["points"]))

            no_person_svg = (
                output_dir / "annotations" / "no_person__no_person_frame_439.svg"
            ).read_text(encoding="utf-8")
            self.assertIn("No accepted person detection", no_person_svg)
            self.assertIn("left_hip", no_person_svg)
            self.assertIn("MISSING", no_person_svg)
            self.assertIn("no_person_frame_439.png", no_person_svg)

            saved_manifest = json.loads(
                (output_dir / "outputs.json").read_text(encoding="utf-8")
            )
            self.assertEqual(saved_manifest, manifest)

    def test_default_output_location_is_under_camera_results(self):
        self.assertEqual(
            DEFAULT_OUTPUT_DIR,
            Path(__file__).resolve().parents[1]
            / "results"
            / "scrum373-annotated-performance-report",
        )


if __name__ == "__main__":
    unittest.main()
