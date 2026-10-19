/*
 * Copyright (c) 2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

/*
 * Which hardware timer each part of the port owns. no-OS's aducm3029 driver
 * keeps GP0 (timer id 0) for no_os_mdelay/udelay and maps every other timer
 * id to GP1, which the sampling trigger takes.
 */

#ifndef IIOD_TIMERS_H
#define IIOD_TIMERS_H

#include <stdint.h>
#include "aducm3029_timer.h"
#include "aducm3029_irq.h"

/* ---------- Sampling trigger (timer0 IIO trigger) ---------- */
#define TRIGGER_TIMER_ID	1
/*
 * PCLK (26 MHz, noos_platform_init) / 64. The period, TRIGGER_TIMER_FREQ_HZ /
 * sampling_frequency, must fit the 16-bit counter: about 6.2 Hz and up.
 */
#define TRIGGER_TIMER_FREQ_HZ	406250
#define TRIGGER_TIMER_MAX_TICKS	UINT16_MAX
#define TRIGGER_TIMER_OPS	&aducm_timer_ops
#define TRIGGER_TIMER_EXTRA	(&(struct aducm_timer_init_param) { \
					.source_freq = PCLK_DIV64, \
				})
#define TRIGGER_IRQ_OPS		&aducm_irq_ops
/* register/enable take the no-OS irq id, set_priority the NVIC line. */
#define TRIGGER_IRQ_ID		ADUCM_TIMER1_INT_ID
#define TRIGGER_IRQ_PRIORITY_ID	TMR1_EVT_IRQn
/* The aducm3029 timer callback's handle is its timer descriptor. */
#define TRIGGER_IRQ_HANDLE_TIMER
#define TRIGGER_IRQ_PRIORITY	3

#endif /* IIOD_TIMERS_H */
