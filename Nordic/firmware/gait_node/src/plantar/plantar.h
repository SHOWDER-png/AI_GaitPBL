/* Plantar pressure module (FSR 402 ×6 via ADS1115). Owner: plantar. */
#ifndef GAIT_PLANTAR_H_
#define GAIT_PLANTAR_H_

#include "common/gait_types.h"

/* Set up the ADS1115 channels. Returns 0 or a negative errno. */
int plantar_init(void);

/* Read all six FSRs into *out (order: enum gait_fsr_pos). Returns 0 or a negative errno. */
int plantar_read(struct gait_plantar_sample *out);

#endif /* GAIT_PLANTAR_H_ */
