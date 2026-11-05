/*
 * Thingy:53 gait node — main loop. Owner: Nordic.
 *
 * Reads the enabled sensor modules on one shared clock and either sends the
 * samples over BLE (CONFIG_GAIT_BLE=y) or prints them to the USB console.
 */
#include <zephyr/kernel.h>
#include <stdio.h>

#include "common/gait_types.h"
#ifdef CONFIG_GAIT_IMU
#include "imu/imu.h"
#endif
#ifdef CONFIG_GAIT_PLANTAR
#include "plantar/plantar.h"
#endif
#ifdef CONFIG_GAIT_BLE
#include "ble/gait_ble.h"
#endif

#ifdef CONFIG_GAIT_IMU
#define TICK_HZ CONFIG_GAIT_IMU_RATE_HZ
#else
#define TICK_HZ 100
#endif

static void report_init(const char *name, int err)
{
	printf("%s init: %s (%d)\n", name, err ? "FAILED" : "ok", err);
}

int main(void)
{
	bool imu_ok = false, plantar_ok = false, ble_ok = false;

#ifdef CONFIG_GAIT_IMU
	imu_ok = (imu_init() == 0);
	report_init("imu", imu_ok ? 0 : -1);
#endif
#ifdef CONFIG_GAIT_PLANTAR
	plantar_ok = (plantar_init() == 0);
	report_init("plantar", plantar_ok ? 0 : -1);
#endif
#ifdef CONFIG_GAIT_BLE
	ble_ok = (gait_ble_init() == 0);
	report_init("ble", ble_ok ? 0 : -1);
#endif
	ARG_UNUSED(imu_ok);
	ARG_UNUSED(plantar_ok);
	ARG_UNUSED(ble_ok);
	ARG_UNUSED(report_init);

	/* Fixed-rate loop: k_sleep(10 ms) in C29 drifted to ~94 Hz, so wait on an absolute deadline. */
	int64_t next = k_uptime_get();
	uint32_t tick = 0;

	while (1) {
#ifdef CONFIG_GAIT_IMU
		if (imu_ok) {
			struct gait_imu_sample imu;

			if (imu_read(&imu) == 0) {
#ifdef CONFIG_GAIT_BLE
				if (ble_ok) {
					gait_ble_send_imu(&imu);
				} else
#endif
				{
					printf("IMU,%u,%.4f,%.4f,%.4f,%.5f,%.5f,%.5f\n", imu.t_ms,
					       (double)imu.acc[0], (double)imu.acc[1], (double)imu.acc[2],
					       (double)imu.gyr[0], (double)imu.gyr[1], (double)imu.gyr[2]);
				}
			}
		}
#endif

#ifdef CONFIG_GAIT_PLANTAR
		if (plantar_ok && (tick % (TICK_HZ / CONFIG_GAIT_PLANTAR_RATE_HZ)) == 0) {
			struct gait_plantar_sample p;

			if (plantar_read(&p) == 0) {
#ifdef CONFIG_GAIT_BLE
				if (ble_ok) {
					gait_ble_send_plantar(&p);
				} else
#endif
				{
					printf("FSR,%u,%d,%d,%d,%d,%d,%d\n", p.t_ms,
					       p.raw[0], p.raw[1], p.raw[2], p.raw[3], p.raw[4], p.raw[5]);
				}
			}
		}
#endif

		tick++;
		next += 1000 / TICK_HZ;
		k_sleep(K_TIMEOUT_ABS_MS(next));
	}
	return 0;
}
