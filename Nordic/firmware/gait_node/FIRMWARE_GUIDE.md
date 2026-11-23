# Firmware guide: โค้ด `gait_node` ทำงานอย่างไร และแก้ตรงไหน

อ่านไฟล์นี้ก่อนแก้โค้ดเซนเซอร์หรือ Bluetooth วิธี build อยู่ใน `WSL_BUILD.md`

สถานะ (2026-11-12): โค้ดชุดนี้ **build ผ่านบน NCS v3.3.4** แต่ส่วน FSR และ Bluetooth
**ยังไม่ได้ทดสอบกับบอร์ดจริง**

## 1. ภาพรวม

```
              ทุก 10 ms (วนใน main)
BMI270 ──► main.c ──► USB serial   "IMU,AX=...,GX=..."
                 
FSR402 ×6 ──► ADS7128 ──I2C──► main.c ──► USB serial   "FSR,t_ms=...,LH=..."
                                     └──► Bluetooth notify (18 byte) ──► แล็ปท็อป / i.MX95
```

ทุกอย่างทำงานใน loop เดียวของ `main()` ไม่มี thread แยก

| ไฟล์ | หน้าที่ |
|---|---|
| `src/main.c` | ตั้งค่า BMI270, เรียก FSR และ BLE, loop หลักทุก 10 ms, พิมพ์ค่าทาง USB serial |
| `src/plantar/fsr402.c` / `.h` | อ่าน FSR402 หกตัวผ่าน ADC ภายนอก **ADS7128** ทาง I2C |
| `src/ble/gait_ble.c` / `.h` | Bluetooth: ประกาศชื่อ `GaitNode`, GATT service, ส่ง packet |
| `src/common/gait_types.h` | struct ข้อมูลที่ใช้ร่วมกัน (`gait_imu_sample`, `gait_plantar_sample`) |
| `src/imu/imu.c` / `.h` | โมดูล IMU แบบแยกไฟล์ ถูก compile แต่ **`main.c` ยังไม่ได้เรียกใช้** |
| `app.overlay` | กำหนดขา I2C ภายนอก (P0.04 = SDA, P0.05 = SCL) และเปิด BMI270 |
| `prj.conf` | เปิดฟีเจอร์: I2C, SENSOR, Bluetooth และสวิตช์โมดูล `CONFIG_GAIT_*` |
| `Kconfig` | นิยามสวิตช์ `CONFIG_GAIT_IMU`, `CONFIG_GAIT_PLANTAR`, `CONFIG_GAIT_BLE` |
| `sysbuild.conf` | build firmware ของคอร์วิทยุ (network core) และเปิด Partition Manager |
| `CMakeLists.txt` | รายชื่อไฟล์ `.c` ที่ compile ตามสวิตช์ใน `prj.conf` |
| `README_ADS7128_WIRING.txt` | บันทึกการต่อสาย ADS7128 |

## 1.1 `src/main.c` ทำงานทีละขั้น

`main()` ทำตามลำดับนี้ (เลขบรรทัดอ้างอิงไฟล์ปัจจุบัน ถ้าแก้ไฟล์เลขอาจเลื่อน):

| ขั้น | บรรทัด | ทำอะไร | ถ้าพลาด |
|---:|---|---|---|
| 1 | 36–40 | รอให้เปิด serial port บนคอมพิวเตอร์ (สูงสุด 30 วินาที) แล้วรออีก 0.5 วินาที | ไม่มีใครเปิดก็เริ่มต่อหลังครบ 30 วินาที |
| 2 | 44–83 | ตั้งค่า BMI270: accel ±2 g, gyro ±500 dps, 100 Hz | พิมพ์ `BMI270 device is not ready` แล้วข้าม IMU ไปตลอด |
| 3 | 86–93 | เรียก `fsr402_init()` เพื่อเริ่ม ADS7128 ทาง I2C | พิมพ์ `FSR402 ADS7128 initialization failed` แล้วข้าม FSR ไปตลอด |
| 4 | 95–104 | เรียก `gait_ble_init()` เปิด Bluetooth และเริ่มประกาศชื่อ `GaitNode` (เมื่อ `CONFIG_GAIT_BLE=y`) | พิมพ์ `BLE init failed: rc=...` ส่วนอื่นทำงานต่อ |
| 5 | 108–171 | loop ไม่รู้จบ: พัก 10 ms → อ่าน IMU แล้วพิมพ์ → อ่าน FSR แล้วพิมพ์และส่ง Bluetooth | พิมพ์ `IMU read failed` หรือ `FSR read failed` แล้ววนต่อ |

