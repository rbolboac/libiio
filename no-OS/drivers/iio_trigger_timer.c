/*
 * Copyright (c) 2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <iio/iio-backend.h>
#include "iio_trigger_timer.h"
#include "iio_trigger_timer_hw.h"

/*
 * Timer trigger, the bare-metal counterpart of the Zephyr trigger_timer: a
 * hardware timer interrupt (iio_trigger_timer_hw.h) counts ticks, and a
 * sampling driver calls iio_trigger_timer_wait() once per scan. With no
 * threads the wait spins, so the transfer blocks for nb_samples /
 * sampling_frequency seconds -- the transfer is synchronous either way.
 */
struct iio_trigger_timer {
	volatile uint32_t ticks;
	uint32_t seen;
	uint32_t freq_hz;
	bool ready;
	bool running;
};

static struct iio_trigger_timer trig = {
	.freq_hz = IIO_TRIGGER_TIMER_DEFAULT_HZ,
};

static void trigger_timer_tick(void)
{
	trig.ticks++;
}

int iio_trigger_timer_init(void)
{
	int ret;

	if (trig.ready)
		return 0;

	ret = iio_trigger_timer_hw_init();
	if (ret)
		return ret;

	trig.ready = true;

	return 0;
}

static int trigger_timer_start(void)
{
	int ret;

	if (!trig.ready)
		return -ENODEV;

	trig.seen = trig.ticks;

	ret = iio_trigger_timer_hw_start(trig.freq_hz, trigger_timer_tick);
	if (ret)
		return ret;

	trig.running = true;

	return 0;
}

void iio_trigger_timer_stop(void)
{
	if (!trig.running)
		return;

	iio_trigger_timer_hw_stop();

	trig.running = false;
}

int iio_trigger_timer_wait(void)
{
	int ret;

	if (!trig.freq_hz)
		return 0;

	if (!trig.running) {
		ret = trigger_timer_start();
		if (ret)
			return ret;
	}

	while (trig.ticks == trig.seen)
		;

	/*
	 * A scan slower than the period missed ticks: resync to the latest
	 * one instead of bursting to catch up, so the spacing stays even.
	 */
	trig.seen = trig.ticks;

	return 0;
}

static int iio_trigger_timer_read_attr(void *dev,
				       const struct iio_device *iio_dev,
				       const struct iio_attr *attr,
				       char *dst, size_t len)
{
	int ret;

	if (attr->type != IIO_ATTR_TYPE_DEVICE ||
	    strcmp(iio_attr_get_name(attr), "sampling_frequency"))
		return -EINVAL;

	ret = snprintf(dst, len, "%u", (unsigned int)trig.freq_hz);
	if (ret < 0 || (size_t)ret >= len)
		return -EINVAL;

	return ret + 1;
}

static int iio_trigger_timer_write_attr(void *dev,
					const struct iio_device *iio_dev,
					const struct iio_attr *attr,
					const char *src, size_t len)
{
	unsigned long val;
	char *end;

	if (attr->type != IIO_ATTR_TYPE_DEVICE ||
	    strcmp(iio_attr_get_name(attr), "sampling_frequency"))
		return -EINVAL;

	if (*src < '0' || *src > '9')
		return -EINVAL;

	val = strtoul(src, &end, 10);
	if (*end == '\n')
		end++;
	if (end == src || *end != '\0' || val > IIO_TRIGGER_TIMER_MAX_HZ)
		return -EINVAL;

	/* The next wait restarts the timer at the new period. */
	iio_trigger_timer_stop();
	trig.freq_hz = (uint32_t)val;

	return (int)len;
}

static int iio_trigger_timer_add_attrs(void *dev, struct iio_device *iio_dev)
{
	return iio_device_add_attr(iio_dev, "sampling_frequency",
				   IIO_ATTR_TYPE_DEVICE);
}

int iio_trigger_timer_get_device_info(struct noos_iio_device_info *info)
{
	if (!info)
		return -EINVAL;

	*info = (struct noos_iio_device_info) {
		.name = IIO_TRIGGER_TIMER_NAME,
		.add_channels = iio_trigger_timer_add_attrs,
		.read_attr = iio_trigger_timer_read_attr,
		.write_attr = iio_trigger_timer_write_attr,
		.is_trigger = true,
	};

	return 0;
}
