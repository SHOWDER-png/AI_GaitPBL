# plantar_ads1115 — first FSR read on the Thingy:53

Owner: plantar. Not started.

Goal: one FSR 402 → voltage divider → ADS1115 → I2C → Thingy:53, raw counts printed on
the USB console at 50 Hz. Then all six across two ADS1115 chips.

Suggested start: the Zephyr ADC sample (`zephyr/samples/drivers/adc/adc_dt`) with an
overlay that adds a `ti,ads1115` node on the I2C bus you wire to.

Record wiring in `../../../hardware/PLANTAR_WIRING.md` and data in `data/`.
