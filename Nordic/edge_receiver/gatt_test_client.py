"""Manual test client for the gait_node GATT service (PC / i.MX95).   pip install bleak

  python gatt_test_client.py              connect to "GaitNode", stream 10 s, print stats
  python gatt_test_client.py --seconds 30
  python gatt_test_client.py --selftest   NO Bluetooth: decode fake packets built here

Packet layout (protocol/BLE_GAIT_PROTOCOL.md), 18 bytes little-endian:
  uint16 seq, uint32 t_ms, 6 x int16   ("<HI6h")
  IMU     : acc x,y,z (0.001 m/s^2), gyro x,y,z (0.001 rad/s)
  Plantar : LH, LB, LT, RH, RB, RT (ADS7128 counts 0..4095)
"""
import argparse
import asyncio
import struct

NAME = "GaitNode"
BASE = "e6d1e9{:02x}-be0e-4889-b490-7643045692db"
IMU, PLANTAR, CTRL, STAT = (BASE.format(i) for i in (0xDA, 0xDB, 0xDC, 0xDD))
FMT = "<HI6h"  # same size (18) for both; plantar values are 0..4095 so signed is fine
PKT = struct.calcsize(FMT)


class Stats:
    """Counts packets and detects lost ones from the sequence number."""

    def __init__(self, label):
        self.label, self.n, self.lost, self.last = label, 0, 0, None

    def add(self, seq):
        if self.last is not None:
            self.lost += (seq - self.last - 1) & 0xFFFF
        self.last = seq
        self.n += 1


def decode_imu(d):
    seq, t, ax, ay, az, gx, gy, gz = struct.unpack(FMT, d)
    return seq, t, (ax / 1000, ay / 1000, az / 1000), (gx / 1000, gy / 1000, gz / 1000)


def decode_plantar(d):
    seq, t, *f = struct.unpack(FMT, d)
    return seq, t, f


def selftest():
    """Round trip without BLE: build packets like the firmware does, decode like the receiver."""
    imu = struct.pack(FMT, 7, 1234, 1500, -800, 9810, 100, 3000, -300)
    pl = struct.pack(FMT, 8, 1240, 3000, 3500, 0, 0, 0, 2500)
    assert len(imu) == len(pl) == 18
    print("IMU    :", decode_imu(imu))
    print("PLANTAR:", decode_plantar(pl))
    st = Stats("test")
    for s in (65534, 65535, 0, 2):  # wraps at 65535, one packet (seq 1) missing
        st.add(s)
    print(f"seq gap check: n={st.n} lost={st.lost} (expect n=4 lost=1)")
    assert st.lost == 1
    print("selftest OK")


async def run(seconds):
    from bleak import BleakClient, BleakScanner

    s_imu, s_pl = Stats("IMU"), Stats("PLANTAR")

    def on_imu(_, d):
        seq, t, acc, gyr = decode_imu(bytes(d))
        s_imu.add(seq)
        if s_imu.n % 100 == 1:
            print(f"IMU     seq={seq} t={t}ms acc={acc} m/s2 gyro={gyr} rad/s")

    def on_pl(_, d):
        seq, t, f = decode_plantar(bytes(d))
        s_pl.add(seq)
        if s_pl.n % 100 == 1:
            print(f"PLANTAR seq={seq} t={t}ms LH,LB,LT,RH,RB,RT={f}")

    dev = await BleakScanner.find_device_by_name(NAME, timeout=15)
    if dev is None:
        raise SystemExit(f"{NAME} not found (is it advertising? connected to another device?)")
    async with BleakClient(dev) as c:
        print("status (0 idle, 1 streaming) =", (await c.read_gatt_char(STAT))[0])
        await c.start_notify(IMU, on_imu)
        await c.start_notify(PLANTAR, on_pl)
        await c.write_gatt_char(CTRL, bytes([1]))  # START
        await asyncio.sleep(seconds)
        await c.write_gatt_char(CTRL, bytes([0]))  # STOP
        await asyncio.sleep(0.5)
    for s in (s_imu, s_pl):
        print(f"{s.label}: {s.n / seconds:.0f} packets/s, lost {s.lost}")
    print("expect about 100 packets/s each (IMU_SAMPLE_RATE_HZ / FSR_SAMPLE_RATE_HZ), lost 0")


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--seconds", type=int, default=10)
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    selftest() if a.selftest else asyncio.run(run(a.seconds))
