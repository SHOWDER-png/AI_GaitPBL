# Nordic — Thingy:53 wireless sensor node

Everything that runs on the Nordic Thingy:53, or talks to it, lives here.
The camera pipeline is in `../Camera/`.

## Who owns what

| Area | Folder | Owner |
|---|---|---|
| IMU (BMI270 on the Thingy:53) | `firmware/gait_node/src/imu/`, `firmware/experiments/imu_*` | Kaan (@Kaannoiz) |
| Plantar pressure (FSR 402 ×6 via ADS1115) | `firmware/gait_node/src/plantar/`, `firmware/experiments/plantar_*`, `hardware/` | _fill in_ |
| Nordic Thingy:53 platform: BLE, power, main loop, integration | `firmware/gait_node/src/ble/`, `firmware/gait_node/src/main.c`, `protocol/`, `edge_receiver/` | _fill in_ |
| Shared data types | `firmware/gait_node/src/common/` | all three: change by PR, everyone reviews |

## Layout

```
Nordic/
├── firmware/
│   ├── gait_node/            The ONE app that is flashed to the Thingy:53 (integration target)
│   │   ├── CMakeLists.txt
│   │   ├── Kconfig           CONFIG_GAIT_IMU / CONFIG_GAIT_PLANTAR / CONFIG_GAIT_BLE switches
│   │   ├── prj.conf
│   │   ├── app.overlay       Devicetree: BMI270 enable, ADS1115 nodes
│   │   └── src/
│   │       ├── main.c        Sampling loop, wires the modules together   (Nordic owner)
│   │       ├── common/       gait_types.h: sample structs shared by all  (everyone)
│   │       ├── imu/          imu.c / imu.h                               (IMU owner)
│   │       ├── plantar/      plantar.c / plantar.h                       (plantar owner)
│   │       └── ble/          gait_ble.c / gait_ble.h                     (Nordic owner)
│   └── experiments/          Standalone bring-up apps, one folder per task. Not flashed together.
│       ├── imu_bmi270_c29/   Kaan's C29 BMI270 characterisation (was /Thingy53_IMU_C29)
│       ├── plantar_ads1115/  Plantar owner: first FSR → ADS1115 → Thingy:53 read
│       └── ble_hello/        Nordic owner: first BLE link Thingy:53 → i.MX95
├── protocol/
│   └── BLE_GAIT_PROTOCOL.md  The contract between the Thingy:53 and the i.MX95 (UUIDs, packet layout)
├── hardware/
│   └── PLANTAR_WIRING.md     FSR positions, voltage divider, ADS1115 addresses, Thingy:53 pins
├── edge_receiver/            i.MX95 (Linux) side: BLE client that receives packets and logs them
└── tools/                    Host scripts (serial capture, CSV conversion, plotting)
```

## How the three parts fit together

```
 BMI270 ──SPI──► imu/  ─────┐
                             ├─► main.c ──► ble/ ──BLE──► edge_receiver/ (i.MX95)
 FSR 402 ×6 ─► ADS1115 ─I2C─► plantar/ ─┘
```

- `imu/` and `plantar/` only **read sensors** and fill a struct from `common/gait_types.h`.
  They do not know about BLE.
- `ble/` only **sends** those structs. It does not know about sensors.
- `main.c` owns timing: IMU at 100 Hz, plantar at 50 Hz, one shared timestamp clock
  (`k_uptime_get()`), so the i.MX95 can line the streams up.
- Each module can be switched off in `prj.conf` (`CONFIG_GAIT_PLANTAR=n` etc.), so the
  firmware still builds and runs when one part is not ready. This is the "modular sensor
  fusion" from the development plan.

## Working rules

1. Try new hardware in `firmware/experiments/<area>_<what>/` first. Move it into
   `gait_node/src/<area>/` once it works.
2. Work on a branch named `SCRUM-<id>-<short-name>` and merge to `main` by PR.
3. Changing `common/gait_types.h` or `protocol/BLE_GAIT_PROTOCOL.md` affects everyone:
   tag the other two on the PR.
4. Commit recorded data you want to keep under `data/` in the relevant folder.
   (`*.csv` is ignored repo-wide; `Nordic/**/data/*.csv` is excepted in `.gitignore`.)

## Build (nRF Connect SDK)

Team baseline (from C29): **nRF Connect SDK v3.3.4**, board target `thingy53/nrf5340/cpuapp`,
sysbuild with MCUboot. v3.4.0 failed to build in C29 (devicetree error), so stay on v3.3.4
until someone fixes that.

```
cd Nordic/firmware/gait_node
west build -b thingy53/nrf5340/cpuapp --sysbuild -p
west flash        # or drag build/dfu_application.zip into nRF Connect Programmer (USB DFU)
```

Serial console: USB CDC ACM, 115200 baud.
