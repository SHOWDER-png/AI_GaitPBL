# Lower-body keypoint records

`tools/lower_body_records.py` derives six structured points per inference
frame from the immutable SCRUM-371 JSONL. It writes a distinct, timestamp-named
JSONL file under `results/scrum372-run-YYYYMMDD/`; it never edits its input.
Capture time (`source.timestamp_utc` and
`source.elapsed_monotonic_ms`) comes from the matching `fixtures/manifest.csv`
row, matched by source run, frame ID, and fixture path. The generated-at time
and filename identify when the derived records were produced.

## Mapping and coordinates

The mapping follows the YOLOv8-pose 17-keypoint COCO ordering documented in
`src/pose_detector.h` and decoded in `src/pose_detector.cpp`:

| Record point | Model index | COCO keypoint |
|---|---:|---|
| `left_hip` | 11 | left hip |
| `right_hip` | 12 | right hip |
| `left_knee` | 13 | left knee |
| `right_knee` | 14 | right knee |
| `left_ankle` | 15 | left ankle |
| `right_ankle` | 16 | right ankle |

Left and right are the subject's anatomical sides per COCO, not the viewer's
left and right in an image. SCRUM-371 stores pixel coordinates after undoing
the model's letterbox padding and scale. The origin is the image's top-left;
x increases rightward and y downward. Coordinates and confidence are copied
from the selected raw keypoint without clipping, rounding, interpolation, or
replacement.

## Record schema and validation

Each JSONL object has `record_type: "lower_body"` and
`schema_version: "lower-body-record.v1"`.

- `generated_at_utc`: UTC time the derivation ran.
- `source`: source-run/frame/fixture identity, image path, phase, and the
  original capture UTC and monotonic timestamps from the fixture manifest.
- `image`: width, height, and coordinate convention.
- `thresholds.keypoint_confidence`: the raw inference record's reporting
  threshold; it is not changed.
- `detection`: the raw detection-valid flag and person confidence.
- `points`: six entries in left-then-right hip, knee, ankle order. Each entry
  has its model index, raw `coordinates_px`, raw `confidence`, original
  SCRUM-371 `source_valid` value, derived `status` and `valid`, `reasons`,
  `duplicate_point`, and `duplicate_of`.

Base point validity requires a detected person, present finite x/y/confidence,
coordinates in-frame, and confidence greater than or equal to the raw
keypoint threshold. In-frame means `0 <= x < image.width` and
`0 <= y < image.height`. The raw `source_valid` flag is retained separately;
it does not override these additional checks.

Possible reason codes:

| Code | Meaning |
|---|---|
| `no_person_detection` | The inference frame has no accepted person detection. |
| `keypoint_missing` | The requested model index is absent from the raw keypoint array. |
| `missing_x`, `missing_y`, `missing_confidence` | That raw value is null/missing. |
| `x_out_of_frame`, `y_out_of_frame` | The unmodified coordinate is outside the image bounds. |
| `low_confidence` | Raw confidence is below the record's threshold. |
| `duplicate_point` | Another valid lower-body point has exactly the same x and y. |

A missing value has status `missing`; a present point failing a spatial or
confidence check has status `invalid`; otherwise status is `valid`. Duplicate
diagnostics are independent of base validity: all exact-match members are
flagged, the lowest model index is the canonical point, and each other member
names it in `duplicate_of`. Only exact numeric coordinate equality among
base-valid points is considered a duplicate; near-coincident points are not
merged. A duplicate flag does not discard or invalidate either raw
observation. Coordinates and confidence are retained for every present point,
including invalid ones. No point is inferred, interpolated, or synthesized.

Invalid JSON, duplicate source indices, missing capture timestamp matches,
and malformed required fields fail explicitly rather than yielding
success-shaped output. No-person frames still produce six explicitly missing
point entries with reasons.

## Run and test

From the repository root, use:

```sh
python3 Camera/tools/lower_body_records.py
python3 -m unittest discover -s Camera/tests -p 'test_lower_body_records.py' -v
```

Optional `--input`, `--manifest`, and `--output-dir` arguments select the raw
SCRUM-371 JSONL, its matching manifest, and a destination directory. Output
files are created exclusively and are never overwritten.

## SCRUM-371 fixture example

For `walking/walking_far_frame_251.png`, the generated record copies capture
time `2026-10-06T05:39:53.162Z` and the mapped raw coordinates/confidences:
left hip (index 11) `(646.543823, 86.3771973; 0.995283484)`, right hip (12)
`(585.810181, 89.1317139; 0.994665623)`, left knee (13)
`(651.360596, 196.090576; 0.996271372)`, right knee (14)
`(591.370483, 202.268677; 0.996128798)`, left ankle (15)
`(655.066956, 288.110352; 0.991232753)`, and right ankle (16)
`(607.537598, 298.072815; 0.991187811)`. All six are inside the 1280x720
fixture and exceed the stored 0.5 confidence threshold. The separate
SCRUM-372 output file is generated from the checked-in result by the command
above; the original SCRUM-371 records remain unchanged.

Visual comparison checked the saved-coordinate overlays on
`fixtures/walking/walking_far_frame_251.png` (frame 251) and
`fixtures/walking/walking_near_frame_346.png` (frame 346). In both frames, the
six hip, knee, and ankle markers fall on the corresponding visible joint
locations. The far-frame markers are positioned at the waist, both knees, and
both ankles; the near-frame markers likewise align with the waist, knees, and
ankles. These comparisons support the joint/index mapping but are not a
quantitative accuracy measurement. Both images crop the subject above the
waist, so visual inspection alone cannot establish anatomical left versus
image-left; side names follow the model's COCO semantics. The overlays were
viewed during review and were not saved as separate image files.
