/*
 * Copyright (c) 2025 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

/*
 * A 16-bit, two-input ADC that needs no hardware, as the standard ADC
 * channels (iio_adc.h): voltage0 plays a sine, voltage1 the same sine a
 * quarter period on (a cosine), from a 32-entry table, over a 2.5 V full
 * scale. Each scan moves the waveform one step; reading raw does not, so it
 * gives the sample a scan would.
 */

#include <errno.h>
#include <stdint.h>

#include <no_os_util.h>

#include <iio_adc.h>

#include "sample_adc.h"

#define ADC_DEMO_REF_MV		2500
#define ADC_DEMO_BITS		16
#define ADC_DEMO_STEPS		32

static const uint16_t sine_lut[ADC_DEMO_STEPS] = {
	0x8000, 0x98f8, 0xb0fb, 0xc71c, 0xda82, 0xea6d, 0xf641, 0xfd89,
	0xffff, 0xfd89, 0xf641, 0xea6d, 0xda82, 0xc71c, 0xb0fb, 0x98f8,
	0x8000, 0x6707, 0x4f04, 0x38e3, 0x257d, 0x1592, 0x09be, 0x0276,
	0x0000, 0x0276, 0x09be, 0x1592, 0x257d, 0x38e3, 0x4f04, 0x6707,
};

#define ADC_DEMO_SCAN_TYPE {						\
	.sign = 'u',							\
	.realbits = ADC_DEMO_BITS,					\
	.storagebits = 16,						\
	.endianness = NOOS_IIO_BE,					\
}

static const struct noos_iio_chan adc_demo_channels[] = {
	NOOS_IIO_VOLTAGE_CHANNEL(0, 0, NOOS_IIO_BIT(RAW) | NOOS_IIO_BIT(SCALE),
				 0, ADC_DEMO_SCAN_TYPE),
	NOOS_IIO_VOLTAGE_CHANNEL(1, 1, NOOS_IIO_BIT(RAW) | NOOS_IIO_BIT(SCALE),
				 0, ADC_DEMO_SCAN_TYPE),
};

static const struct noos_iio_value adc_demo_values[] = {
	/* One LSB in mV: 2500 / 2^16. */
	NOOS_IIO_CONST(VOLTAGE, SCALE, FRACTIONAL_LOG2, ADC_DEMO_REF_MV,
		       ADC_DEMO_BITS),
};

struct adc_demo {
	struct sample_adc_param param;
	unsigned int step;
};

static struct adc_demo adc_demo;

/* Input @ch's sample at the current step. */
static int adc_demo_sample(const struct adc_demo *demo, unsigned int ch)
{
	return sine_lut[(demo->step + ch * ADC_DEMO_STEPS / 4) %
			ADC_DEMO_STEPS];
}

static int adc_demo_read_raw(void *dev, const struct noos_iio_chan *ch,
			     enum noos_iio_info info, int *val, int *val2)
{
	if (info != NOOS_IIO_INFO_RAW)
		return -EINVAL;

	*val = adc_demo_sample(dev, (unsigned int)ch->priv);

	return NOOS_IIO_VAL_INT;
}

static int adc_demo_read_scan(void *dev, uint32_t enabled, int64_t *vals)
{
	struct adc_demo *demo = dev;
	unsigned int i;

	for (i = 0; i < NO_OS_ARRAY_SIZE(adc_demo_channels); i++)
		if (enabled & (UINT32_C(1) << i))
			vals[i] = adc_demo_sample(demo, i);

	demo->step = (demo->step + 1) % ADC_DEMO_STEPS;

	return 0;
}

static int adc_demo_wait_scan(void *dev)
{
	struct adc_demo *demo = dev;

	return demo->param.wait_scan();
}

static int adc_demo_enable_buffer(void *dev, bool enable)
{
	struct adc_demo *demo = dev;

	/* The first wait of the next stream starts the trigger again. */
	if (!enable && demo->param.stop_scan)
		demo->param.stop_scan();

	return 0;
}

int sample_adc_get_device_info(const struct sample_adc_param *param,
			       struct noos_iio_device_info *info)
{
	if (!param || !info)
		return -EINVAL;

	adc_demo = (struct adc_demo) { .param = *param };

	*info = (struct noos_iio_device_info) {
		.name = "adc-demo",
		.dev = &adc_demo,
		.channels = adc_demo_channels,
		.num_channels = NO_OS_ARRAY_SIZE(adc_demo_channels),
		.values = adc_demo_values,
		.nb_values = NO_OS_ARRAY_SIZE(adc_demo_values),
		.read_raw = adc_demo_read_raw,
		.read_scan = adc_demo_read_scan,
		.wait_scan = param->wait_scan ? adc_demo_wait_scan : NULL,
		.enable_buffer = adc_demo_enable_buffer,
		.trigger = param->trigger,
	};

	return 0;
}
