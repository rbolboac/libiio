# Copyright (c) 2026 Analog Devices, Inc.
# SPDX-License-Identifier: MIT
#
# The iiod server half of the port, as an OBJECT library that any CMake build
# with a `no-os` target can link, such as this directory's own firmware
# build.
#
#   libiio_noos_add_server(<name>
#     TRANSPORT uart|usb|network
#     [V0_COMPAT]          serve libiio v0.x clients; the caller adds the
#                          generated parser/lexer and ascii-ops.c to SOURCES
#     [SOURCES ...]        more files built with the server's settings
#     [INCLUDES ...]       dirs holding the platform's parameters.h / timers.h;
#                          with uart, giving them also builds noos_iiod_run()
#                          (iiod/uart_params.c), else only noos_iiod_run_uart()
#     [DEFINITIONS ...])   extra defines, also seen by whoever links <name>
#
# Link <name> into the executable. It brings the objects, the libiio defines
# (LIBIIO_STATIC, NO_THREADS, ...) and the API include dirs (<iio/iio.h>,
# iio_device.h) that the device classes registered with it need. The rest of
# libiio's headers stay private: the repo root has attr.h, sort.h and
# network.h, which must not shadow a project's own.

set(LIBIIO_NOOS_DIR ${CMAKE_CURRENT_LIST_DIR}/..)
get_filename_component(LIBIIO_NOOS_DIR "${LIBIIO_NOOS_DIR}" REALPATH)
get_filename_component(LIBIIO_DIR "${LIBIIO_NOOS_DIR}/.." REALPATH)

set(LIBIIO_NOOS_CORE_SRCS
  ${LIBIIO_DIR}/context.c
  ${LIBIIO_DIR}/library.c
  ${LIBIIO_DIR}/backend.c
  ${LIBIIO_DIR}/device.c
  ${LIBIIO_DIR}/channel.c
  ${LIBIIO_DIR}/attr.c
  ${LIBIIO_DIR}/utilities.c
  ${LIBIIO_DIR}/lock-dummy.c
  ${LIBIIO_DIR}/scan.c
  ${LIBIIO_DIR}/buffer.c
  ${LIBIIO_DIR}/stream.c
  ${LIBIIO_DIR}/task.c
  ${LIBIIO_DIR}/mask.c
  ${LIBIIO_DIR}/sort.c
  ${LIBIIO_DIR}/block.c
  ${LIBIIO_DIR}/events.c
  ${LIBIIO_DIR}/tinyiiod/tinyiiod.c
  ${LIBIIO_DIR}/iiod/responder.c
  ${LIBIIO_DIR}/iiod-responder.c
  ${LIBIIO_DIR}/iiod/rw.c
  ${LIBIIO_NOOS_DIR}/backend.c
)

function(libiio_noos_add_server name)
  cmake_parse_arguments(PARSE_ARGV 1 arg "V0_COMPAT" "TRANSPORT"
                        "SOURCES;INCLUDES;DEFINITIONS")

  if(NOT EXISTS ${LIBIIO_NOOS_DIR}/iiod/${arg_TRANSPORT}.c)
    message(FATAL_ERROR
      "libiio_noos_add_server: unknown TRANSPORT '${arg_TRANSPORT}' (uart, usb or network).")
  endif()

  if(arg_V0_COMPAT)
    set(_v0 1)
  else()
    set(_v0 0)
    # Only the stub of what ascii-ops.c would provide.
    list(APPEND arg_SOURCES ${LIBIIO_DIR}/tinyiiod/ops-stubs.c)
  endif()

  # noos_iiod_run() reads the UART from parameters.h; without the dir that
  # has it, callers pass theirs to noos_iiod_run_uart().
  if(arg_TRANSPORT STREQUAL "uart" AND arg_INCLUDES)
    list(APPEND arg_SOURCES ${LIBIIO_NOOS_DIR}/iiod/uart_params.c)
  endif()

  add_library(${name} OBJECT
    ${LIBIIO_NOOS_CORE_SRCS}
    ${LIBIIO_NOOS_DIR}/iiod/${arg_TRANSPORT}.c
    ${arg_SOURCES}
  )

  # The private dirs come first, so no-OS's own iio/ headers (iio.h, iiod.h)
  # never stand in for libiio's.
  target_include_directories(${name}
    PUBLIC
      ${LIBIIO_NOOS_DIR}/include
      ${LIBIIO_DIR}/include
    PRIVATE
      ${LIBIIO_NOOS_DIR}/iiod
      ${LIBIIO_DIR}
      ${LIBIIO_DIR}/iiod
      ${arg_INCLUDES}
  )

  target_compile_definitions(${name} PUBLIC
    LIBIIO_STATIC=1
    NO_THREADS=1
    WITH_ZSTD=0
    WITH_LIBTINYIIOD=1
    WITH_EXTERNAL_BACKEND=1
    WITH_IIOD_USB_DMABUF=0
    WITH_IIOD_V0_COMPAT=${_v0}
    WITH_LOCAL_BACKEND=0
    WITH_NETWORK_BACKEND=0
    WITH_SERIAL_BACKEND=0
    WITH_USB_BACKEND=0
    WITH_XML_BACKEND=0
    WITH_NETWORK_BACKEND_DYNAMIC=0
    WITH_SERIAL_BACKEND_DYNAMIC=0
    WITH_USB_BACKEND_DYNAMIC=0
    HAVE_DNS_SD=0
    WITH_EMU_BACKEND=0
    WITH_EMU_BACKEND_DYNAMIC=0
    WITH_MODULES=0
    DEFAULT_LOG_LEVEL=0
    LIBIIO_VERSION_MAJOR=1
    LIBIIO_VERSION_MINOR=0
    LIBIIO_VERSION_PATCH=0
    LIBIIO_VERSION_GIT="v1.0"
    HAS_STRTOK_R=1
    LIBIIO_SCAN_BACKENDS=""
    DISABLE_SECURE_SOCKET=1
    ${arg_DEFINITIONS}
  )

  # no-os's headers, defines and the forced no_os_config.h, but not its
  # objects: those reach the link through the executable once.
  target_link_libraries(${name} PRIVATE $<COMPILE_ONLY:no-os>)
endfunction()
