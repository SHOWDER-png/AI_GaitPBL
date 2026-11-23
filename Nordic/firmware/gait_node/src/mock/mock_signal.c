#include "mock/mock_signal.h"

#define STRIDE_MS 1100 /* one gait cycle */

static uint32_t phase(uint32_t t_ms, uint32_t shift) /* 0..999 */
{
	return ((t_ms % STRIDE_MS) * 1000 / STRIDE_MS + shift) % 1000;
}

static int16_t tri(uint32_t ph, int16_t amp) /* triangle -amp..+amp */
{
	int32_t x = ph < 500 ? (int32_t)ph * 4 - 1000 : 3000 - (int32_t)ph * 4;

	return (int16_t)(amp * x / 1000);
}

static uint16_t bump(uint32_t ph, uint32_t a, uint32_t b, uint16_t peak)
{
	if (ph < a || ph >= b) {
		return 0;
	}

	uint32_t mid = (a + b) / 2;
	uint32_t up = ph < mid ? ph - a : b - ph;

	return (uint16_t)((uint32_t)peak * up / (mid - a));
}

void mock_imu(uint32_t t_ms, struct imu_sample *s)
{
	uint32_t ph = phase(t_ms, 0);

	s->timestamp_ms = t_ms;
	s->accel[0] = tri(ph, 1500);
	s->accel[1] = tri((ph + 250) % 1000, 800);
	s->accel[2] = 9810 + tri(ph, 2500); /* gravity + bounce */
	s->gyro[0] = tri((ph + 100) % 1000, 400);
	s->gyro[1] = tri((ph + 125) % 1000, 3000); /* leg swing */
	s->gyro[2] = tri((ph + 300) % 1000, 300);
}

void mock_plantar(uint32_t t_ms, struct plantar_sample *s)
{
	s->timestamp_ms = t_ms;
	for (int side = 0; side < 2; side++) { /* right foot = half a stride later */
		uint32_t ph = phase(t_ms, side ? 500 : 0);

		s->fsr[side * 3 + 0] = bump(ph, 0, 250, 3000);   /* heel */
		s->fsr[side * 3 + 1] = bump(ph, 200, 650, 3500); /* ball */
		s->fsr[side * 3 + 2] = bump(ph, 450, 800, 2500); /* toe */
	}
}
