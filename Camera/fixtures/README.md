# Camera Fixtures

The local run `Camera/captures/SCRUM-370-20261006-123844` contains standing, walking, and no-person images, but no images have been checked into this fixture folder. The remaining walking frames sampled at far, middle, and near distances show the lower body and feet in frame. However, 107 PNGs were manually deleted while their rows remain in `frames.csv` (standing indices 1-22 and walking indices 166-250). Keep `frames.csv` as the original capture log; use only existing images for any derived manifest or fixture selection. A plain background is not required: representative real-world backgrounds are acceptable, but inspect for personal or sensitive details and do not commit them without authorization and consent.

- `walking/`: a few frames showing a complete walking pose, including feet.
- `no_person/`: at least one representative empty-scene frame.
- `standing/`: optional still standing examples.

Use unchanged PNG copies from one capture run. Do not add raw sessions, faces, identifying surroundings, or any image without the subject's consent. The original run stays local under the ignored `Camera/captures/` directory. Update this note with the source run ID, actual image dimensions/FPS, and any observed drops once the sample set is created.