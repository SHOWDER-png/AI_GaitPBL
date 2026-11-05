# Plantar sensor wiring — FSR 402 ×6 → ADS1115 → Thingy:53

Owner: plantar. Fill in as the hardware is built; record only what was actually tested.

## Sensor positions

| Index (`enum gait_fsr_pos`) | Foot | Position | ADS1115 chip | Channel |
|---:|---|---|---|---|
| 0 | Left | Heel | A (0x48) | AIN0 |
| 1 | Left | Ball | A (0x48) | AIN1 |
| 2 | Left | Toe | A (0x48) | AIN2 |
| 3 | Right | Heel | B (0x49) | AIN0 |
| 4 | Right | Ball | B (0x49) | AIN1 |
| 5 | Right | Toe | B (0x49) | AIN2 |

One ADS1115 has only 4 single-ended inputs, so six FSRs need two chips.
Set the second chip's ADDR pin so it answers at a different address (ADDR→GND = 0x48,
ADDR→VDD = 0x49).

## Circuit per FSR

- Voltage divider: FSR 402 in series with a fixed resistor R_M = _TBD_ Ω, output to ADS1115 AINx.
- Supply voltage: _TBD_ V (must not exceed the ADS1115 VDD).
- ADS1115 gain (PGA) setting: _TBD_.

## Connection to the Thingy:53

- I2C bus / connector used: _TBD_
- SDA pin: _TBD_   SCL pin: _TBD_   VDD: _TBD_   GND: _TBD_
- Pull-ups: _TBD_

Put the same bus into `firmware/gait_node/app.overlay`.

## Calibration

- Raw counts → force (N): method _TBD_, data under `firmware/experiments/plantar_ads1115/data/`.
