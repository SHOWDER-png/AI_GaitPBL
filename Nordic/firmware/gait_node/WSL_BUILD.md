# Build gait_node บน Linux (WSL2 Ubuntu)

ใช้แทน Docker (`DOCKER_BUILD.md` เลิกใช้แล้ว) — build บน Linux จริงใน WSL เร็วกว่า Windows
เพราะไฟล์ SDK และโปรเจกต์อยู่บนดิสก์ Linux (ext4) ไม่ต้องข้าม `/mnt/c` หรือ `/mnt/d`

- SDK: nRF Connect SDK **v3.3.4** (ห้ามใช้ v3.4.0)
- Board: `thingy53/nrf5340/cpuapp`, ต้องใส่ `--sysbuild`
- ผลลัพธ์: `build/dfu_application.zip` → แฟลชด้วย Programmer app บน Windows

> สถานะ: ขั้นตอนที่ 0–2 ทดสอบแล้ว ส่วนคำสั่ง `toolchain launch` (ขั้นตอนที่ 3) และสคริปต์
> `wsl_build.sh` **ยังไม่เคยรันจริง** ถ้า error ดูหัวข้อ "แก้ปัญหา"

---

## 0. สิ่งที่ต้องมี (ทำครั้งเดียว)

Ubuntu บน WSL2 (ตรวจ: `wsl -l -v` ต้องเห็น `Ubuntu  Running/Stopped  2`)

ติดตั้งแพ็กเกจระบบ (ต้องใช้รหัส sudo ของคุณเอง):

```bash
sudo apt update
sudo apt install -y git cmake ninja-build gcc unzip bzip2 curl rsync \
    libusb-1.0-0 libncurses6 python3 python3-pip python3-venv
```

> ถ้า toolchain ของ Nordic มี cmake/ninja/compiler มาในตัว บางตัวอาจไม่ต้องใช้ แต่ลงไว้ไม่เสียหาย

## 1. ติดตั้ง nrfutil

```bash
mkdir -p ~/.local/bin ~/ncs ~/work
curl -L https://developer.nordicsemi.com/.pc-tools/nrfutil/x64-linux/nrfutil -o ~/.local/bin/nrfutil
chmod +x ~/.local/bin/nrfutil
echo 'export PATH=$HOME/.local/bin:$PATH' >> ~/.bashrc
source ~/.bashrc

nrfutil install sdk-manager
```

## 2. ติดตั้ง SDK v3.3.4 + toolchain (หลาย GB ใช้เวลานาน)

```bash
nrfutil sdk-manager install v3.3.4 --install-dir ~/ncs
nrfutil sdk-manager toolchain install --ncs-version v3.3.4 --install-dir ~/ncs
```

ผลที่ได้: `~/ncs/v3.3.4` (SDK) และ `~/ncs/toolchains/...` (compiler, cmake, west)
ถ้าดาวน์โหลดล้มเหลวให้รันซ้ำอีกครั้ง

**พื้นที่ดิสก์:** WSL เก็บไฟล์ทั้งหมดใน `ext4.vhdx` บนไดรฟ์ C:
ที่ `C:\Users\<คุณ>\AppData\Local\wsl\{...}\ext4.vhdx` — SDK + toolchain กินประมาณ 10–15 GB
ตรวจพื้นที่ว่างบน C: ก่อน ถ้าไม่พอให้ย้าย distro ไป drive อื่น (`wsl --export` / `wsl --import`)

## 3. Build

### วิธีเร็ว: ใช้สคริปต์ `wsl_build.sh`

จากโฟลเดอร์ `gait_node` ใน WSL:

```bash
cd /mnt/d/Kosen/gaitAI/AI_GaitPBL/nordic/firmware/gait_node
bash wsl_build.sh            # build ต่อเนื่อง (เร็ว)
bash wsl_build.sh pristine   # build ใหม่หมด (ช้า ใช้เมื่อเปลี่ยน Kconfig/overlay/sysbuild.conf แล้วแปลก)
```

สคริปต์จะ: sync โค้ดจาก Windows → `~/work/gait_node` (ดิสก์ Linux) → build →
คัดลอก `dfu_application.zip` กลับมาที่ `gait_node/build/wsl/`

### วิธีมือ (ถ้าอยากรู้ว่าทำอะไรอยู่)

