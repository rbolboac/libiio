/*
 * Copyright (c) 2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>
#include <stddef.h>
#include <no_os_timer.h>
#include <no_os_irq.h>
#include "timers.h"
#include "iio_trigger_timer_hw.h"

/*
 * The trigger tick through no-OS's timer and IRQ API: a TRIGGER_TIMER_ID
 * timer at TRIGGER_TIMER_FREQ_HZ, whose elapsed callback is registered on a
 * TRIGGER_IRQ_OPS controller (timers.h).
 */

struct trigger_timer_hw {
	struct no_os_timer_desc *timer;
	struct no_os_irq_ctrl_desc *irq;
	struct no_os_callback_desc cb;
	void (*tick)(void);
};

static struct trigger_timer_hw hw;

static void trigger_timer_cb(void *ctx)
{
	hw.tick();
}

/*
 * no-OS keeps the callbacks of every peripheral in one list with a single
 * shared iterator, which each timer interrupt walks to find its callback. A
 * register or unregister that such an interrupt splits acts on the node the
 * interrupt left the iterator on: the unregister can free the USB tick's
 * callback, and a timer whose callback is gone never has its flag cleared,
 * so its interrupt fires forever. Change the list with interrupts off.
 */
static int trigger_cb_register(void)
{
	int ret;

	no_os_irq_global_disable(hw.irq);
	ret = no_os_irq_register_callback(hw.irq, TRIGGER_IRQ_ID, &hw.cb);
	no_os_irq_global_enable(hw.irq);

	return ret;
}

static void trigger_cb_unregister(void)
{
	no_os_irq_global_disable(hw.irq);
	no_os_irq_unregister_callback(hw.irq, TRIGGER_IRQ_ID, &hw.cb);
	no_os_irq_global_enable(hw.irq);
}

int iio_trigger_timer_hw_init(void)
{
	struct no_os_irq_init_param irq_ip = {
		.irq_ctrl_id = 0,
		.platform_ops = TRIGGER_IRQ_OPS,
		.extra = NULL,
	};
	int ret;

	if (hw.irq)
		return 0;

	ret = no_os_irq_ctrl_init(&hw.irq, &irq_ip);
	if (ret)
		return ret;

	hw.cb = (struct no_os_callback_desc) {
		.callback = trigger_timer_cb,
		.ctx = NULL,
		.event = NO_OS_EVT_TIM_ELAPSED,
		.peripheral = NO_OS_TIM_IRQ,
		.handle = TRIGGER_IRQ_HANDLE,
	};

	ret = no_os_irq_set_priority(hw.irq, TRIGGER_IRQ_ID,
				     TRIGGER_IRQ_PRIORITY);
	if (ret)
		/* The controller is shared with the other drivers: keep it. */
		hw.irq = NULL;

	return ret;
}

int iio_trigger_timer_hw_start(uint32_t freq_hz, void (*tick)(void))
{
	struct no_os_timer_init_param timer_ip = {
		.id = TRIGGER_TIMER_ID,
		.freq_hz = TRIGGER_TIMER_FREQ_HZ,
		.ticks_count = TRIGGER_TIMER_FREQ_HZ / freq_hz,
		.platform_ops = TRIGGER_TIMER_OPS,
		.extra = TRIGGER_TIMER_EXTRA,
	};
	int ret;

	if (!hw.irq)
		return -ENODEV;

	hw.tick = tick;

	ret = no_os_timer_init(&hw.timer, &timer_ip);
	if (ret)
		return ret;

	/*
	 * The callback is registered only now, as iiod/usb.c does for its tick:
	 * registering enables the timer's interrupt, and no_os_timer_init()
	 * resets the timer, so registering first leaves it without interrupts.
	 */
	ret = trigger_cb_register();
	if (ret)
		goto remove_timer;

	ret = no_os_irq_enable(hw.irq, TRIGGER_IRQ_ID);
	if (ret)
		goto unregister_cb;

	ret = no_os_timer_start(hw.timer);
	if (ret)
		goto disable_irq;

	return 0;

disable_irq:
	no_os_irq_disable(hw.irq, TRIGGER_IRQ_ID);
unregister_cb:
	trigger_cb_unregister();
remove_timer:
	no_os_timer_remove(hw.timer);
	hw.timer = NULL;

	return ret;
}

void iio_trigger_timer_hw_stop(void)
{
	no_os_timer_stop(hw.timer);
	no_os_irq_disable(hw.irq, TRIGGER_IRQ_ID);
	trigger_cb_unregister();
	no_os_timer_remove(hw.timer);

	hw.timer = NULL;
}
