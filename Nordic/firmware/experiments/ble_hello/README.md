# ble_hello — first BLE link Thingy:53 → i.MX95

Owner: Nordic. Status: **code written, not yet run on hardware.**

## Goal

The Thingy:53 advertises as `GaitNode` and notifies a counter at 100 Hz; the
FRDM-IMX95 (or a laptop first) connects, subscribes and logs every value, so dropped
packets and real throughput can be measured before the real protocol is fixed.

This experiment sends no sensor data. It answers three questions that
`../../../protocol/BLE_GAIT_PROTOCOL.md` leaves open:

1. Does one 18-byte notification every 10 ms get through without loss?
2. If not, how much is lost, and does it get lost in the Thingy (buffers full) or in the air?
3. Do we need to batch several samples into one notification?

## What is in this folder

| File | What it does |
|---|---|
| `src/main.c` | Firmware: advertises, exposes one notify characteristic, sends an 18-byte packet every 10 ms |
| `prj.conf` | Turns on BLE peripheral, sets the name `GaitNode`, asks for a 7.5–15 ms connection interval |
| `sysbuild.conf` | Builds the BLE radio image for the nRF5340 network core (copy of `Nordic/sysbuild.conf`) |
| `CMakeLists.txt` | Standard Zephyr app build file |
| `receiver/ble_hello_rx.py` | Laptop / i.MX95 side: connects, logs every packet to CSV, prints loss and rate |
| `data/` | Put the recorded CSVs here (create it on first run) |

### Packet (little-endian, 18 bytes)

| Offset | Type | Field |
|---:|---|---|
| 0 | uint16 | sequence number, +1 every 10 ms, wraps at 65536 |
| 2 | uint32 | t_ms, Thingy uptime |
| 6 | 12 bytes | filler (0xA5) |

Same size as the IMU packet proposed in the protocol doc, so the results carry over.
18 bytes fits in the default BLE packet (20 bytes of payload), so no MTU negotiation is needed.

The sequence number advances every tick even when the packet could not be sent, so the
receiver sees every loss as a gap in `seq`.

### UUIDs (experiment only, random)

- Service: `b7b7a76b-f857-45e2-84d3-4bae85442671`
- Counter (notify): `b7b7a76c-f857-45e2-84d3-4bae85442671`

## Run it

1. Build and flash as in `../../../FLASHING.md`
   (`west build -b thingy53/nrf5340/cpuapp --sysbuild -p` in this folder).
2. Open the USB serial port. It should print `advertising as GaitNode`.
3. On the laptop:

   ```
   pip install bleak
   python receiver/ble_hello_rx.py --seconds 60 --out data/laptop_1m.csv
   ```

4. The serial port prints one line per second while streaming:
   `seq=… sent=… failed=… skipped=…`, plus the connection interval the central granted.
5. The script prints expected / received / lost, the rate in Hz, and bytes per second.

Before writing the script off, a quick check with the **nRF Connect** phone app also works:
scan, connect to `GaitNode`, enable notifications on the counter characteristic.

## What to record

Run each case for at least 60 s and keep the CSV in `data/`:

| Case | Why |
|---|---|
| Laptop, Thingy 0.5 m away | Best case baseline |
| Laptop, Thingy 3–5 m away or on the foot / behind the body | Realistic gait setup |
| 10-minute run | Slow drift, sequence wrap, stability |
| FRDM-IMX95 as receiver | The real target (IW612 + BlueZ) |

For each run note: loss %, rate at receiver, the connection interval printed by the Thingy,
and the Thingy's `failed` / `skipped` counters.

How to read the result:

- `failed` or `skipped` above 0 on the Thingy: packets were lost before the radio
  (buffers full). Fix by batching samples or raising the BLE buffer counts.
- Thingy counters at 0 but gaps at the receiver: lost on the receiver side.
- Connection interval much longer than 15 ms: the central ignored the request, so
  batching is needed on that central.

## When it works

- Write the numbers and the batching decision into `../../../protocol/BLE_GAIT_PROTOCOL.md`.
- Move the BLE code into `../../gait_node/src/ble/gait_ble.c` and set `CONFIG_GAIT_BLE=y`.
- Update the status row in `../README.md`.
