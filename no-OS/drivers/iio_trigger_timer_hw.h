/*
 * Copyright (c) 2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef NOOS_DRIVERS_IIO_TRIGGER_TIMER_HW_H_
#define NOOS_DRIVERS_IIO_TRIGGER_TIMER_HW_H_

#include <stdint.h>

/*
 * The hardware tick behind drivers/iio_trigger_timer.c, implemented by
 * drivers/iio_trigger_timer_no_os.c through no-OS's timer and IRQ API with
 * the TRIGGER_* wiring of the platform's timers.h.
 */

/**
 * @brief One-time setup, before any start.
 * @return 0 in case of success, negative error code otherwise.
 */
int iio_trigger_timer_hw_init(void);

/**
 * @brief Call tick() from interrupt context freq_hz times a second.
 * @param freq_hz - Tick rate, 1 .. IIO_TRIGGER_TIMER_MAX_HZ.
 * @param tick    - Called on every period.
 * @return 0 in case of success, negative error code otherwise.
 */
int iio_trigger_timer_hw_start(uint32_t freq_hz, void (*tick)(void));

/**
 * @brief Stop the ticks started by iio_trigger_timer_hw_start().
 */
void iio_trigger_timer_hw_stop(void);

#endif /* NOOS_DRIVERS_IIO_TRIGGER_TIMER_HW_H_ */
