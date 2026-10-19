/*
 * Copyright (c) 2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>
#include "platform_init.h"
#include "iio_device.h"

/*
 * no-OS's platform_init(): power service, HCLK and PCLK at 26 MHz (the UART
 * baud table assumes it) and adi_initComponents(), which applies the pin mux
 * generated from pinmux_config.c.
 */
int noos_platform_init(void)
{
	return platform_init() ? -EIO : 0;
}
