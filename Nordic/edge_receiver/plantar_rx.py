"""Show the six FSR402 values sent by the Thingy:53 gait_node over Bluetooth.

    pip install bleak
    python plantar_rx.py                       # print live until Ctrl+C
    python plantar_rx.py --seconds 30 --out walk1.csv

Packet layout and UUID: ../protocol/BLE_GAIT_PROTOCOL.md (plantar packet).
Works on Windows, macOS and Linux (so also on the i.MX95 with BlueZ).
"""

import argparse
import asyncio
import csv
import struct
import time

from bleak import BleakClient, BleakScanner

DEVICE_NAME = "GaitNode"
PLANTAR_UUID = "e6d1e9db-be0e-4889-b490-7643045692db"  # must match gait_ble.c
PACKET_LEN = 18
COLUMNS = ["l_heel", "l_ball", "l_toe", "r_heel", "r_ball", "r_toe"]


async def run(seconds: float | None, out_path: str | None) -> None:
    print(f"scanning for {DEVICE_NAME} ...")
    device = await BleakScanner.find_device_by_name(DEVICE_NAME, timeout=15.0)
    if device is None:
        raise SystemExit(f"{DEVICE_NAME} not found. Is the Thingy on, and not connected to a phone?")

    rows = []
    state = {"last_seq": None, "lost": 0, "last_print": 0.0}

    def on_notify(_sender, data: bytearray) -> None:
        if len(data) != PACKET_LEN:
            print(f"unexpected packet length {len(data)}")
            return
        # <  little-endian, H seq (uint16), I t_ms (uint32), 6h six int16 ADC counts
        seq, t_ms, *raw = struct.unpack("<HI6h", data)

        if state["last_seq"] is not None:
            state["lost"] += (seq - state["last_seq"] - 1) % 65536
        state["last_seq"] = seq
        rows.append((time.time(), seq, t_ms, *raw))

        # Print about 10 lines per second so the terminal stays readable
        now = time.perf_counter()
        if now - state["last_print"] >= 0.1:
            state["last_print"] = now
            values = "  ".join(f"{name}={v:4d}" for name, v in zip(COLUMNS, raw))
            print(f"t={t_ms:8d} ms  {values}")

    async with BleakClient(device) as client:
        print(f"connected to {device.address}. Press the sensors. Ctrl+C to stop.")
        await client.start_notify(PLANTAR_UUID, on_notify)
        try:
            if seconds is None:
                while client.is_connected:
                    await asyncio.sleep(0.5)
            else:
                await asyncio.sleep(seconds)
        except (KeyboardInterrupt, asyncio.CancelledError):
            pass

    print(f"\nreceived {len(rows)} packets, lost {state['lost']}")
    if out_path and rows:
        with open(out_path, "w", newline="") as f:
            w = csv.writer(f)
            w.writerow(["rx_time_utc", "seq", "t_ms", *COLUMNS])
            w.writerows(rows)
        print(f"saved to {out_path}")


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--seconds", type=float, default=None, help="stop after this long (default: until Ctrl+C)")
    p.add_argument("--out", default=None, help="also save every packet to this CSV file")
    args = p.parse_args()
    try:
        asyncio.run(run(args.seconds, args.out))
    except KeyboardInterrupt:
        pass
