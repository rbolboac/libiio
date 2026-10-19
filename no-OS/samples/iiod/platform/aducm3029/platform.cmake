# Copyright (c) 2026 Analog Devices, Inc.
# SPDX-License-Identifier: MIT
#
# ADuCM3029 glue for the iiod firmware (EVAL-ADICUP3029). The no-os library
# already carries the DFP drivers, and config_platform_sdk() has CCES generate
# the startup code, pin mux and linker script; this file only adds what the
# libiio port needs on top of it:
#
#   IIOD_PLATFORM_SRCS      board bring-up (noos_platform_init)
#   IIOD_PLATFORM_DEFS      part-specific defines
#   IIOD_PLATFORM_ADC       default ADC HAL under drivers/adc/
#   WITH_IIOD_V0_COMPAT     default OFF here, see below
#
# The stack (8 KiB) and heap (32 KiB, all of DSRAM_B) are no-OS's own for the
# part, set on the no-os target by drivers/platform/aducm3029/.

if(NOT IIOD_TRANSPORT STREQUAL "uart")
  message(FATAL_ERROR
    "${TARGET} supports only the uart transport: it has no USB device core, "
    "and the network build assumes the ADIN1110 wiring of the AD-APARD32690-SL.")
endif()

# The v0 (ASCII) protocol is off by default: its flex scanner allocates a
# 16 KiB input buffer per session, half of the heap, and exits when it cannot.
# -DWITH_IIOD_V0_COMPAT=ON still builds it.
set(WITH_IIOD_V0_COMPAT OFF CACHE BOOL "Serve libiio v0.x clients (ASCII protocol)")

set(IIOD_PLATFORM_SRCS ${CMAKE_CURRENT_LIST_DIR}/platform.c)
set(IIOD_PLATFORM_INCLUDES "")
set(IIOD_PLATFORM_DEFS "")
set(IIOD_PLATFORM_ADC adc_demo)
