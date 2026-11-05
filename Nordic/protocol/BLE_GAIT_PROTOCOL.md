# BLE gait protocol (Thingy:53 → i.MX95) — DRAFT

Owner: Nordic. Status: **draft, not implemented**. The firmware (`firmware/gait_node/src/ble/`)
and the receiver (`edge_receiver/`) must both match this file. Change it by PR and tag
the IMU and plantar owners.

## Roles

- Thingy:53 = BLE **peripheral**, advertises as `GaitNode`.
- i.MX95 (FRDM-IMX95, onboard IW612 Wi-Fi 6 + BT module) = BLE **central**, subscribes to notifications.

## Service and characteristics

| Item | UUID | Properties | Content |
|---|---|---|---|
| Gait service | _TBD (generate a random 128-bit UUID)_ | — | — |
| IMU data | _TBD_ | Notify | IMU packet below |
| Plantar data | _TBD_ | Notify | Plantar packet below |
| Control | _TBD_ | Write | start/stop streaming, set rates (later) |

## Packets (little-endian)

Source structs: `firmware/gait_node/src/common/gait_types.h`.
Packets carry scaled integers, not floats, to keep notifications small.

### IMU packet — proposal

| Offset | Type | Field | Unit |
|---:|---|---|---|
| 0 | uint16 | sequence number (wraps) | — |
| 2 | uint32 | t_ms, Thingy uptime | ms |
| 6 | int16 ×3 | acc x, y, z | 0.001 m/s² (±32.7 m/s² ≈ ±3.3 g) |
| 12 | int16 ×3 | gyr x, y, z | 0.001 rad/s (±32.7 rad/s) |

18 bytes per sample. Batch several samples per notification if 100 Hz single
notifications drop packets (decide after testing in `experiments/ble_hello`).

### Plantar packet — proposal

| Offset | Type | Field | Unit |
|---:|---|---|---|
| 0 | uint16 | sequence number (wraps) | — |
| 2 | uint32 | t_ms, Thingy uptime | ms |
| 6 | int16 ×6 | raw ADC: L heel, L ball, L toe, R heel, R ball, R toe | ADS1115 counts |

18 bytes per sample.

## Open questions

- Time sync with the camera on the i.MX95: Thingy uptime ≠ i.MX95 clock. Proposal: the
  receiver records its own receive time per packet and estimates the offset.
- Lost packets: sequence numbers let the receiver detect gaps; no retransmit for now.
