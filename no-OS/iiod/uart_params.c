/*
 * Copyright (c) 2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include <no_os_uart.h>
#include "parameters.h"
#include "iio_device.h"

/*
 * noos_iiod_run() for a build whose platform glue names its console UART in
 * parameters.h (UART_DEVICE_ID/BAUDRATE/OPS/EXTRA), as samples/iiod/platform/<platform>/
 * here does. A no-OS project that already has a no_os_uart_init_param calls
 * noos_iiod_run_uart() with it instead, and needs no parameters.h.
 */
int noos_iiod_run(void)
{
	struct no_os_uart_init_param uart_ip = {
		.device_id = UART_DEVICE_ID,
		.baud_rate = UART_BAUDRATE,
		.size = NO_OS_UART_CS_8,
		.parity = NO_OS_UART_PAR_NO,
		.stop = NO_OS_UART_STOP_1_BIT,
		.asynchronous_rx = true,
		.platform_ops = UART_OPS,
		.extra = UART_EXTRA,
	};

	return noos_iiod_run_uart(&uart_ip);
}