ใน loop ขั้น 5 แต่ละรอบ:

1. `k_msleep(10)` พัก 10 ms
2. อ่าน BMI270 (`sensor_sample_fetch` แล้ว `sensor_channel_get`) พิมพ์ `IMU,AX=...,GX=...` ทาง USB
3. อ่าน FSR 6 ช่อง (`fsr402_read_all`) พิมพ์ `FSR,t_ms=...,LH=...,RT=...` ทาง USB
4. ใส่ค่า FSR ลง `struct gait_plantar_sample` แล้วเรียก `gait_ble_send_plantar()`
   ถ้ายังไม่มีอุปกรณ์มา subscribe ฟังก์ชันคืน `-ENOTCONN` และโค้ดไม่สนใจค่านี้

จุดที่ควรรู้:

- **รอบละมากกว่า 10 ms:** `k_msleep(10)` พักก่อนทำงานทุกครั้ง ไม่ได้ชดเชยเวลาที่อ่าน I2C และพิมพ์ใช้ไป ความถี่จริงจึงต่ำกว่า 100 Hz ให้ดูจาก `t_ms`
- **ค่า IMU ไม่ถูกส่ง Bluetooth:** ใน loop มีแค่ `gait_ble_send_plantar()` ยังไม่มีการเรียก `gait_ble_send_imu()`
- **โค้ด BMI270 อยู่ใน `main.c` เอง:** ไม่ได้เรียก `imu_init()` / `imu_read()` จาก `src/imu/imu.c`
- **โค้ดส่วน Bluetooth ครอบด้วย `#ifdef CONFIG_GAIT_BLE`:** ถ้าตั้ง `CONFIG_GAIT_BLE=n` โค้ดส่วนนี้ไม่ถูก compile
- **ขนาด stack ของ main:** ตั้งไว้ 4096 ใน `prj.conf` (`CONFIG_MAIN_STACK_SIZE`) เพราะ `printf` ทศนิยมกับ Bluetooth ใช้ stack มาก ถ้าบอร์ดรีเซ็ตเองให้เพิ่มค่านี้

## 2. IMU (BMI270)

BMI270 อยู่บนบอร์ด Thingy:53 อยู่แล้ว ไม่ต้องต่อสาย

| สิ่งที่ต้องการ | แก้ที่ไหน |
|---|---|
| เปิดใช้อุปกรณ์ | `app.overlay`: `&bmi270 { status = "okay"; };` |
| ดึง device ในโค้ด | `main.c`: `DEVICE_DT_GET_ONE(bosch_bmi270)` |
| ช่วงวัดและความถี่ | `main.c` ตอนต้น `main()` (ชุดคำสั่ง `sensor_attr_set`) |
| รูปแบบที่พิมพ์ | `main.c` ใน loop: `IMU,AX=...,AY=...,AZ=...,GX=...,GY=...,GZ=...` |

ค่าที่ตั้งไว้:

- Accelerometer ±2 g, หน่วยที่พิมพ์เป็น m/s²
- Gyroscope ±500 dps, หน่วยที่พิมพ์เป็น rad/s
- ความถี่ 100 Hz
- ลำดับสำคัญ: ตั้ง full scale และ oversampling ก่อน แล้วตั้ง sampling frequency เป็นอย่างสุดท้าย

ค่า IMU **ยังไม่ถูกส่งผ่าน Bluetooth** (ดูหัวข้อ 6)

## 3. FSR402 (ผ่าน ADS7128)

