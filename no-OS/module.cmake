# Copyright (c) 2026 Analog Devices, Inc.
# SPDX-License-Identifier: MIT
#
# libiio as a no-OS library module. no-OS keeps only the switch
# (CONFIG_LIBIIO) and cmake/libraries/libiio.cmake, which finds this tree and
# includes this file.
#
# Provides the libiio-iiod OBJECT library: the libiio core, tinyiiod, the
# IIOD responder, the no-OS backend and the uart transport. It is linked into
# the project's executable (NO_OS_PROJECT_NAME) and carries the device API:
# <iio/iio.h>, <iio/iio-backend.h> and iio_device.h. A project describes its
# devices with struct noos_iio_device_info, registers them with
# noos_iio_register_device() and runs the server with noos_iiod_run_uart() on
# its own no_os_uart_init_param, so the server needs nothing from the project.
# Devices are written against this API, in the project or as no-OS drivers
# (iio_adxl355.c); no-OS's own IIO (iio/, CONFIG_IIO) is not used.
#
# Included from no-OS's libraries/, after projects/, so the project's
# executable exists.

include(${CMAKE_CURRENT_LIST_DIR}/cmake/libiio_noos_server.cmake)

libiio_noos_add_server(libiio-iiod TRANSPORT uart)

# The device API on the no-os target too, so no-OS drivers written against it
# (drivers/accel/adxl355/iio_adxl355.c) build there.
target_include_directories(no-os PUBLIC
  ${LIBIIO_NOOS_DIR}/include
  ${LIBIIO_DIR}/include
)
target_compile_definitions(no-os PUBLIC LIBIIO_STATIC=1)

# Appended to the property rather than through target_link_libraries(), so
# it fits a project that links no-os with either signature.
if(DEFINED NO_OS_PROJECT_NAME AND TARGET ${NO_OS_PROJECT_NAME})
  set_property(TARGET ${NO_OS_PROJECT_NAME} APPEND PROPERTY LINK_LIBRARIES libiio-iiod)
else()
  message(STATUS "libiio: no project executable to link; link libiio-iiod yourself")
endif()
