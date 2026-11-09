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
├── FLASHING.md               How to build and put the firmware on a real Thingy:53
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

## Build and flash (nRF Connect SDK)

Full step-by-step guide, including putting it on a real Thingy:53: **[FLASHING.md](FLASHING.md)**.

Team baseline (from C29): **nRF Connect SDK v3.3.4**, board target `thingy53/nrf5340/cpuapp`,
sysbuild with MCUboot. v3.4.0 failed to build in C29 (devicetree error), so stay on v3.3.4
until someone fixes that.

```
cd Nordic/firmware/gait_node
west build -b thingy53/nrf5340/cpuapp --sysbuild -p
# then flash build/dfu_application.zip over USB — see FLASHING.md
```

Serial console: USB CDC ACM, 115200 baud.

# nordic/ — Thingy:53 sensor node firmware

Firmware for the **Nordic Thingy:53 (nRF5340)** wearable node. It reads the
BMI270 IMU and 6× FSR402 foot-pressure sensors (via an ADS7128 ADC) and streams
28-byte packets over BLE (Nordic UART Service) to the NXP i.MX95 host.

Built with **nRF Connect SDK v3.4.0** (Zephyr) using `west` / sysbuild.

## File structure

```
nordic/
├── CMakeLists.txt      # Zephyr app definition (lists src/main.c)
├── prj.conf            # Kconfig for the APP core: I2C, sensor, BMI270, BLE, NUS, MTU
├── sysbuild.conf       # Kconfig for the WHOLE build: network-core image + Partition Manager
├── src/
│   └── main.c          # the application code (the only C file you edit)
├── .gitignore          # ignores build*/ so build output is never committed
├── build/              # GENERATED by the build, safe to delete (also build_1/ etc.)
└── .vscode/            # nRF Connect extension settings (if present)
```

| File | What it does | Edit it? |
|---|---|---|
| `src/main.c` | Reads IMU (Sensor API) + FSR (ADS7128 over I2C), packs `SensorTelemetryPacket_t`, sends with `bt_nus_send()` every 20 ms (50 Hz) | Yes |
| `prj.conf` | Settings for the app core image (drivers, Bluetooth, MTU 247) | Yes |
| `sysbuild.conf` | Chooses the network-core firmware (`ipc_radio`) and enables Partition Manager, required for Thingy:53 in NCS v3.4.0 | Rarely |
| `CMakeLists.txt` | Tells the build which source files to compile | Only when adding `.c` files |
| `build*/` | Compiler output | No — never edit by hand |

## What the build produces

One `west build` builds **four images** (sysbuild):

| Image | Runs on | Purpose | Written by |
|---|---|---|---|
| `mcuboot` | app core | Bootloader, validates and swaps images, enables DFU | NCS (built-in) |
| `b0n` | network core | Network-core bootloader | NCS (built-in) |
| `nordic` | app core | **This project's code** (`src/main.c`) | You |
| `ipc_radio` | network core | BLE radio firmware | NCS (built-in) |

Files you will use inside `build/`:

| Output | Use |
|---|---|
| `dfu_application.zip` | Update package with app + network core images. Use for USB (Programmer) or BLE DFU |
| `merged_CPUNET.hex` and the merged app hex | Used by `west flash` through a debug probe |

## How to use

**1. Build** — in VS Code open the *nRF Connect* panel → *Add Build Configuration*:
board `thingy53/nrf5340/cpuapp`, SDK `v3.4.0`, build directory `build`, leave the
extra arguments empty → **Build**. Use *Pristine* if you change `sysbuild.conf`.

**2. Flash** — the Thingy:53 has no on-board debugger, so `west flash` only works
with an external probe (nRF5340 DK debug-out or J-Link, 10-pin cable). Without a probe:

- **USB:** enter MCUboot recovery mode (switch SW1 off, hold the push button,
  switch SW1 on), then in *nRF Connect for Desktop → Programmer* select the
  Thingy:53 and write `build/dfu_application.zip`.
- **Bluetooth:** use the *nRF Programmer* mobile app with `dfu_application.zip`.
  Works only while the running firmware has the MCU Manager (SMP) service.

**3. Check** — scan with *nRF Connect for Mobile*: the device should be named
`thingy53_gait`. Connect, subscribe to the NUS TX characteristic
(`6e400003-b5a3-f393-e0a9-e50e24dcca9e`) and you should see 28-byte values about
50 times per second.

## Packet format (must match the host parser)

`SensorTelemetryPacket_t`, little-endian, 28 bytes, Python format `"<I6h6H"`:

| Field | Type | Unit |
|---|---|---|
| `timestamp_ms` | uint32 | ms since boot |
| `accel_x/y/z` | int16 | mm/s² |
| `gyro_x/y/z` | int16 | mrad/s |
| `fsr_ch0` … `fsr_ch5` | uint16 | raw ADC count (0–4095) |

Raw values only. Calibration and CoP calculation are done on the i.MX95.

## Known issues / to do

- **ADS7128 bus:** `main.c` uses `DT_NODELABEL(i2c1)` as a placeholder. On the
  Thingy:53, `i2c1` is the *internal* sensor bus (other chips live there, and 0x10
  is already taken), so a devicetree overlay for the external I2C pins is still
  needed. FSR values read `0` until it exists.
- **ADS7128 register bytes** were written from general knowledge, so verify them
  against the ADS7128 datasheet.
- **FSR / IMU calibration** (`min_raw`/`max_raw`, gyro bias) is not implemented yet.
- To keep BLE OTA updates after the first flash, the SMP/MCU Manager service must
  be added to the app.