FSR402 หกตัวต่อเข้า ADC ภายนอก Adafruit ADS7128 (12 bit, 8 ช่อง) ไม่ได้ใช้ ADC ในตัวชิป nRF5340

### การต่อสาย

| Thingy:53 | ADS7128 |
|---|---|
| P0.04 (EXP_BOARD_PIN1) | SDA |
| P0.05 (EXP_BOARD_PIN2) | SCL |
| 3V3 (VDD_EXP_BOARD) | VIN |
| GND | GND |

- I2C address ของ ADS7128: `0x10` (ค่าจากโรงงาน ไม่ต้องบัดกรี jumper)
- ใช้บัส `i2c0` แยกจากบัสเซนเซอร์บนบอร์ด เพราะบนบอร์ดมี BMM150 ที่ address `0x10` อยู่แล้ว

### ลำดับช่อง

| ช่อง ADS7128 | ตำแหน่ง | ชื่อใน serial | index ใน `raw[]` |
|---|---|---|---:|
| A0 | ส้นเท้าซ้าย | `LH` | 0 |
| A1 | ฝ่าเท้าซ้าย | `LB` | 1 |
| A2 | นิ้วเท้าซ้าย | `LT` | 2 |
| A3 | ส้นเท้าขวา | `RH` | 3 |
| A4 | ฝ่าเท้าขวา | `RB` | 4 |
| A5 | นิ้วเท้าขวา | `RT` | 5 |

A6 และ A7 ว่าง

### แก้ที่ไหน

| สิ่งที่ต้องการ | แก้ที่ไหน |
|---|---|
| ขา I2C | `app.overlay`: `i2c0_ext_default` (`NRF_PSEL(TWIM_SDA, 0, 4)`, `TWIM_SCL, 0, 5`) |
| I2C address | `fsr402.c`: `ADS7128_I2C_ADDR` |
| จำนวนช่อง | `fsr402.h`: `FSR402_CHANNEL_COUNT` (ตอนนี้ 6) |
| ตั้งค่า ADC | `fsr402.c`: `fsr402_init()` |
| อ่านค่า | `fsr402.c`: `fsr402_read_all(uint16_t raw[6])` |
| เปิด/ปิดโมดูล | `prj.conf`: `CONFIG_GAIT_PLANTAR=y` และ `CONFIG_I2C=y` |

ค่าที่ได้เป็น raw ADC 12 bit (0–4095) ยังไม่ได้แปลงเป็นแรง

### ข้อควรเช็กก่อนต่อสายจริง

- FSR402 ทุกตัวต้องต่อเป็น **voltage divider** (FSR + ตัวต้านทานคงที่) ห้ามต่อ FSR เปล่าเข้า ADS7128
- แรงดันที่ขา A0–A5 ต้องอยู่ระหว่าง 0 ถึง 3.3 V
- ถ้า serial ขึ้น `FSR402 ADS7128 initialization failed` ให้เช็กสาย SDA/SCL และไฟเลี้ยงก่อน

## 4. Bluetooth

| รายการ | ค่า |
|---|---|
| ชื่อที่ประกาศ | `GaitNode` (`CONFIG_BT_DEVICE_NAME` ใน `prj.conf`) |
| Service UUID | `e6d1e9d9-be0e-4889-b490-7643045692db` |
| IMU (notify) | `e6d1e9da-be0e-4889-b490-7643045692db` |
| Plantar / FSR (notify) | `e6d1e9db-be0e-4889-b490-7643045692db` |

รายละเอียด protocol ฉบับเต็มอยู่ที่ `../../protocol/BLE_GAIT_PROTOCOL.md` ถ้าแก้ packet ต้องแก้เอกสารนั้นและสคริปต์ฝั่งรับให้ตรงกัน

### Packet ของ FSR (18 byte, little-endian)

| Offset | ชนิด | ข้อมูล |
|---:|---|---|
| 0 | uint16 | เลขลำดับ (เพิ่มทีละ 1, วนที่ 65536) |
| 2 | uint32 | `t_ms` เวลาตั้งแต่บอร์ดเปิด |
| 6 | int16 ×6 | ค่า ADC: LH, LB, LT, RH, RB, RT |

