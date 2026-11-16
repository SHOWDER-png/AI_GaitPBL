# gait_node - target structure and migration plan (for Claude Code)

Firmware for the **Nordic Thingy:53 (nRF5340)** sensor node of the AI Assist Gait system.
It reads the BMI270 IMU and 6x FSR402 (via ADS7128 ADC) in separate threads and sends the
raw data over BLE to the NXP i.MX95 host. This file defines the **target layout** and the
**order of work**. Follow it step by step.

## 0. Read first (mandatory)
1. `DOCKER_BUILD.md` - how to build (Docker, `west build -p always`, SDK version stated there)
2. `FIRMWARE_GUIDE.md` - older notes on FSR/IMU/threads. This README supersedes its file layout;
   update that guide after the restructure so it matches.
3. The existing sources: `src/main.c`, `fsr402.*`, and the teammate's `gait_ble.*`.
   **Do not invent APIs.** Read each header before calling it. The existing `gait_ble` code is the
   source of truth for the BLE wire format today; extend it, do not rewrite it.

## 1. System context
- One wireless node: BMI270 (on-board) + 6 FSR402 through ADS7128 (I2C) + BLE (Nordic UART / GATT).
- Host (i.MX95, Python `bleak`) computes CoP and gait metrics. **The node sends raw values only.**
- Thingy:53 has no on-board debugger: flash via `dfu_application.zip` (see `DOCKER_BUILD.md`).

## 2. Target file structure

```
gait_node/
  CMakeLists.txt   prj.conf   sysbuild.conf   app.overlay   .gitignore
  README.md   DOCKER_BUILD.md   FIRMWARE_GUIDE.md
  src/
    main.c                      init modules, create threads, system controller (no sampling loop)
    system/
      system_config.h           rates, priorities, stack sizes (single place for timing)
      system_state.h/.c         system state machine
      calibration.h/.c          IMU bias, FSR min/max (added: not in the original plan)
    imu/
      imu.h/.c                  BMI270 driver wrapper: init + read (no thread control)
      imu_thread.h/.c           periodic thread that calls imu.c
    plantar/
      fsr402.h/.c               ADS7128 + FSR402 driver: init + read_all (no thread control)
      plantar_thread.h/.c       periodic thread that calls fsr402.c
    ble/
      gait_ble.h/.c             BLE init, GATT/NUS, notifications (no sensor access)
      ble_thread.h/.c           takes samples from the queues, builds packets, sends
      gait_packet.h             wire format shared with the i.MX95 parser (added)
    data/
      gait_data.h               imu_sample, plantar_sample
      data_queue.h/.c           k_msgq queues between threads
```

Create files **only when their phase starts** (section 8). Do not create everything at once.
Skip `calibration.*` and `log` until their phase.

## 3. Responsibilities (must not overlap)

| File | Does | Does NOT |
|---|---|---|
| `main.c` | init modules, create threads, control system state | read sensors, send BLE |
| `system_config.h` | sampling rates, priorities, stack sizes | process data |
| `imu.c` / `fsr402.c` | talk to the hardware, return raw values | control threads |
| `imu_thread.c` / `plantar_thread.c` | decide WHEN to sample, push to queue | send BLE |
| `ble_thread.c` | take from queues, build packet, send | read sensors |
| `gait_ble.c` | BLE init, GATT, notify | read sensors |

Rule: **each piece of hardware is touched from exactly one thread.** Threads talk only through
queues (`k_msgq`), never through shared globals.

## 4. Timing config (`system/system_config.h`)
Starting values only; measure on real hardware and tune.

```c
#define IMU_SAMPLE_RATE_HZ   100
#define FSR_SAMPLE_RATE_HZ   100
#define BLE_SEND_RATE_HZ      50
#define IMU_INTERVAL_MS (1000 / IMU_SAMPLE_RATE_HZ)
#define FSR_INTERVAL_MS (1000 / FSR_SAMPLE_RATE_HZ)
#define BLE_INTERVAL_MS (1000 / BLE_SEND_RATE_HZ)
#define IMU_THREAD_PRIORITY 4   /* lower number = more important */
#define FSR_THREAD_PRIORITY 5
#define BLE_THREAD_PRIORITY 6
#define IMU_STACK_SIZE 2048
#define FSR_STACK_SIZE 2048
#define BLE_STACK_SIZE 2048
```
Intervals are defined only here. Threads use `K_TIMEOUT_ABS_MS` for a fixed period without drift.
If a thread crashes or the board resets, raise that thread's stack to 4096.

## 5. Data and queues
```c
struct imu_sample      { uint32_t timestamp_ms; int16_t accel[3]; int16_t gyro[3]; };
struct plantar_sample  { uint32_t timestamp_ms; uint16_t fsr[6]; };
```
- Two queues (`imu_q`, `fsr_q`), producers use `K_NO_WAIT`: if full, **drop** and count the drop.
  A sensor thread must never block.
