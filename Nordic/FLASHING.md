# Building and flashing the Thingy:53

How to put `firmware/gait_node` (or any app in `firmware/experiments/`) onto a real
Nordic Thingy:53. Team baseline: **nRF Connect SDK v3.3.4** (v3.4.0 failed to build in C29).

## 1. One-time setup (each laptop)

1. Install **nRF Connect for VS Code** (VS Code extension pack from Nordic), or the
   command-line `nrfutil` with the `sdk-manager` command.
2. Through it, install **nRF Connect SDK v3.3.4** and the **matching toolchain v3.3.4**.
3. Install **nRF Connect for Desktop** and, inside it, the **Programmer** app.
4. Windows only: if the Thingy:53 doesn't appear as a COM port, install the drivers that
   nRF Connect for Desktop offers.

## 2. Build

### Option A: VS Code (easiest)

1. nRF Connect panel → **Open an existing application** → pick `Nordic/firmware/gait_node`
   (or an experiment folder).
2. **Add build configuration**:
   - SDK: v3.3.4, Toolchain: v3.3.4
   - Board target: `thingy53/nrf5340/cpuapp`
   - Leave **sysbuild** on (default)
3. Click **Build**.

### Option B: command line

Open a terminal with the v3.3.4 toolchain loaded (nRF Connect panel → **Open terminal**,
or `nrfutil sdk-manager toolchain launch --ncs-version v3.3.4 --shell`), then:

```
cd Nordic/firmware/gait_node
west build -b thingy53/nrf5340/cpuapp --sysbuild -p
```

`-p` does a clean build. Leave it off for faster rebuilds.

### Output

The file to flash over USB is:

```
build/dfu_application.zip
```

## 3. Flash over USB (no debugger needed)

The Thingy:53 has no built-in debugger. It ships with the **MCUboot** bootloader, which
accepts new firmware over USB.

1. Connect the Thingy:53 to the laptop with a USB-C **data** cable.
2. Put it into bootloader mode: switch the power switch **SW1 off**, then **hold SW2**
   (the large button under the top cover) while switching **SW1 on**. Release SW2.
3. Open **nRF Connect for Desktop → Programmer**.
4. Top-left **Select device** → choose the Thingy:53 (it shows as an MCUboot device).
5. **Add file** → pick `build/dfu_application.zip`.
6. Click **Write**, confirm, and wait for it to finish (about 30–60 s).
7. The board restarts and runs the new firmware.

If it doesn't show up in step 4, check the cable (charge-only cables are common),
then repeat step 2.

## 3b. Flash with a debugger (optional, faster for development)

With an external J-Link, or an nRF5340 DK used as a debugger (DK "Debug out" → Thingy:53
debug connector, 10-pin cable):

```
west flash
```

This writes MCUboot and the app together. Use it if the USB method stops working,
for example after a firmware build that broke USB.

## 4. See the output

The firmware prints to a USB serial port (115200 baud):

- **macOS:** `ls /dev/cu.usbmodem*`, then `screen /dev/cu.usbmodemXXXX 115200`
  (exit with `Ctrl-A`, then `K`)
- **Windows:** check Device Manager for the COM port, then use PuTTY, the VS Code
  serial monitor, or `tools/capture_imu.ps1 -Port COMx`
- **VS Code:** nRF Connect panel → **Connected Devices** → open the serial terminal

Expected output from `gait_node` with only the IMU on:

```
imu init: ok (0)
IMU,1234,0.0748,-0.1287,-9.8064,0.00293,0.00399,0.00186
...
```

Once BLE is on (`CONFIG_GAIT_BLE=y`), data goes over Bluetooth instead of being printed.
Use `edge_receiver/` on the i.MX95, or the **nRF Connect for Mobile** app, to check it.

## 5. Final integration build

When all three parts work in their experiments:

1. In `firmware/gait_node/prj.conf`, set `CONFIG_GAIT_IMU=y`, `CONFIG_GAIT_PLANTAR=y`,
   `CONFIG_GAIT_BLE=y` and uncomment the I2C/ADC and BT lines.
2. Build (section 2) and flash (section 3) `gait_node`.
3. Run `edge_receiver` on the i.MX95.

Only `firmware/gait_node` goes onto the Thingy:53. `edge_receiver/` runs on the i.MX95.
`protocol/` and `hardware/` are documentation, and `tools/` runs on your laptop.
