# GATT + ข้อมูลจำลอง (mock) — ทดสอบ BLE โดยไม่ต้องต่อเซนเซอร์

ปรับจาก `gatt_demo.zip` เข้ากับโค้ดจริงของ `gait_node` (ไม่ได้สร้างโปรเจกต์แยก)

## 1. GATT คืออะไร (สั้น ๆ)

BLE จัดข้อมูลเป็น **service → characteristic** แต่ละ characteristic คือ "ช่อง" ที่กำหนดสิทธิ์ได้
(อ่าน / เขียน / notify = บอร์ดส่งให้เองเมื่อมีค่าใหม่ ผู้รับต้อง subscribe ก่อน)

service ของเรา `e6d1e9d9-be0e-4889-b490-7643045692db`:

| ช่อง | UUID (ตัวที่ 4–8 ต่างกัน) | สิทธิ์ | เนื้อหา |
|---|---|---|---|
| IMU | `e6d1e9da-…` | notify | 18 byte: seq, t_ms, acc×3, gyro×3 |
| Plantar | `e6d1e9db-…` | notify | 18 byte: seq, t_ms, FSR×6 |
| Control | `e6d1e9dc-…` | write 1 byte | 0 = stop, 1 = start, 2 = calibrate (ยังเป็น stub) |
| Status | `e6d1e9dd-…` | read 1 byte | 0 = หยุดอยู่, 1 = กำลังส่ง |

Packet เป็น little-endian, Python decode ด้วย `"<HI6h"`

## 2. อะไรที่เอามาจาก gatt_demo และอะไรที่ไม่ได้เอา

| รายการ | ที่ทำ |
|---|---|
| ช่อง **Control** (write) และ **Status** (read) | เพิ่มเข้า `src/ble/gait_ble.c` ตรวจความยาว = 1 และค่า 0..2 ไม่ตรงจะตอบ ATT error |
| **mock signal** (เดิน ก้าวละ 1.1 s) | `src/mock/mock_signal.c` ใช้ type ของโปรเจกต์ (`imu_sample`, `plantar_sample`) |
| **mock thread** | `src/mock/mock_thread.c` ส่ง plantar ผ่าน `data_queue` → `ble_thread` เหมือนข้อมูลจริง ส่วน IMU ส่งตรงเข้า `gait_ble` |
| buffer ส่ง BLE | เพิ่มใน `prj.conf` |
| **ไม่เอา** packet 16 byte ของ demo | คง 18 byte ที่มี **seq** (ฝั่งรับใช้ตรวจแพ็กเก็ตหาย) และ UUID เดิมตาม `protocol/BLE_GAIT_PROTOCOL.md` |
| **ไม่เอา** `main.c` และ service แยกของ demo | ใช้ `gait_ble.c` เดิม (ไม่งั้นมี service ซ้ำสองชุด) |

เมื่อ Control = STOP, `gait_ble_send_*` คืน `-EPERM` และไม่ส่ง; START ส่งต่อ (ค่าเริ่มต้น = ส่งเลย)

## 3. เปิด/ปิด mock

`prj.conf`:

```
CONFIG_GAIT_MOCK_DATA=y   # ข้อมูลปลอม ไม่อ่านเซนเซอร์ (ใช้ทดสอบ BLE)
CONFIG_GAIT_MOCK_DATA=n   # ข้อมูลจริงจาก BMI270 + ADS7128
```

เปิด mock แล้ว `main.c` จะข้าม `imu_init()` / `fsr402_init()` / thread เซนเซอร์จริง
และเริ่ม mock thread แทน (100 Hz ตาม `system_config.h`) — **ปิดก่อนวัดจริงเสมอ**

## 4. ผลลัพธ์ที่ควรเห็น

### 4.1 บอร์ด (USB serial)

```
MOCK DATA MODE: sensors are NOT read, BLE sends fake data
BLE: advertising as GaitNode
Mock data thread started
BLE consumer thread started
=== GAIT NODE SAMPLING STARTED ===
BLE: connected
BLE: IMU notifications ON
BLE: plantar notifications ON
BLE: command 1 (streaming ON)
MOCK: t=12345 ms, IMU notifications sent 100, failed 0     <- ทุก 1 วินาที
```

`failed` ต้องเป็น 0 ถ้าไม่ใช่ = buffer ส่งเต็ม (ดูข้อ 7)

### 4.2 ข้อมูลที่ไหลเข้า (หน่วยและช่วงค่า)

