/*
 * Copyright (c) 2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef SAMPLE_ADC_H
#define SAMPLE_ADC_H

/*
 * The sample's ADC: one driver per image, the one the build picks under
 * samples/iiod/adc/ (its ADC=): adc_demo without hardware, common_api on the
 * MAX32 parts. Each is a table-driven device with the standard ADC channels
 * (iio_adc.h).
 */

#include <iio_device.h>

/**
 * struct sample_adc_param - how the sample paces the ADC's buffer
 * @trigger:   name of the registered trigger that paces it (may be NULL)
 * @wait_scan: wait for the next scan (may be NULL: unpaced)
 * @stop_scan: the buffer was disabled (may be NULL)
 */
struct sample_adc_param {
	const char *trigger;
	int (*wait_scan)(void);
	void (*stop_scan)(void);
};

/* Bring the ADC up and describe it for noos_iio_register_device(). */
int sample_adc_get_device_info(const struct sample_adc_param *param,
			       struct noos_iio_device_info *info);

#endif /* SAMPLE_ADC_H */
