# Build firmware `gait_node` ด้วย Docker (NCS v3.3.4)

Build firmware ของ Thingy:53 ใน container Ubuntu โดยใช้ **nRF Connect SDK v3.3.4 เท่านั้น**

สถานะการทดสอบ (2026-11-12):

- ขั้น 1–3 (สร้าง container, ติดตั้งแพ็กเกจ, ติดตั้ง `nrfutil`, เห็น v3.3.4 ในรายการ) ทดสอบแล้ว ใช้ได้
- ขั้น 4–6 (โหลด SDK และ build ใน container) **ยังไม่ได้ทดสอบ**
- โค้ดชุดนี้ build ผ่านแล้วบน Windows โดยตรงด้วย v3.3.4 (ดูหัวข้อ "ไม่ใช้ Docker ก็ได้")

## ก่อนเริ่ม

- เปิด **Docker Desktop** ให้ทำงานอยู่
- ต้องมีที่ว่างประมาณ **15 GB** บนไดรฟ์ที่ Docker เก็บข้อมูล ค่าเริ่มต้นคือไดรฟ์ C:
  ถ้า C: เหลือน้อย ย้ายก่อนที่ Docker Desktop → Settings → Resources → Disk image location (เช่น `E:\docker`)
- เปิด PowerShell

## ครั้งแรก (ทำครั้งเดียว ใช้เวลา 30–50 นาที)

### ขั้น 1: สร้าง container

รันใน PowerShell:

```powershell
cd D:\Kosen\gaitAI\AI_GaitPBL\nordic\firmware\gait_node
docker run -it --name ncs -v ${PWD}:/workspace -v ncs-data:/root/ncs ubuntu:latest bash
```

| ส่วนของคำสั่ง | ความหมาย |
|---|---|
| `--name ncs` | ตั้งชื่อ container เพื่อกลับเข้ามาใช้ซ้ำ |
| `-v ${PWD}:/workspace` | โฟลเดอร์ `gait_node` บน Windows จะเห็นใน container ที่ `/workspace` |
| `-v ncs-data:/root/ncs` | เก็บ SDK ไว้ใน volume ชื่อ `ncs-data` ไม่ต้องโหลดซ้ำ |

หลังรันจะเห็น prompt เป็น `root@...:/#` แปลว่าอยู่ใน container แล้ว เช็กว่าเห็นโปรเจกต์:

```bash
ls /workspace
```

ต้องเห็น `CMakeLists.txt  prj.conf  sysbuild.conf  app.overlay  src`

ถ้าขึ้นว่าชื่อ `ncs` ถูกใช้แล้ว ให้ข้ามไปหัวข้อ "ทุกครั้งที่ build" หรือลบ container เก่าด้วย `docker rm ncs`

### ขั้น 2: ติดตั้งแพ็กเกจ (ใน container)

```bash
apt update
apt install -y git curl ca-certificates unzip xz-utils bzip2 libusb-1.0-0 libncurses6
```

### ขั้น 3: ติดตั้ง nrfutil

```bash
curl -L https://files.nordicsemi.com/artifactory/swtools/external/nrfutil/executables/x86_64-unknown-linux-gnu/nrfutil -o /usr/local/bin/nrfutil
chmod +x /usr/local/bin/nrfutil
nrfutil install sdk-manager
nrfutil sdk-manager search
```

คำสั่งสุดท้ายแสดงรายการเวอร์ชัน ต้องมีบรรทัด `nrf  v3.3.4  Available`

### ขั้น 4: โหลด SDK และ toolchain v3.3.4 (นาน 20–40 นาที)

```bash
nrfutil sdk-manager install v3.3.4 --install-dir /root/ncs
nrfutil sdk-manager toolchain install --ncs-version v3.3.4 --install-dir /root/ncs
ls /root/ncs
```

ต้องเห็น `v3.3.4` และ `toolchains` (ถ้าคำสั่งที่สองบอกว่าติดตั้งแล้ว ไม่เป็นไร)

### ขั้น 5: อนุญาตให้ git อ่านโฟลเดอร์ที่เมานต์มา

```bash
git config --global --add safe.directory '*'
```

ตั้งค่าครั้งแรกเสร็จแล้ว ทำขั้น 6 ต่อได้เลย

## ทุกครั้งที่ build

ถ้าออกจาก container ไปแล้ว กลับเข้ามาด้วย (PowerShell):

```powershell
docker start -ai ncs
```

### ขั้น 6: Build (ใน container)

```bash
nrfutil sdk-manager toolchain launch --ncs-version v3.3.4 --install-dir /root/ncs --shell
export ZEPHYR_BASE=/root/ncs/v3.3.4/zephyr
cd /workspace
west build -p always -b thingy53/nrf5340/cpuapp --sysbuild . --build-dir /workspace/build
```

