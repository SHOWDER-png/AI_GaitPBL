
#include <errno.h>
#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/devicetree.h>
#include <zephyr/sys/printk.h>
#include <errno.h>
#include "fsr402.h"

/* Get ADC configuration from app.overlay */
#define FSR_NODE DT_PATH(zephyr_user)

static const struct adc_dt_spec fsr_adc =
    ADC_DT_SPEC_GET(FSR_NODE);

/* Initialize ADC */
int fsr402_init(void)
{
    if (!adc_is_ready_dt(&fsr_adc)) {
        printk("FSR ADC device not ready\n");
        return -ENODEV;
    }

    int err = adc_channel_setup_dt(&fsr_adc);

    if (err < 0) {
        printk("ADC setup failed: %d\n", err);
        return err;
    }

    printk("FSR402 initialized\n");

    return 0;
}

/* Read raw ADC value */
int fsr402_read(int16_t *raw_value)
{
    struct adc_sequence sequence = {
        .buffer = raw_value,
        .buffer_size = sizeof(*raw_value),
    };

    int err = adc_sequence_init_dt(&fsr_adc, &sequence);

    if (err < 0) {
        return err;
    }

    return adc_read_dt(&fsr_adc, &sequence);
}