/*
 * Copyright (c) 2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef IIOD_PARAMETERS_H
#define IIOD_PARAMETERS_H

/* ---------- UART ---------- */
/*
 * UART0 is the console on the EVAL-ADICUP3029 (P0_10/P0_11, pinmux_config.c),
 * on the USB connector with S2 on USB. The no-OS driver takes no extra, and
 * its baud table assumes the 26 MHz PCLK set by noos_platform_init().
 */
#include "aducm3029_uart.h"

#define UART_DEVICE_ID		0
#define UART_BAUDRATE		115200
#define UART_OPS		&aducm_uart_ops
#define UART_EXTRA		NULL

/* ---------- Timers: sampling trigger ---------- */
#include "timers.h"

#endif /* IIOD_PARAMETERS_H */
