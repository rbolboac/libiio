/*
 * Copyright (c) 2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef NOOS_INCLUDE_IIO_ACCEL_H_
#define NOOS_INCLUDE_IIO_ACCEL_H_

/*
 * The standard channels of an accelerometer, as Linux's accelerometers have
 * them, for a table-driven device (iio_device.h): accel_x, accel_y and
 * accel_z (NOOS_IIO_ACCEL, modified by the axis).
 *
 * The part names the standard attributes it has; this list puts each one
 * where every accelerometer has it:
 * - the part's settings, NOOS_IIO_ACCEL_SHARED (scale, sampling_frequency,
 *   the filter corners, oversampling_ratio), are shared by type: one value
 *   for all the axes (in_accel_scale), read and written through any of
 *   them;
 * - every other one (raw, calibbias, calibscale, ...) is the axis' own.
 *
 * A channel's priv is its axis, 0 for x to 2 for z.
 */

#include <iio_device.h>

#define NOOS_IIO_ACCEL_SHARED	(NOOS_IIO_BIT(SCALE) |			\
				 NOOS_IIO_BIT(SAMP_FREQ) |		\
				 NOOS_IIO_BIT(HIGH_PASS_FILTER_3DB_FREQUENCY) | \
				 NOOS_IIO_BIT(LOW_PASS_FILTER_3DB_FREQUENCY) | \
				 NOOS_IIO_BIT(OVERSAMPLING_RATIO))

/*
 * One axis: @_axis is X, Y or Z, @_scan its scan index (-1: none), @_info
 * the NOOS_IIO_BIT()s it has, @_avail those of them with a list, and last
 * its scan_type, as Linux's: { .sign = 's', .realbits = 20,
 * .storagebits = 32, .endianness = NOOS_IIO_LE }, or NOOS_IIO_NO_SCAN_TYPE.
 */
#define NOOS_IIO_ACCEL_CHANNEL(_axis, _scan, _info, _avail, ...) {	\
	.type = NOOS_IIO_ACCEL,						\
	.modified = true,						\
	.channel2 = NOOS_IIO_MOD_##_axis,				\
	.scan_index = (_scan),						\
	.scan_type = __VA_ARGS__,					\
	.info_mask_separate = (_info) & ~NOOS_IIO_ACCEL_SHARED,		\
	.info_mask_separate_available = (_avail) & ~NOOS_IIO_ACCEL_SHARED, \
	.info_mask_shared_by_type = (_info) & NOOS_IIO_ACCEL_SHARED,	\
	.info_mask_shared_by_type_available =				\
		(_avail) & NOOS_IIO_ACCEL_SHARED,			\
	.priv = NOOS_IIO_MOD_##_axis - NOOS_IIO_MOD_X,			\
}

/* The three axes, scan elements 0 to 2. */
#define NOOS_IIO_ACCEL_XYZ(_info, _avail, ...)				\
	NOOS_IIO_ACCEL_CHANNEL(X, 0, _info, _avail, __VA_ARGS__),	\
	NOOS_IIO_ACCEL_CHANNEL(Y, 1, _info, _avail, __VA_ARGS__),	\
	NOOS_IIO_ACCEL_CHANNEL(Z, 2, _info, _avail, __VA_ARGS__)

#endif /* NOOS_INCLUDE_IIO_ACCEL_H_ */
