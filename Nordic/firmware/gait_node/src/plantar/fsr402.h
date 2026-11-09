#ifndef FSR402_H
#define FSR402_H

#include <stdint.h>

int fsr402_init(void);
int fsr402_read(int16_t *raw_value);

#endif