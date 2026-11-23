"""Receiver for the ble_hello experiment.

Connects to the Thingy:53 advertising as "GaitNode", subscribes to the counter
characteristic, logs every packet to a CSV and prints loss / rate statistics.

    pip install bleak
    python ble_hello_rx.py --seconds 60 --out ../data/laptop_1m.csv

Works on Windows, macOS and Linux (so also on the i.MX95 once BlueZ is up).
"""

import argparse
import asyncio
import csv
import statistics
import struct
import time

from bleak import BleakClient, BleakScanner

DEVICE_NAME = "GaitNode"
COUNTER_UUID = "b7b7a76c-f857-45e2-84d3-4bae85442671"  # must match src/main.c
PACKET_LEN = 18
RATE_HZ = 100


async def run(seconds: float, out_path: str) -> None:
    print(f"scanning for {DEVICE_NAME} ...")
    device = await BleakScanner.find_device_by_name(DEVICE_NAME, timeout=15.0)
    if device is None:
        raise SystemExit(f"{DEVICE_NAME} not found. Is the Thingy powered and not already connected?")

    rows = []  # (rx_time_s, seq, t_ms)

    def on_notify(_sender, data: bytearray) -> None:
        rx = time.perf_counter()
        if len(data) != PACKET_LEN:
            print(f"unexpected packet length {len(data)}")
            return
        seq, t_ms = struct.unpack_from("<HI", data, 0)
        rows.append((rx, seq, t_ms))

    async with BleakClient(device) as client:
        print(f"connected to {device.address}, logging for {seconds:.0f} s")
        await client.start_notify(COUNTER_UUID, on_notify)
        await asyncio.sleep(seconds)
        await client.stop_notify(COUNTER_UUID)

    if len(rows) < 2:
        raise SystemExit("no packets received")

    t0 = rows[0][0]
    with open(out_path, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["rx_time_s", "seq", "t_ms"])
        for rx, seq, t_ms in rows:
            w.writerow([f"{rx - t0:.6f}", seq, t_ms])

    # Loss: every step in seq larger than 1 is that many missing packets.
    # seq is uint16, so take differences modulo 65536 to survive the wrap.
    lost = 0
    gaps = 0
    for (_, a, _), (_, b, _) in zip(rows, rows[1:]):
        step = (b - a) % 65536
        if step != 1:
            gaps += 1
            lost += step - 1
    received = len(rows)
    expected = received + lost

    # Rate as seen by the Thingy clock (sender) and by this machine (receiver)
    thingy_span = (rows[-1][2] - rows[0][2]) / 1000.0
    rx_span = rows[-1][0] - rows[0][0]
    intervals_ms = [(b[0] - a[0]) * 1000.0 for a, b in zip(rows, rows[1:])]

    print(f"\nsaved {received} packets to {out_path}")
    print(f"expected {expected}, received {received}, lost {lost} in {gaps} gaps "
          f"({100.0 * lost / expected:.3f} % loss)")
    print(f"rate: {(received - 1) / rx_span:.2f} Hz at receiver, "
          f"{(expected - 1) / thingy_span:.2f} Hz at Thingy (target {RATE_HZ})")
    print(f"throughput: {received * PACKET_LEN / rx_span:.0f} bytes/s")
    print(f"arrival interval: median {statistics.median(intervals_ms):.1f} ms, "
          f"max {max(intervals_ms):.1f} ms")
    print("(packets arrive in bursts, one burst per connection interval, so a median "
          "near 0 ms with a max near the interval is normal)")


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--seconds", type=float, default=60.0, help="how long to log")
    p.add_argument("--out", default="ble_hello_log.csv", help="CSV output path")
    args = p.parse_args()
    asyncio.run(run(args.seconds, args.out))
