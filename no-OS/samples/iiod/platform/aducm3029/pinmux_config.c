/*
 * Copyright (c) 2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

/*
 * Pin mux of the EVAL-ADICUP3029 for the iiod firmware, in the form the CCES
 * Pin Multiplexing editor writes: no-OS's aducm3029 build hands this file to
 * CCES, which turns it into the project's adi_initpinmux(), run by
 * adi_initComponents() in noos_platform_init().
 *
 * Selected Peripherals
 * --------------------
 * UART0 (Tx, Rx)
 *
 * GPIO (unavailable)
 * ------------------
 * P0_10, P0_11
 */

#include <sys/platform.h>
#include <stdint.h>

#define UART0_TX_PORTP0_MUX  ((uint32_t) ((uint32_t) 1 << 20))
#define UART0_RX_PORTP0_MUX  ((uint32_t) ((uint32_t) 1 << 22))

int32_t adi_initpinmux(void);

/*
 * Initialize the Port Control MUX Registers
 */
int32_t adi_initpinmux(void)
{
	/* PORTx_MUX registers */
	*((volatile uint32_t *)REG_GPIO0_CFG) = UART0_TX_PORTP0_MUX
						| UART0_RX_PORTP0_MUX;

	return 0;
}