ฝั่ง Python แกะด้วย `struct.unpack("<HI6h", data)`

### ฟังก์ชันใน `gait_ble.c`

| ฟังก์ชัน | ทำอะไร | ถูกเรียกจาก |
|---|---|---|
| `gait_ble_init()` | เปิด Bluetooth และเริ่มประกาศ | `main.c` ก่อนเข้า loop |
| `gait_ble_send_plantar()` | ส่งค่า FSR หนึ่ง packet | `main.c` หลังอ่าน FSR สำเร็จ |
| `gait_ble_send_imu()` | ส่งค่า IMU หนึ่ง packet | **ยังไม่มีใครเรียก** |

ถ้ายังไม่มีอุปกรณ์มา subscribe ฟังก์ชันส่งจะคืน `-ENOTCONN` และ `main.c` ไม่สนใจค่านี้

## 5. ดูค่าได้อย่างไร

| วิธี | ทำอย่างไร |
|---|---|
| USB serial | เปิด serial terminal ที่ 115200 baud จะเห็นบรรทัด `IMU,...` และ `FSR,...` |
| Bluetooth บนแล็ปท็อป | `pip install bleak` แล้วรัน `python ../../edge_receiver/plantar_rx.py` |
| มือถือ | แอป nRF Connect → เชื่อมต่อ `GaitNode` → เปิด notify ที่ characteristic ลงท้าย `e9db` (เห็นเป็นเลขฐานสิบหก) |

## 6. สิ่งที่ยังไม่ได้ทำ / ข้อจำกัด

- **รอ USB 30 วินาที:** `main.c` รอให้เปิด serial port ก่อนเริ่ม (สูงสุด 30 วินาที) ถ้าใช้แบตเตอรี่โดยไม่ต่อ USB บอร์ดจะเริ่มช้า
- **อัตราจริงน่าจะต่ำกว่า 100 Hz:** loop พัก 10 ms แล้วยังอ่าน I2C หกช่องและพิมพ์ serial ให้วัดจาก `t_ms`
- **IMU ยังไม่ส่งผ่าน Bluetooth:** ต้องเติมค่าใน `struct gait_imu_sample` แล้วเรียก `gait_ble_send_imu()` ใน loop
- **`src/imu/imu.c` ซ้ำกับโค้ดใน `main.c`:** ควรเลือกใช้อย่างใดอย่างหนึ่ง
- **ไม่มี thread แยก:** ถ้า Bluetooth ส่งไม่ทัน loop ทั้งหมดจะช้าลงตาม

## 7. เปิด/ปิดโมดูล

แก้ใน `prj.conf`:

```
CONFIG_GAIT_IMU=y        # BMI270
CONFIG_GAIT_PLANTAR=y    # FSR402 ผ่าน ADS7128 (ต้องมี CONFIG_I2C=y)
CONFIG_GAIT_BLE=y        # Bluetooth (ต้องมีกลุ่ม CONFIG_BT_* ด้านล่างด้วย)
```

หมายเหตุ: `main.c` ตอนนี้เรียก `fsr402_init()` เสมอ ถ้าตั้ง `CONFIG_GAIT_PLANTAR=n` จะ link ไม่ผ่าน
ต้องครอบโค้ด FSR ใน `main.c` ด้วย `#ifdef CONFIG_GAIT_PLANTAR` ก่อน

## 8. Build และ flash

- เวอร์ชัน: **NCS v3.3.4** เท่านั้น ห้ามสลับกับ v3.4.0
- บอร์ด: `thingy53/nrf5340/cpuapp` พร้อม `--sysbuild`
- ขั้นตอนทั้งหมดอยู่ใน `WSL_BUILD.md`
- ไฟล์ที่ใช้ flash: `build\dfu_application.zip`
- หลัง flash ต้องเห็นชื่อ `GaitNode` ในแอป nRF Connect