- Count dropped samples and late deadlines per thread (needed for profiling).
- Units: accel mm/s^2, gyro mrad/s, FSR raw ADC counts (no conversion on the node).

## 6. Main and system state
`main()` order: init hardware/modules -> `gait_data_init()` -> start threads -> set state
`IDLE` -> loop `system_state_update()` every 100 ms. No sampling loop in `main`.

States: `INIT`, `IDLE`, `SAMPLING`, `STOP`. Sampling and transmitting happen at the same time,
so there is no separate TRANSMITTING state; BLE runs whenever the queues have data.
Start BLE init **before** any wait for a USB terminal, otherwise the board does not advertise.
The API names in the original plan (`imu_init`, `gait_data_init`, ...) are examples; create them
to match each module and make sure the project builds.

## 7. Build files
- `CMakeLists.txt`: list every `.c` explicitly and add
  `target_include_directories(app PRIVATE src)` so includes like `"system/system_config.h"`
  work from subfolders. Missing `.c` -> `undefined reference`.
- `prj.conf`: keep `CONFIG_BT`, NUS/GATT, `CONFIG_BT_L2CAP_TX_MTU=247`, `CONFIG_BT_BUF_ACL_*=251`,
  I2C, sensor, and any ADC options already used. Do not remove existing options.
- `sysbuild.conf`: keep it (Partition Manager + `ipc_radio` network core). Without it the
  network-core build fails (`undefined node label 's0_partition'`). Check it against the SDK
  version in `DOCKER_BUILD.md`.
- `app.overlay`: ADS7128 must be on an **external** I2C bus. `i2c1` is the Thingy:53 internal
  sensor bus (address 0x10 is already used there). See open decision D3.

## 8. Migration plan (build must pass after every phase)

| Phase | Work | Build check |
|---|---|---|
| 0 | Inventory current files; list them; confirm the current code builds | baseline builds |
| 1 | Add `system_config.h`, `gait_data.h`; CMake include dirs. No behavior change | builds |
| 2 | Move BMI270 code to `imu/imu.c`; add `imu_thread.c` | IMU values still printed |
| 3 | Move ADS7128/FSR to `plantar/`; add `plantar_thread.c` | 6 FSR values still printed |
| 4 | Add queues + `ble_thread.c`; keep `gait_ble.c` working | data arrives over BLE |
| 5 | Slim `main.c` into the controller; add `system_state.*` | same behavior as phase 4 |
| 6 | `calibration.*`, host commands over BLE, log/profiling thread | each tested on its own |

Where the old code goes:

| Old code | New place |
|---|---|
| BMI270 `sensor_attr_set()` | `imu.c` (init) |
| BMI270 `sensor_sample_fetch()` | `imu_thread.c` calls `imu.c` |
| `fsr402_init()` / `fsr402_read_all()` | `fsr402.c`, called from `plantar_thread.c` |
| `gait_ble_init()` | `gait_ble.c` |
| `gait_ble_send_plantar()` | called from `ble_thread.c` |
| `k_msleep(10)` | periodic timing inside each thread |
| `printf()` logging | separate low-priority log thread (not in the sampling path) |

## 9. Open decisions (ask the user, do not guess)
- **D1 Packet format.** Two queues mean two sample types with different timestamps. Proposal:
  two packet types, each `__packed`, little-endian, 17 bytes: `u8 type, u32 t_ms` +
  `6 x int16` (IMU) or `6 x uint16` (FSR). Python: `"<BI6h"` and `"<BI6H"`. The existing
  plantar format in `gait_ble.c` must keep working until the user confirms a change.
- **D2 Rate mismatch.** FSR 100 Hz vs BLE 50 Hz: decimate or batch? Decide before `ble_thread.c`.
- **D3 ADS7128 wiring.** Which Thingy:53 pins are SDA/SCL and what is the address-pin setting?
  Until known, the overlay cannot be finished.
- **D4 SDK version.** Use the version in `DOCKER_BUILD.md`. Do not mix versions.

## 10. Rules for the agent
- Ask before deleting anything. Never touch `.git`, the SDK, or the Docker volume `ncs-data`.
- One build folder only: `gait_node/build` (git-ignored, `.gitignore` has `build*/`).
- Keep files small; do not add libraries or Kconfig options that are not needed.
- ADS7128 register/command bytes were written from general knowledge and are **not verified**
  against the datasheet. Flag them; do not treat them as correct.
- State clearly what you could not verify instead of guessing.

## 11. Definition of done
- Tree matches section 2 (only files whose phase is finished).
- `west build -p always` passes with the SDK in `DOCKER_BUILD.md`.
- IMU and 6 FSR values reach the host over BLE; no thread blocks; drops and late deadlines are counted.
- `FIRMWARE_GUIDE.md` updated to the new layout.