| คำสั่ง | ทำอะไร |
|---|---|
| บรรทัด 1 | เข้า shell ที่มี `west`, `cmake` และคอมไพเลอร์ของ v3.3.4 |
| บรรทัด 2 | บอกว่า SDK อยู่ที่ไหน |
| บรรทัด 4 | build ใหม่ทั้งหมด (`-p always`) รวม firmware ของคอร์วิทยุ (`--sysbuild`) |

สำเร็จเมื่อบรรทัดท้าย ๆ ขึ้น `Generating ../dfu_application.zip`

ไฟล์ที่ได้อยู่บน Windows ที่:

```
D:\Kosen\gaitAI\AI_GaitPBL\nordic\firmware\gait_node\build\dfu_application.zip
```

ออกจาก container: พิมพ์ `exit` สองครั้ง (ครั้งแรกออกจาก toolchain shell)

## Flash (ทำบน Windows ไม่ใช่ใน Docker)

1. ต่อ Thingy:53 ด้วยสาย USB-C ที่ส่งข้อมูลได้
2. เข้าโหมด bootloader: ปิดสวิตช์ SW1, กดปุ่ม SW2 ค้างไว้, เปิด SW1, แล้วปล่อย SW2
3. เปิด nRF Connect for Desktop → **Programmer** → เลือก Thingy:53
4. Add file → `build\dfu_application.zip` → **Write**

ตรวจผล: สแกนด้วยแอป nRF Connect for Mobile ต้องเห็นชื่อ **`GaitNode`**
รายละเอียดเพิ่มเติมอยู่ใน `../../FLASHING.md`

## แก้ปัญหาที่พบบ่อย

| อาการ | สาเหตุ / วิธีแก้ |
|---|---|
| `west: command not found` | ยังไม่ได้เข้า toolchain shell ทำบรรทัด 1 ของขั้น 6 ใหม่ |
| `CMakeCache.txt directory ... is different` | โฟลเดอร์ `build` ถูกสร้างจากที่อื่น (เช่น build บน Windows มาก่อน) ลบโฟลเดอร์ `build` ทั้งโฟลเดอร์แล้ว build ใหม่ |
| `doesn't contain a CMakeLists.txt` | เมานต์ผิดโฟลเดอร์ ต้องรันขั้น 1 จากในโฟลเดอร์ `gait_node` |
| `undefined node label 's0_partition'` | ขาด `sysbuild.conf` (ต้องมี `SB_CONFIG_PARTITION_MANAGER=y`) |
| `include could not find ... toolchain/zephyr/gnu/generic.cmake` | ใช้ toolchain ผิดเวอร์ชัน ต้องเป็น v3.3.4 ทั้ง SDK และ toolchain |
| `fatal: detected dubious ownership` | ยังไม่ได้ทำขั้น 5 |
| ที่เต็มระหว่างโหลด SDK | ย้าย Disk image location ของ Docker Desktop ไปไดรฟ์ที่ว่าง แล้วทำขั้น 4 ใหม่ |
| build ช้ามาก | ปกติสำหรับโฟลเดอร์ Windows ที่เมานต์เข้า container ครั้งแรกอาจนาน 10–20 นาที |

## ไม่ใช้ Docker ก็ได้ (วิธีที่ทดสอบแล้วบนเครื่องนี้)

SDK v3.3.4 และ toolchain ติดตั้งอยู่ที่ `E:\ncs` แล้ว และโค้ดชุดนี้ build ผ่านด้วยวิธีนี้

- **VS Code:** ส่วนขยาย nRF Connect → ตั้ง SDK และ toolchain ให้ชี้ไปที่ `E:\ncs` → เปิดโฟลเดอร์ `gait_node` → Add build configuration (board `thingy53/nrf5340/cpuapp`, เปิด sysbuild) → Build
- **Terminal:** เปิดจาก nRF Connect → Open terminal แล้วรัน

```
west build -p always -b thingy53/nrf5340/cpuapp --sysbuild
```

ข้อควรจำ: ต้องรันจากในโฟลเดอร์ `gait_node` และห้ามใช้โฟลเดอร์ `build` เดียวกันสลับกับ Docker โดยไม่ลบก่อน

## กติกาเรื่องพื้นที่

- มี build folder เดียวคือ `gait_node\build` (ประมาณ 100 MB) ลบได้เสมอ build ใหม่ได้ทุกเมื่อ
- `.gitignore` มี `build*/` แล้ว build จะไม่ติดไปกับ git
- ห้ามเก็บ SDK ไว้ในโฟลเดอร์โปรเจกต์ ใน Docker อยู่ที่ volume `ncs-data` บน Windows อยู่ที่ `E:\ncs`
- เลิกใช้ Docker แล้วอยากได้พื้นที่คืน: `docker rm ncs` แล้ว `docker volume rm ncs-data`
