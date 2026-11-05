# ble_hello — first BLE link Thingy:53 → i.MX95

Owner: Nordic. Not started.

Goal: the Thingy:53 advertises as `GaitNode` and notifies a counter at 100 Hz; the
FRDM-IMX95 (or a laptop first) connects, subscribes and logs every value, so dropped
packets and real throughput can be measured before the real protocol is fixed.

Suggested start: NCS `samples/bluetooth/peripheral_lbs` or `peripheral_uart` on the Thingy,
and `bluetoothctl` / a Python `bleak` script on the receiving side.

Results feed into `../../../protocol/BLE_GAIT_PROTOCOL.md`.
