# Host tools

Scripts that run on a laptop, not on the Thingy:53.

| Script | Owner | What it does |
|---|---|---|
| `capture_imu.ps1` | IMU (Kaan) | Windows PowerShell. Reads the C29 serial output (`AX: …; GX: …;`) from a COM port and writes a CSV with columns `t_ms,ax_mps2,ay_mps2,az_mps2,gx_rads,gy_rads,gz_rads`. Usage: `.\capture_imu.ps1 -Label stationary -Seconds 10 -Port COM5` |

Note: `gait_node` prints a different format (`IMU,t_ms,ax,…` / `FSR,t_ms,…`, one CSV line
per sample), so a capture script for it only needs to split on the first field.
