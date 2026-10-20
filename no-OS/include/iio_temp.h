/*
 * Copyright (c) 2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef NOOS_INCLUDE_IIO_TEMP_H_
#define NOOS_INCLUDE_IIO_TEMP_H_

/*
 * The standard channels of a temperature, as Linux's temperature sensors
 * have them, for a table-driven device (iio_device.h): "temp" for a part
 * with one (a temperature sensor, or the one inside an accelerometer or an
 * ADC), "temp0", "temp1" ... for a part with several.
 *
 * Every standard attribute is the channel's own: raw, input, and scale and
 * offset as Linux reads a temperature, (raw + offset) * scale in milli
 * degrees Celsius.
 */

#include <iio_device.h>

/* The part's temperature: @_scan its scan index (-1: none), @_info the
 * NOOS_IIO_BIT()s it has, @_avail those of them with a list, and last its
 * scan_type (an initializer, as Linux's, or NOOS_IIO_NO_SCAN_TYPE). */
#define NOOS_IIO_TEMP_CHANNEL(_scan, _info, _avail, ...) {		\
	.type = NOOS_IIO_TEMP,						\
	.scan_index = (_scan),						\
	.scan_type = __VA_ARGS__,					\
	.info_mask_separate = (_info),					\
	.info_mask_separate_available = (_avail),			\
}

/* Temperature @_n of several; priv is @_n. */
#define NOOS_IIO_TEMP_CHANNEL_N(_n, _scan, _info, _avail, ...) {	\
	.type = NOOS_IIO_TEMP,						\
	.indexed = true,						\
	.channel = (_n),						\
	.scan_index = (_scan),						\
	.scan_type = __VA_ARGS__,					\
	.info_mask_separate = (_info),					\
	.info_mask_separate_available = (_avail),			\
	.priv = (_n),							\
}

#endif /* NOOS_INCLUDE_IIO_TEMP_H_ */
