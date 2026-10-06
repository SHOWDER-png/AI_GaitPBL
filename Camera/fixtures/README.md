# Camera Fixtures

This fixture set reuses unchanged full-resolution frames from the local run `Camera/captures/SCRUM-370-20261006-123844`; no recapture was performed. It contains one standing sample, three walking samples (far/middle/near), and one no-person sample. The images are listed with source frame IDs, timestamps, paths, and SHA-256 hashes in `manifest.csv`.

The run has 509 currently available PNGs: 143 standing, 181 walking, and 185 no-person. Standing frames 1-22 are absent, while their records remain in the original `frames.csv`. Keep that CSV as the original capture log; `Camera/captures/SCRUM-370-20261006-123844/frames_available.csv` is the derived manifest for the 509 files that exist. Sampled walking frames at far, middle, and near distances show the lower body and feet in frame. A plain background is not required; the fixtures retain the real environment. The selected images have no face in frame and no legible identifying text in the reviewed view; only use/share the images with the subject's consent.

- `walking/`: a few frames showing a complete walking pose, including feet.
- `no_person/`: at least one representative empty-scene frame.
- `standing/`: optional still standing examples.

Use unchanged PNG copies from one capture run. Do not add raw sessions or images without the subject's consent. The complete source run stays local under the ignored `Camera/captures/` directory. The curated fixtures are the only capture images intended for version control.