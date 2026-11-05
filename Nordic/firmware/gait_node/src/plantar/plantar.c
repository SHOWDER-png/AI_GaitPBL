/*
 * Plantar pressure module. Owner: plantar.
 *
 * Placeholder: returns -ENOTSUP until the ADS1115 read is proven in
 * experiments/plantar_ads1115. Then implement here with the Zephyr ADC API
 * (adc_channel_setup_dt / adc_read_dt) against the ads1115 nodes in app.overlay.
 */
#include <zephyr/kernel.h>
#include <errno.h>

#include "plantar/plantar.h"

int plantar_init(void)
{
	/* TODO(plantar): set up 6 ADC channels across the two ADS1115 chips */
	return -ENOTSUP;
}

int plantar_read(struct gait_plantar_sample *out)
{
	out->t_ms = k_uptime_get_32();
	for (int i = 0; i < GAIT_FSR_COUNT; i++) {
		out->raw[i] = 0;
	}
	/* TODO(plantar): read each channel into out->raw[GAIT_FSR_*] */
	return -ENOTSUP;
}
