/*
 * Copyright (c) 2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef NOOS_INCLUDE_IIO_ADC_H_
#define NOOS_INCLUDE_IIO_ADC_H_

/*
 * The standard channels of an ADC, as Linux's ADCs have them, for a
 * table-driven device (iio_device.h): "voltage0", "voltage1" ..., and
 * "voltage0-voltage1" for a differential input (NOOS_IIO_VOLTAGE).
 *
 * The part names the standard attributes it has; this list puts each one
 * where every ADC has it:
 * - the conversion's settings, NOOS_IIO_ADC_SHARED (sampling_frequency,
 *   oversampling_ratio), are the device's: the device attributes
 *   "sampling_frequency" and "oversampling_ratio";
 * - every other one (raw, scale, offset, calibbias, ...) is the input's
 *   own, a scale included, since a gain is often per input.
 *
 * A channel's priv is its number (the positive input's, for a differential
 * one).
 */

#include <iio_device.h>

#define NOOS_IIO_ADC_SHARED	(NOOS_IIO_BIT(SAMP_FREQ) |		\
				 NOOS_IIO_BIT(OVERSAMPLING_RATIO))

#define NOOS_IIO_ADC_MASKS(_info, _avail)				\
	.info_mask_separate = (_info) & ~NOOS_IIO_ADC_SHARED,		\
	.info_mask_separate_available = (_avail) & ~NOOS_IIO_ADC_SHARED, \
	.info_mask_shared_by_all = (_info) & NOOS_IIO_ADC_SHARED,	\
	.info_mask_shared_by_all_available = (_avail) & NOOS_IIO_ADC_SHARED

/*
 * Input @_n: @_scan its scan index (-1: none), @_info the NOOS_IIO_BIT()s
 * it has, @_avail those of them with a list, and last its scan_type, as
 * Linux's: { .sign = 'u', .realbits = 12, .storagebits = 16,
 * .endianness = NOOS_IIO_BE }, or NOOS_IIO_NO_SCAN_TYPE.
 */
#define NOOS_IIO_VOLTAGE_CHANNEL(_n, _scan, _info, _avail, ...) {	\
	.type = NOOS_IIO_VOLTAGE,					\
	.indexed = true,						\
	.channel = (_n),						\
	.scan_index = (_scan),						\
	.scan_type = __VA_ARGS__,					\
	NOOS_IIO_ADC_MASKS(_info, _avail),				\
	.priv = (_n),							\
}

/* Input @_p minus input @_m. */
#define NOOS_IIO_VOLTAGE_DIFF_CHANNEL(_p, _m, _scan, _info, _avail, ...) { \
	.type = NOOS_IIO_VOLTAGE,					\
	.indexed = true,						\
	.differential = true,						\
	.channel = (_p),						\
	.channel2 = (_m),						\
	.scan_index = (_scan),						\
	.scan_type = __VA_ARGS__,					\
	NOOS_IIO_ADC_MASKS(_info, _avail),				\
	.priv = (_p),							\
}

#endif /* NOOS_INCLUDE_IIO_ADC_H_ */
