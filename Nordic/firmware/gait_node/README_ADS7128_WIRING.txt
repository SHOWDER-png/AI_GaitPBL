GAIT_NODE ADS7128 PATCH - WIRING AND APPLY NOTES
=================================================

This patch targets the gait_node source tree from the supplied gait_node.zip.
It replaces the old ADS1115 comments and the old internal-ADC FSR driver with
an ADS7128 I2C implementation.

SELECTED ADDRESS
----------------
Keep the Adafruit breakout at its factory/default I2C address: 0x10 (7-bit).
Do NOT solder the 0x11 or 0x17 address jumper.

BUS AND THINGY:53 PINS
---------------------
The patch configures a dedicated external I2C bus on the Thingy:53 nRF5340:
  Thingy:53 P0.04 / EXP_BOARD_PIN1  -> ADS7128 SDA
  Thingy:53 P0.05 / EXP_BOARD_PIN2  -> ADS7128 SCL

Power:
  Thingy:53 3V3 / VDD_EXP_BOARD     -> ADS7128 VIN
  Thingy:53 GND                    -> ADS7128 GND

ADS7128 channel mapping:
  A0 -> Left Heel
  A1 -> Left Ball
  A2 -> Left Toe
  A3 -> Right Heel
  A4 -> Right Ball
  A5 -> Right Toe
  A6, A7 spare

ALRT, ADDR, and AVDD header pins are not required for this first I2C test.
The breakout's ADDR pin/jumper is left at factory default 0x10.
The breakout board's AVDD is factory-connected to VIN; powering VIN from 3.3V
therefore makes the ADC full-scale/reference 3.3V.

IMPORTANT ANALOG SAFETY
----------------------
Each FSR402 requires a voltage-divider circuit. Do not connect a bare FSR
directly to an ADS7128 analog input. Keep every A0..A5 voltage between GND
and AVDD (0..3.3V with this power setup). Confirm the divider wiring and
reference resistor before connecting the six sensors.

APPLY
-----
Copy the contents of this patch ZIP into the existing project folder so the
gait_node/ paths merge/overwrite the corresponding files. Do not copy any
build/ directory. Then run a pristine build from the gait_node project root:

  west build -p always -b thingy53/nrf5340/cpuapp .

The source has not been built on the user's Windows SDK in this environment.
A successful compile still does not prove I2C communication; that requires
the physical board, wiring, and serial output test.

PROTOCOL NOTE
-------------
The driver uses the ADS7128 manual-conversion mode:
- single-register write opcode 0x08
- CHANNEL_SEL register 0x11
- ADC output is 12-bit, MSB-aligned in two bytes
The driver deliberately does not enable CRC or oversampling in this first
bring-up version.