| ช่อง | ค่า | ความหมาย |
|---|---|---|
| acc | x ±1.5, y ±0.8, **z ≈ 9.81 ± 2.5** m/s² | z มีแรงโน้มถ่วง + การกระเด้ง |
| gyro | y ±3.0 rad/s (แกว่งขา) | x, z ค่าเล็กกว่า |
| FSR | 0 .. 3500 (นับ ADC 12 bit ไม่เกิน 4095) | ส้นเท้า (LH) ขึ้นก่อน ตามด้วยตรงกลางเท้า (LB) และนิ้ว (LT) เท้าขวา (RH,RB,RT) ตามหลังครึ่งก้าว (0.55 s) |

รอบเดิน 1.1 s → ดูใน plot จะเห็นลูกคลื่นซ้ำทุก 1.1 วินาที

### 4.3 ฝั่งรับ (Python)

```
status (0 idle, 1 streaming) = 1
IMU     seq=0 t=3010ms acc=(0.0, 0.0, 9.81) m/s2 gyro=(...) rad/s
PLANTAR seq=0 t=3010ms LH,LB,LT,RH,RB,RT=[...]
IMU: 100 packets/s, lost 0
PLANTAR: 100 packets/s, lost 0
```

## 5. ทดสอบเองด้วยมือ (3 ระดับ)

### ระดับ 0 — ไม่ใช้ Bluetooth เลย (ทดสอบ packet format / decoder)

```bash
python edge_receiver/gatt_test_client.py --selftest
```

สร้าง packet ปลอมด้วย `struct.pack("<HI6h", ...)`, decode กลับ และทดสอบตรวจ seq หาย/วนรอบ 65535
ผ่านแล้วบนเครื่องนี้ (`selftest OK`)

### ระดับ 1 — มือถือ (nRF Connect for Mobile) ดูค่าดิบ

1. แฟลชบอร์ดด้วย `CONFIG_GAIT_MOCK_DATA=y`
2. Scan → เจอ **GaitNode** → Connect
3. เปิด service `e6d1e9d9…` แล้วกดลูกศรสามหัว (subscribe) ที่ IMU / Plantar → ค่า hex 18 byte ไหลเข้า
4. อ่าน Status (ควรได้ `01`)
5. **เขียน Control:** กด write ค่า `00` → ค่าหยุดไหล, Status อ่านได้ `00`; เขียน `01` → ไหลต่อ; เขียน `05` หรือ 2 byte → ได้ error (ถูกต้อง)

ตัวอย่างอ่าน hex (little-endian): `00 00 | D2 04 00 00 | DC 05 ...` → seq=0, t_ms=0x4D2=1234, ค่าแรก=0x05DC=1500

### ระดับ 2 — คอมพิวเตอร์ (Python + bleak)

```bash
pip install bleak
python edge_receiver/gatt_test_client.py --seconds 10
```

(Windows ต้องมี Bluetooth LE; ฝั่ง i.MX95 ใช้สคริปต์เดียวกันได้)

## 6. ลำดับทดสอบที่แนะนำ

1. `--selftest` → decoder ถูก
2. mock + มือถือ → BLE/GATT บนบอร์ดถูก (แยกปัญหา "BLE ผิด" ออกจาก "เซนเซอร์/สายผิด")
3. mock + Python → ความเร็ว 100 pkt/s, lost = 0
4. `CONFIG_GAIT_MOCK_DATA=n` → ต่อเซนเซอร์จริงและเทียบรูปร่างสัญญาณ

## 7. ปัญหาที่อาจเจอ

| อาการ | แก้ |
|---|---|
| `failed` > 0 | เพิ่ม `CONFIG_BT_BUF_ACL_TX_COUNT` / `CONFIG_BT_L2CAP_TX_BUF_COUNT` (ต้องน้อยกว่า `CONFIG_BT_BUF_EVT_RX_COUNT`) หรือลดอัตราใน `system_config.h` |
| lost > 0 ฝั่งรับ | connection interval ยาวเกิน/สัญญาณรบกวน ลดอัตราส่งหรือรวมหลาย sample ต่อ packet (ยังไม่ได้ทำ) |
| scan ไม่เจอ | ปิด/เปิดบอร์ด; ถ้ามีเครื่องอื่นเชื่อมอยู่แล้วจะไม่ advertise |
| ข้อมูลกลายเป็นชุดเดิมตลอด | ลืมปิด mock แล้วเอาไปวัดจริง |
| Control เขียนได้ทุกคน | ตอนนี้ไม่มีการเข้ารหัส/จับคู่ ใช้งานจริงควรเปลี่ยนเป็น `BT_GATT_PERM_WRITE_ENCRYPT` |
