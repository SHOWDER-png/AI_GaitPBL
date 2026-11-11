# SCRUM-362 – IMU capture schema (unit C29, Thingy:53)

## Capture format (schema_version = 1)

Text CSV streamed over USB CDC ACM (COM port, 115200 baud setting is ignored by USB CDC).
Every time a program opens the port (DTR asserted) the firmware starts a fresh capture:
header block, then rows with `seq` starting at 0. When the port is closed it stops.

Lines starting with `#` are comments (metadata, statistics, boot log). All other lines
after the column line are data rows.

### Header block (comment lines)

```
# schema_version=1
# unit=C29 board=thingy53/nrf5340/cpuapp sensor=BMI270
# target_rate_hz=100 period_us=10000
# accel_range=+/-2g gyro_range=+/-500dps (set by imu_init)
# t_us=microseconds since boot, monotonic, taken just before sensor fetch
# clock_ticks_per_sec=32768 resolution_us=30.51
# units: accel=m/s^2 gyro=rad/s; axis order X,Y,Z (PCB arrows)
# imu_init=OK
seq,t_us,ax_mps2,ay_mps2,az_mps2,gx_rads,gy_rads,gz_rads
```

### Data row

| Column | Type | Unit | Meaning |
|---|---|---|---|
| `seq` | uint32 | – | Schedule slot number, starts at 0. A gap in `seq` = a lost sample. |
| `t_us` | uint64 | µs | Time since boot, from the Zephyr uptime tick counter. Monotonic, origin = boot, resolution 1/32768 s ≈ 30.5 µs. Taken just before the sensor read. |
| `ax_mps2, ay_mps2, az_mps2` | decimal, 6 places | m/s² | Accelerometer X, Y, Z. Range ±2 g. |
| `gx_rads, gy_rads, gz_rads` | decimal, 6 places | rad/s | Gyroscope X, Y, Z. Range ±500 dps. |

Axis order is X, Y, Z in the sensor frame documented in the SCRUM-360 init work (PCB arrows).
At rest and flat, gravity shows on Z (about −9.8 m/s²).

### Other comment lines

- `# STAT slots=<n> written=<n> dropped=<n> read_errors=<n>` every 100 slots.
  - `slots` = schedule slots elapsed, `written` = rows sent,
    `dropped` = slots skipped because the loop was more than one period late,
    `read_errors` = slots where the fetch or either channel read failed (no row written).
- `# log: ...` boot/init messages from Zephyr and `imu_init()` (added by the capture script
  for lines that appear before the header, e.g. `IMU: init OK (accel +/-2 g, gyro +/-500 dps, 100 Hz)`).
- `# BAD LINE: ...` a data line that failed parsing (should never appear; counted in the metrics).

## Sampling behaviour

- Target rate 100 Hz, absolute schedule (`slot n` at start + n × 10 ms), so no drift.
- A sample is written only if the fetch, accel read and gyro read all succeed; otherwise
  `read_errors` is incremented and no row is written.
- If the loop is more than one period late, the missed slots are counted in `dropped`
  and `seq` skips them.

## Capture command

```
Unblock-File .\capture_c29.ps1        # once, because the file was downloaded
.\capture_c29.ps1 -Label stationary_60s_final -Seconds 60 -Port COM5
```

Outputs in `data\`: `<Label>.csv` (clean capture), `<Label>_raw.txt` (unmodified serial log),
`<Label>_metrics.txt` (rate / jitter / loss).
The script drops leftover rows of a previous capture that may still be buffered in USB
before the new header, and reports how many (`stale lines discarded`).

## Result (stationary_60s_final, board still and flat)

| Metric | Value |
|---|---|
| Valid samples | 5953 (seq 0 to 5952), bad data lines 0 |
| Lost samples | 0 (0.000 %) |
| Duration | 59.520 s |
| Actual rate | 100.000 Hz |
| dt mean / min / max | 10000.0 / 9948 / 10102 µs |
| Jitter (std of dt) | 14.8 µs |
| Max abs deviation from 10 ms | 102 µs |
| Non-monotonic timestamps | 0 |
| Device counters (last STAT) | dropped=0, read_errors=0 |

Note: the `dropped` and `read_errors` counters were 0 in every run; the failure paths
were not fault-injected in this task.