```bash
# 1) sync โค้ดไปดิสก์ Linux (เร็วกว่า build บน /mnt/d มาก)
rsync -a --delete --exclude 'build*/' \
  /mnt/d/Kosen/gaitAI/AI_GaitPBL/nordic/firmware/gait_node/ ~/work/gait_node/

# 2) เข้า toolchain แล้ว build (รันจากในโฟลเดอร์โปรเจกต์)
cd ~/work/gait_node
nrfutil sdk-manager toolchain launch --ncs-version v3.3.4 --install-dir ~/ncs -- \
  west build -b thingy53/nrf5340/cpuapp --sysbuild -d build
```

> `west` ต้องรันจากในโฟลเดอร์โปรเจกต์เสมอ ไม่ใช่จากโฟลเดอร์ SDK

## 4. เอา firmware ไปแฟลช (บน Windows)

ไฟล์อยู่ที่ `gait_node\build\wsl\dfu_application.zip` (ถ้าใช้สคริปต์)
หรือใน WSL: `~/work/gait_node/build/dfu_application.zip` — เปิดจาก Windows ผ่าน
`\\wsl$\Ubuntu\home\<user>\work\gait_node\build\dfu_application.zip`

1. ใช้ nRF Connect for Desktop → **Programmer**
2. เข้า MCUboot: ปิดบอร์ด กดปุ่ม **SW1** ค้างไว้ ขณะเสียบ USB
3. เลือก Thingy:53 → **Add file** → `dfu_application.zip` → **Write**

Thingy:53 ไม่มี debugger on-board จึงแฟลชผ่าน USB DFU เท่านั้น

## 5. ตรวจว่า build ถูกต้อง

- บรรทัดท้ายต้องไม่มี `error` และมี `Generating ... dfu_application.zip`
- ขนาด zip ประมาณ 300 KB
- ถ้าลืม `--sysbuild` จะได้เฉพาะ app core โดยไม่มี network core (`ipc_radio`) → BLE ไม่ทำงาน

## 6. แก้ปัญหา

| อาการ | สาเหตุ / วิธีแก้ |
|---|---|
| `nrfutil: command not found` | ยังไม่ได้ `source ~/.bashrc` หรือ PATH ไม่มี `~/.local/bin` |
| ดาวน์โหลด SDK ล้มเหลว (`Failed to download tarball`) | เครือข่ายหลุดกลางทาง รันคำสั่งซ้ำ |
| `cmake`/`ninja`/`gcc` not found | ยังไม่ได้ `sudo apt install` ตามข้อ 0 |
| `toolchain launch` ไม่รู้จักตัวเลือก | ดูไวยากรณ์จริง: `nrfutil sdk-manager toolchain launch --help` |
| `path is on mount` (west) | รัน west จากนอกโฟลเดอร์โปรเจกต์ หรือโปรเจกต์อยู่คนละไดรฟ์กับ SDK → ใช้ `~/work/gait_node` |
| CMakeCache path mismatch | โฟลเดอร์ `build/` ถูกคัดลอกมาจาก Windows → ลบแล้ว build ใหม่ (สคริปต์ exclude `build*/` ให้แล้ว) |
| build ช้า | ห้ามสั่ง build บน `/mnt/d` ตรง ๆ ใช้ `~/work`; ไม่ต้องใส่ `-p always`; จำกัดหน่วยความจำ WSL 7 GB ถ้า OOM ให้เพิ่มใน `%UserProfile%\.wslconfig` |
| แก้ `prj.conf`/overlay แล้วไม่เปลี่ยน | ใช้ `bash wsl_build.sh pristine` |
| แฟลชแล้วบอร์ดไม่ทำงาน | ตรวจ `sysbuild.conf` (ต้องมี `SB_CONFIG_PARTITION_MANAGER=y` และ ipc_radio) และใช้ SDK v3.3.4 เท่านั้น |

## 7. ลบ/ล้าง

```bash
rm -rf ~/work/gait_node/build        # ล้างผล build
nrfutil sdk-manager uninstall --help # ดูวิธีถอน SDK/toolchain
```

หลังลบไฟล์ใน WSL พื้นที่บน C: จะไม่คืนทันที ต้อง compact vhdx (`wsl --shutdown` แล้ว `Optimize-VHD`)
