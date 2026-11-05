# edge_receiver — i.MX95 side of the BLE link

Owner: Nordic. Not started.

Runs on the FRDM-IMX95 (Linux). Connects to the Thingy:53 `GaitNode`, subscribes to the
IMU and plantar characteristics, decodes packets per `../protocol/BLE_GAIT_PROTOCOL.md`,
and logs them with the i.MX95 receive time so they can be aligned with the camera stream
in `../../Camera/`.

Output CSV columns (proposal):
- IMU: `rx_time_utc, seq, t_ms, ax_mps2, ay_mps2, az_mps2, gx_rads, gy_rads, gz_rads`
  (same units as `firmware/experiments/imu_bmi270_c29`)
- Plantar: `rx_time_utc, seq, t_ms, l_heel, l_ball, l_toe, r_heel, r_ball, r_toe`
