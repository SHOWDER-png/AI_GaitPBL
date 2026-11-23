# Experiments

Small standalone Zephyr apps for bringing up one thing at a time. Each folder builds and
flashes on its own (`west build -b thingy53/nrf5340/cpuapp --sysbuild -p` inside the folder).

Name folders `<area>_<what>[_<ticket>]`, e.g. `imu_bmi270_c29`, `plantar_ads1115`, `ble_hello`.
Keep recorded evidence in a `data/` subfolder and a short note (README or YAML) of what was
measured. When an experiment works, move the code into `../gait_node/src/<area>/`.

| Folder | Owner | Status |
|---|---|---|
| `imu_bmi270_c29/` | IMU (Kaan) | Done: BMI270 identity, axes, units, ~94 Hz observed (see `imu_config.yaml`) |
| `plantar_ads1115/` | Plantar | Not started |
| `ble_hello/` | Nordic | Code written, not yet run on hardware |
