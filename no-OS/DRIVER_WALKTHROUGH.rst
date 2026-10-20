ADXL355 on libiio's IIOD server
===============================

How a no-OS driver becomes an IIO device that libiio v1 clients
(``iio_info``, pyadi-iio, IIO Oscilloscope) see over the board's UART: what
turns libiio on in the build, what the driver writes, and what libiio does
for it. The example is the ADXL355 (``drivers/accel/adxl355/`` in no-OS) on
the EVAL-ADICUP3029.

.. contents::
   :local:
   :depth: 2

At a glance
-----------

The driver writes:

- a channel table, from the standard channel macros;
- a value table: the constants and the settings picked from tables;
- ``read_raw``/``write_raw`` for what needs the hardware;
- ``read_scan`` and ``wait_scan`` for the buffer.

libiio does the rest:

- names the channels and attributes as Linux does;
- formats and parses every value, lists the ``_available`` ones;
- checks writes against the tables;
- packs the buffer and serves the protocol over the UART.

1. The pieces
-------------

A host request travels down this stack. The IIO driver is the only part
written for the ADXL355; everything above it is shared by every device on
the server.

.. list-table::
   :header-rows: 1
   :widths: 18 52 30

   * - Layer
     - What it does
     - Where
   * - Host tools
     - ``iio_info``, ``iio_attr``, ``iio_rwdev``, pyadi-iio over libiio v1
       (or v0)
     - the PC
   * - Transport
     - bytes over the console UART, the ``BINARY`` handshake, the server loop
     - libiio ``no-OS/iiod/uart.c``
   * - IIOD protocol
     - tinyiiod and the v1 responder, the same code as Linux's iiod
     - libiio ``iiod/responder.c``
   * - no-OS backend
     - libiio's backend ops (context, buffers, blocks, registers) and the
       device registry
     - libiio ``no-OS/backend.c``
   * - Table layer
     - a channel table becomes channels, attributes, values and scans
     - libiio ``no-OS/backend_table.c``
   * - Standard channels
     - what an accelerometer or a temperature looks like
     - libiio ``no-OS/include/iio_accel.h``, ``iio_temp.h``
   * - IIO driver
     - the ADXL355's channel table, values and ``read_raw``
     - no-OS ``drivers/accel/adxl355/iio_adxl355.c``
   * - Part driver
     - registers, ODR, offsets, readings; no IIO in it
     - no-OS ``drivers/accel/adxl355/adxl355.c``
   * - Platform
     - SPI, UART, IRQ and timers of the ADuCM3029
     - no-OS ``drivers/platform/aducm3029/``

no-OS's own IIO server (``iio/``, ``CONFIG_IIO``) is not used. The IIO driver
is written against libiio's device API (``iio_device.h``), not no-OS's
``iio_types.h``. The two define types with the same names, so a file
includes one or the other, never both.

2. The build
------------

libiio is a no-OS library, like lwIP or FreeRTOS. The project's config
switches it on, no-OS finds the libiio tree, and libiio's own
``no-OS/module.cmake`` builds the server and links it into the firmware.

The project config
~~~~~~~~~~~~~~~~~~

``projects/eval-adxl355-pmdz/iio.conf``, the defconfig fragment of the IIO
example:

.. code-block:: none

   CONFIG_UART=y
   CONFIG_LIBIIO=y
   CONFIG_IRQ=y
   CONFIG_DMA=y
   CONFIG_SPI=y
   CONFIG_I2C=y
   CONFIG_GPIO=y
   CONFIG_ACCEL=y
   CONFIG_ACCEL_ADXL355=y
   CONFIG_ACCEL_IIO_ADXL355=y
   CONFIG_ADXL355_PMDZ_IIO_EXAMPLE=y

- ``CONFIG_LIBIIO=y`` builds libiio's IIOD server. ``CONFIG_LIBIIO_VERSION``
  names the libiio ref to clone, ``main`` by default.
- ``CONFIG_ACCEL_IIO_ADXL355=y`` builds the ADXL355's IIO driver and selects
  the part driver.
- ``CONFIG_ADXL355_PMDZ_IIO_EXAMPLE=y`` picks the example that registers the
  device and runs the server.

The Kconfig dependencies keep the two IIO stacks apart:

.. code-block:: kconfig

   # drivers/accel/Kconfig
   config ACCEL_IIO_ADXL355
       depends on SPI && LIBIIO
       select ACCEL_ADXL355
       bool "Enable the ADXL355 IIO driver"

   config ACCEL_IIO_ADXL355_CALLBACKS
       depends on ACCEL_IIO_ADXL355
       bool "Describe the ADXL355 through raw callbacks"

   # projects/eval-adxl355-pmdz/Kconfig
   config ADXL355_PMDZ_IIO_EXAMPLE
       bool "IIO example"
       depends on LIBIIO && !IIO

How no-OS finds libiio
~~~~~~~~~~~~~~~~~~~~~~

.. code-block:: cmake

   # libraries/CMakeLists.txt
   if(${CONFIG_LIBIIO})
           include(libiio)
   endif()

   # cmake/libraries/libiio.cmake
   resolve_library_source(
       libiio
       "${CONFIG_LIBIIO_VERSION}"          # default "main"
       "${NO_OS_DIR}/libraries/libiio"     # where a managed clone goes
       "https://github.com/analogdevicesinc/libiio"
       LIBIIO_SOURCE_DIR
       LIBIIO_BINARY_DIR
   )
   include(${LIBIIO_SOURCE_DIR}/no-OS/module.cmake)

``resolve_library_source()`` takes the first of:

#. ``NO_OS_DEP_LIBIIO_PATH``, as a cache variable or an environment
   variable: an existing libiio tree, used as it is. This is how you build
   against a working copy.
#. A managed clone of ``CONFIG_LIBIIO_VERSION``: in
   ``$NO_OS_CACHE_DIR/libiio/<version>`` when that is set, else in
   ``libraries/libiio``. A clone already on that commit is reused.

The tree it settles on must have ``no-OS/module.cmake``; configure stops
with a message if it does not.

.. note::

   Set ``NO_OS_DEP_LIBIIO_PATH`` for every build, not only the first
   configure. When a libiio CMake file changes, the next build re-runs
   configure; without the variable in the environment it falls back to
   ``libraries/libiio`` and stops.

What libiio's module builds
~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. code-block:: cmake

   # libiio: no-OS/module.cmake
   include(${CMAKE_CURRENT_LIST_DIR}/cmake/libiio_noos_server.cmake)

   libiio_noos_add_server(libiio-iiod TRANSPORT uart)

   # The device API on the no-os target too, so no-OS drivers written
   # against it (drivers/accel/adxl355/iio_adxl355.c) build there.
   target_include_directories(no-os PUBLIC
     ${LIBIIO_NOOS_DIR}/include      # iio_device.h, iio_accel.h, iio_temp.h ...
     ${LIBIIO_DIR}/include           # <iio/iio.h>, <iio/iio-backend.h>
   )
   target_compile_definitions(no-os PUBLIC LIBIIO_STATIC=1)

   set_property(TARGET ${NO_OS_PROJECT_NAME} APPEND PROPERTY
                LINK_LIBRARIES libiio-iiod)

``libiio_noos_add_server()`` (``no-OS/cmake/libiio_noos_server.cmake``)
creates ``libiio-iiod``, an OBJECT library holding:

- the libiio core (``context.c``, ``device.c``, ``channel.c``, ``attr.c``,
  ``buffer.c``, ``stream.c`` ...), tinyiiod and the IIOD responder;
- the no-OS backend: ``backend.c``, ``backend_table.c`` and ``iio_enum.c``;
- the transport, ``iiod/uart.c``.

It is compiled with libiio's configuration for a bare-metal server
(``NO_THREADS=1``, ``WITH_EXTERNAL_BACKEND=1``, every client backend off).
It sees no-os's headers and ``no_os_config.h`` but not no-os's objects:
those reach the link once, through the executable. libiio's private headers
stay private, so the repo root's ``attr.h`` or ``network.h`` never shadow a
project's own.

The module also puts ``no-OS/include`` and libiio's ``include/`` on the
``no-os`` target. That is what lets ``drivers/accel/adxl355/iio_adxl355.c``,
built as part of no-os, include ``iio_device.h`` and ``iio_accel.h``.

The driver's sources
~~~~~~~~~~~~~~~~~~~~

.. code-block:: cmake

   # drivers/accel/CMakeLists.txt
   no_os_sources_ifdef(CONFIG_ACCEL_ADXL355 ${CMAKE_CURRENT_SOURCE_DIR}/adxl355/adxl355.c)

   if(CONFIG_ACCEL_IIO_ADXL355_CALLBACKS)
     no_os_sources_ifdef(CONFIG_ACCEL_IIO_ADXL355 ${CMAKE_CURRENT_SOURCE_DIR}/adxl355/iio_adxl355_callbacks.c)
   else()
     no_os_sources_ifdef(CONFIG_ACCEL_IIO_ADXL355 ${CMAKE_CURRENT_SOURCE_DIR}/adxl355/iio_adxl355.c)
   endif()
   target_include_directories(no-os PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/adxl355)

``CONFIG_ACCEL_IIO_ADXL355_CALLBACKS`` picks the raw-callback version of the
same driver (section 6). Both implement ``iio_adxl355.h``, so the example
does not change.

Building and flashing
~~~~~~~~~~~~~~~~~~~~~

.. code-block:: bash

   cd no-OS                                      # the no-OS checkout
   export NO_OS_DEP_LIBIIO_PATH=/path/to/libiio  # this libiio tree

   cmake --preset eval-adicup3029 \
         -B build/eval-adxl355-pmdz-iio-eval-adicup3029 \
         -DPROJECT_DEFCONFIG=eval-adxl355-pmdz/iio.conf
   cmake --build build/eval-adxl355-pmdz-iio-eval-adicup3029
   cmake --build build/eval-adxl355-pmdz-iio-eval-adicup3029 --target flash

The board preset (``board_configs/aducm3029/CMakePresets.json``) gives the
platform, the toolchain and the board defconfig; ``PROJECT_DEFCONFIG`` gives
the project and its config. The configure log says which libiio tree it
used: ``-- libiio configured from: /path/to/libiio``.

3. Start-up
-----------

``projects/eval-adxl355-pmdz/src/examples/iio_example/iio_example.c``. The
platform's ``main()`` brings the board up and calls it:

.. code-block:: c

   int example_main()
   {
   	struct adxl355_iio_dev *adxl355_iio_desc;
   	struct adxl355_iio_dev_init_param adxl355_iio_ip = {
   		.adxl355_dev_init = &adxl355_ip,
   	};
   	struct noos_iio_device_info info;
   	int ret;

   	ret = adxl355_iio_init(&adxl355_iio_desc, &adxl355_iio_ip);
   	if (ret)
   		return ret;

   	ret = adxl355_iio_get_device_info(adxl355_iio_desc, &info);
   	if (ret)
   		goto error;

   	ret = noos_iio_register_device(&info);
   	if (ret)
   		goto error;

   	return noos_iiod_run_uart(&adxl355_uart_ip);

   error:
   	adxl355_iio_remove(adxl355_iio_desc);
   	return ret;
   }

#. ``adxl355_iio_init()`` brings the part up through the part driver: init,
   soft reset, measurement mode with the temperature and DATA_RDY on.
#. ``adxl355_iio_get_device_info()`` fills a ``struct noos_iio_device_info``,
   the description of the device (section 4).
#. ``noos_iio_register_device()`` checks the description, copies it into
   the device registry and installs libiio's generic callbacks for it. A
   wrong table fails here with ``-EINVAL``, at boot, not when a client
   happens to read the attribute.
#. ``noos_iiod_run_uart()`` runs the server on the project's own UART
   parameters (``adxl355_uart_ip``) and does not return. It builds one
   libiio context from the registry when it starts (one ``iio:device0``
   named ``adxl355``) and serves it to every client that connects.

Several devices register the same way, one ``noos_iio_register_device()``
call each, before the server runs. Section 3a follows the information from
these calls to a client, step by step.

3a. How the information travels
-------------------------------

Three moments, each drawn from the ADXL355 project down to the IIOD server
and back. A box is a function; its file is on its right. ``[no-OS]`` marks
the ADXL355 project and driver, ``[libiio]`` libiio's ``no-OS/`` directory
and the IIOD core.

At start-up: the description becomes a libiio context
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The driver hands over a description once; libiio turns it into channels and
attributes once, before any client connects.

.. code-block:: none

   [no-OS]  main()                                   src/platform/aducm3029/main.c
              |
              v
            example_main()                           src/examples/iio_example/iio_example.c
              |
              |-- adxl355_iio_init(&desc, &ip)       drivers/accel/adxl355/iio_adxl355.c
              |     adxl355_init(), soft reset,      (adxl355.c, over no-OS SPI)
              |     measurement mode
              |
              |-- adxl355_iio_get_device_info(desc, &info)
              |     fills struct noos_iio_device_info:
              |       .name = "adxl355"   .dev = desc
              |       .channels  = adxl355_iio_channels   (4: accel_x/y/z, temp)
              |       .values    = adxl355_iio_values     (5: scale, ODR, HPF, temp)
              |       .read_raw, .write_raw, .read_scan, .wait_scan, .reg_read ...
              |
              |-- (1) noos_iio_register_device(&info)
              |         |
              |   ======|=================================================== [libiio]
              |         v
              |       noos_iio_register_device()     backend.c
              |         copies info into noos_iio_devices[]
              |         noos_iio_table_install()     backend_table.c
              |           checks the tables (-EINVAL if wrong)
              |           sets .add_channels = table_add_channels
              |                .read_attr    = table_read_attr
              |                .write_attr   = table_write_attr
              |                .read_samples = table_read_samples
              |
              '-- (2) noos_iiod_run_uart(&adxl355_uart_ip)
                        |
                  ======|=================================================== [libiio]
                        v
                      noos_iiod_run_uart()           iiod/uart.c
                        no_os_uart_init(uart_ip)
                        iiod_uart_run()
                          iio_create_context(params, "no-os:")
                            |
                            v
                          noos_create_context()      backend.c
                            for each registered device:
                              iio_context_add_device("iio:device0", "adxl355")
                              pdata = &noos_iio_devices[i]   (the copied info)
                              info->add_channels() = table_add_channels()
                                for each noos_iio_chan:            backend_table.c
                                  id from type and modifier: "accel_x"
                                  iio_device_add_channel()   scan index 0, le:s20/32
                                  iio_channel_add_attr()     "raw"   in_accel_x_raw
                                                             "scale" in_accel_scale
                              iio_device_add_buffer(), scan elements accel_x/y/z
                          iio_context_get_xml()      the context as XML, once
                          loop: iiod_interpreter(ctx, uart, read, write, xml)
                                                     iiod/responder.c, waits on the UART

A client reads an attribute
~~~~~~~~~~~~~~~~~~~~~~~~~~~

``iio_attr -c adxl355 accel_x sampling_frequency``: the request goes down to
the driver's ``get`` callback, the number comes back as text.

.. code-block:: none

   PC        iio_attr  --- READ_ATTR(device 0, channel accel_x, "sampling_frequency") --.
                                                                                      | UART
   [libiio]  iiod_uart_read()                        iiod/uart.c                      |
             handle_read_attr()                      iiod/responder.c   <-------------'
               iio_attr_read_raw(attr)               attr.c
                 backend ops->read_attr
                   = noos_read_attr()                backend.c
                       info = iio_device_get_pdata(dev)
                       info->read_attr()
                         = table_read_attr()         backend_table.c
                             noos_iio_chan_of(chn)   -> the accel_x table entry
                             find_info()             "sampling_frequency" -> NOOS_IIO_INFO_SAMP_FREQ
                             read_info()
                               find_value(ACCEL, SAMP_FREQ) -> the NOOS_IIO_LIST entry
                               value_read()
   [no-OS]                       val->get(info->dev) = adxl355_iio_get_odr(desc)
                                   -> 0  (dev->odr_lpf, the row in use)
   [libiio]                      table[0] = {4000, 0}, NOOS_IIO_VAL_INT_PLUS_MICRO
                               format_val()          -> "4000.000000"
             answer to the client  --- "4000.000000" --------------------------------> PC

An attribute with no value entry takes the same path to ``read_info()``,
which then calls ``info->read_raw()`` instead (``adxl355_iio_read_raw()`` for
``raw`` and ``calibbias``). A write takes it the other way:
``table_write_attr()`` parses the text with the entry's type (or
``write_raw_get_fmt()``'s), then calls the entry's ``set`` or
``write_raw()``.

A client captures samples
~~~~~~~~~~~~~~~~~~~~~~~~~

``iio_rwdev -s 250 -b 25 adxl355 accel_x accel_y accel_z``: the driver
gives numbers one scan at a time, libiio packs them into the client's
block.

.. code-block:: none

   PC        iio_rwdev  --- OPEN_BUFFER(mask x|y|z), ENABLE_BUFFER --------------------.
                                                                                      |
   [libiio]  handle_open_buffer()  -> noos_open_buffer()        backend.c   <---------'
             handle_enable_buffer() -> noos_enable_buffer()     (info->enable_buffer if set)

   PC        --- TRANSFER_BLOCK(25 samples x 12 bytes) ---------------------------------.
                                                                                       |
   [libiio]  handle_transfer_block()                 iiod/responder.c   <--------------'
               iio_block_enqueue()                   block.c
                 backend ops->enqueue_block
                   = noos_enqueue_block()            backend.c
                       info->read_samples()
                         = table_read_samples()      backend_table.c
                             scan_layout()           x at 0, y at 4, z at 8 (12 bytes)
                             for each of the 25 samples:
                               table_read_scan()
   [no-OS]                       info->wait_scan()   = adxl355_iio_wait_drdy()   polls DATA_RDY
                                 info->read_scan()   = adxl355_iio_read_scan()
                                   adxl355_get_raw_xyz() over SPI
                                   vals[0..2] = x, y, z (sign-extended)
   [libiio]                    put_value() x3        le 32-bit words, at their offsets
             the 300-byte block  -------------------------------------------------------> PC

Nothing above ``table_read_samples()`` knows it is an accelerometer, and
nothing below ``read_scan()`` knows about libiio: the driver gives numbers,
the table layer turns them into what the client asked for. A raw-callback
driver (section 6) sets ``.read_attr`` and ``.read_samples`` itself, so
``info->read_attr()`` and ``info->read_samples()`` go straight to the
driver instead of to ``table_read_attr()`` and ``table_read_samples()``.

4. Writing iio_adxl355.c
------------------------

The IIO driver describes the device the way a Linux IIO driver does: one
table of channels (Linux's ``iio_chan_spec``) and one set of handlers
(``iio_info``). The enums are Linux's, in Linux's order, with ``NOOS_`` in
front, so a Linux driver's masks and ``read_raw`` carry over almost line by
line.

The instance
~~~~~~~~~~~~

.. code-block:: c

   /* iio_adxl355.h */
   #include "adxl355.h"
   #include "iio_device.h"

   #define ADXL355_IIO_HPF_ENTRIES	7

   struct adxl355_iio_dev {
   	struct adxl355_dev *adxl355_dev;
   	/* -3 dB HPF corners for the current ODR, {integer, micro}. */
   	int adxl355_hpf_3db_table[ADXL355_IIO_HPF_ENTRIES][2];
   };

It holds the no-OS part driver and the one thing that changes at run time:
the high-pass filter corners, which follow the output data rate.

The channels
~~~~~~~~~~~~

.. code-block:: c

   /* Each axis: 20 bits, sign-extended into a little-endian 32-bit word. */
   #define ADXL355_IIO_SCAN_TYPE {						\
   	.sign = 's',							\
   	.realbits = 20,							\
   	.storagebits = 32,						\
   	.endianness = NOOS_IIO_LE,					\
   }

   static const struct noos_iio_chan adxl355_iio_channels[] = {
   	NOOS_IIO_ACCEL_XYZ(NOOS_IIO_BIT(RAW) | NOOS_IIO_BIT(CALIBBIAS) |
   			   NOOS_IIO_BIT(SCALE) | NOOS_IIO_BIT(SAMP_FREQ) |
   			   NOOS_IIO_BIT(HIGH_PASS_FILTER_3DB_FREQUENCY),
   			   NOOS_IIO_BIT(SAMP_FREQ) |
   			   NOOS_IIO_BIT(HIGH_PASS_FILTER_3DB_FREQUENCY),
   			   ADXL355_IIO_SCAN_TYPE),
   	/* Not buffered, as in no-OS's IIO driver. */
   	NOOS_IIO_TEMP_CHANNEL(-1, NOOS_IIO_BIT(RAW) | NOOS_IIO_BIT(SCALE) |
   			      NOOS_IIO_BIT(OFFSET), 0, NOOS_IIO_NO_SCAN_TYPE),
   };

The macros come from ``iio_accel.h`` and ``iio_temp.h``, the standard
channels of those types. The driver gives three things: the standard
attributes its part has, which of them have a list, and the scan type
(the last argument). The macro does the rest:

- ``NOOS_IIO_ACCEL_XYZ`` makes ``accel_x``, ``accel_y`` and ``accel_z``:
  type ``NOOS_IIO_ACCEL``, modified by the axis, scan elements 0 to 2,
  ``priv`` the axis (0 to 2).
- It places each attribute where every accelerometer has it. The part's
  settings (``NOOS_IIO_ACCEL_SHARED``: scale, sampling frequency, the filter
  corners, oversampling ratio) are shared by type, one value for all the
  axes. Everything else (``raw``, ``calibbias``) is the axis' own.
- ``NOOS_IIO_TEMP_CHANNEL(-1, ...)`` makes ``temp``, not a scan element,
  with every attribute its own.

The scan type is Linux's ``struct iio_scan_type``, field for field, with
``NOOS_`` in front of the endianness: ``sign`` ('s' or 'u'), ``realbits``,
``storagebits``, ``shift``, ``repeat`` and ``endianness``
(``NOOS_IIO_BE``, ``NOOS_IIO_LE`` or ``NOOS_IIO_CPU``). libiio turns it into
the format a client decodes with, ``le:s20/32>>0`` here; the scale it
converts with is the channel's ``scale`` attribute. Linux's ADXL355 pushes
its register bytes as they are, so its scan type is big-endian with a shift
of 4 (``be:s20/32>>4``); here the driver gives plain numbers and libiio
packs them, so little-endian with no shift is simpler. A client decodes the
same values either way.

The result has the same masks as Linux's own ADXL355 driver:

.. code-block:: c

   /* Linux, drivers/iio/accel/adxl355_core.c */
   #define ADXL355_ACCEL_CHANNEL(index, reg, axis) {			\
   	.type = IIO_ACCEL,						\
   	.modified = 1,							\
   	.channel2 = IIO_MOD_##axis,					\
   	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |			\
   			      BIT(IIO_CHAN_INFO_CALIBBIAS),		\
   	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE) |		\
   				    BIT(IIO_CHAN_INFO_SAMP_FREQ) |	\
   		BIT(IIO_CHAN_INFO_HIGH_PASS_FILTER_3DB_FREQUENCY),	\
   	...

A type with no header yet (a DAC, a PLL, an IMU) writes its
``struct noos_iio_chan`` entries directly, with the same fields: ``type``,
``channel``, ``channel2``, ``indexed``, ``modified``, ``differential``,
``scan_index``, ``scan_type`` and the ``info_mask_*`` masks.

The values
~~~~~~~~~~

Half of the ADXL355's attributes are data: a constant, or a value picked
from a table by index. They take one line each and are served by libiio
before ``read_raw`` is ever called:

.. code-block:: c

   #define ADXL355_IIO_VALUES(_scale_nano, _temp_offset)			\
   	NOOS_IIO_CONST(ACCEL, SCALE, INT_PLUS_NANO, 0, _scale_nano),	\
   	NOOS_IIO_LIST(ACCEL, SAMP_FREQ, INT_PLUS_MICRO,			\
   		      adxl355_odr_table, adxl355_iio_get_odr,		\
   		      adxl355_iio_set_odr),				\
   	NOOS_IIO_LIST_FN(ACCEL, HIGH_PASS_FILTER_3DB_FREQUENCY,		\
   			 INT_PLUS_MICRO, adxl355_iio_hpf_avail,		\
   			 adxl355_iio_get_hpf, adxl355_iio_set_hpf),	\
   	NOOS_IIO_CONST(TEMP, SCALE, INT_PLUS_MICRO, -110, 497238),	\
   	NOOS_IIO_CONST(TEMP, OFFSET, INT_PLUS_MICRO, _temp_offset, 250000)

   static const struct noos_iio_value adxl355_iio_values[] = {
   	ADXL355_IIO_VALUES(38245, -2111),
   };
   static const struct noos_iio_value adxl357_iio_values[] = {
   	ADXL355_IIO_VALUES(191229, -2111),
   };
   static const struct noos_iio_value adxl359_iio_values[] = {
   	ADXL355_IIO_VALUES(191229, -2078),
   };

.. list-table::
   :header-rows: 1
   :widths: 22 22 56

   * - Macro
     - For
     - What libiio does with it
   * - ``NOOS_IIO_CONST``
     - a fixed value
     - reads it; a write is ``-EPERM``
   * - ``NOOS_IIO_LIST``
     - a value from a fixed table
     - reads the row ``get`` names. A write is parsed with the entry's type
       and must be one of the rows (``-EINVAL`` otherwise); then ``set`` gets
       its index. ``_available`` lists the table.
   * - ``NOOS_IIO_LIST_FN``
     - a table that changes
     - the same, with the table from a function: the filter corners follow
       the ODR

An entry serves every channel of its type (``ACCEL``, ``TEMP``). The 357 and
359 differ only in their scale and temperature offset, so each part has its
own three-line table. The callbacks only deal with indexes:

.. code-block:: c

   static const int adxl355_odr_table[] = {
   	4000, 0,  2000, 0,  1000, 0,  500, 0,  250, 0,  125, 0,
   	62, 500000,  31, 250000,  15, 625000,  7, 813000,  3, 906000,
   };

   static int adxl355_iio_get_odr(void *dev)
   {
   	return adxl355_of(dev)->odr_lpf;		/* the row in use */
   }

   static int adxl355_iio_set_odr(void *dev, unsigned int idx)
   {
   	int ret;

   	ret = adxl355_set_odr_lpf(adxl355_of(dev), idx);
   	if (ret)
   		return ret;

   	/* The HPF corners scale with the ODR. */
   	adxl355_fill_hpf_table(dev);

   	return 0;
   }

read_raw and write_raw
~~~~~~~~~~~~~~~~~~~~~~

What is left needs the hardware: the readings and the per-axis offsets.

.. code-block:: c

   static int adxl355_iio_read_raw(void *dev, const struct noos_iio_chan *ch,
   				enum noos_iio_info info, int *val, int *val2)
   {
   	int32_t xyz[3];
   	int ret;

   	switch (info) {
   	case NOOS_IIO_INFO_RAW:
   		if (ch->type == NOOS_IIO_TEMP) {
   			ret = adxl355_iio_read_temp(dev, val);
   			if (ret)
   				return ret;

   			return NOOS_IIO_VAL_INT;
   		}

   		ret = adxl355_iio_read_xyz(dev, xyz);
   		if (ret)
   			return ret;

   		*val = xyz[ch->priv];		/* priv is the axis, 0..2 */
   		return NOOS_IIO_VAL_INT;
   	case NOOS_IIO_INFO_CALIBBIAS:
   		ret = adxl355_iio_get_calibbias(dev, (unsigned int)ch->priv, val);
   		if (ret)
   			return ret;

   		return NOOS_IIO_VAL_INT;
   	default:
   		return -EINVAL;
   	}
   }

   static int adxl355_iio_write_raw(void *dev, const struct noos_iio_chan *ch,
   				 enum noos_iio_info info, int val, int val2)
   {
   	if (info != NOOS_IIO_INFO_CALIBBIAS)
   		return -EPERM;

   	return adxl355_iio_set_calibbias(dev, (unsigned int)ch->priv, val);
   }

   /* calibbias is an integer: "1.5" is refused. */
   static int adxl355_iio_write_raw_get_fmt(void *dev,
   					 const struct noos_iio_chan *ch,
   					 enum noos_iio_info info)
   {
   	return NOOS_IIO_VAL_INT;
   }

- ``read_raw`` returns the value and its type, here ``NOOS_IIO_VAL_INT``.
  Any of Linux's ``IIO_VAL_*`` types works: ``INT_PLUS_MICRO``,
  ``INT_PLUS_NANO``, ``FRACTIONAL``, ``FRACTIONAL_LOG2``, ``INT_64``,
  ``INT_PLUS_MICRO_DB``, ``CHAR``, and ``INT_MULTIPLE`` from
  ``read_raw_multi``. libiio prints them as Linux does.
- ``write_raw`` gets the value already parsed; ``write_raw_get_fmt`` says
  how to parse it (micro when it is left out).
- A part with an accelerometer and a temperature lists both in its one
  table and switches on ``ch->type``, as Linux drivers do.

The buffer
~~~~~~~~~~

.. code-block:: c

   /* One scan: the axes from one reading, whichever are enabled. */
   static int adxl355_iio_read_scan(void *dev, uint32_t enabled, int64_t *vals)
   {
   	int32_t xyz[3];
   	unsigned int i;
   	int ret;

   	ret = adxl355_iio_read_xyz(dev, xyz);
   	if (ret)
   		return ret;

   	for (i = 0; i < 3; i++)
   		vals[i] = xyz[i];

   	return 0;
   }

For every sample the client asks for, libiio calls ``wait_scan`` first (here
``adxl355_iio_wait_drdy``, which polls DATA_RDY, so the part paces the
buffer at its ODR), then ``read_scan``. ``read_scan`` gives one value per
channel, ``vals[i]`` for bit ``i`` of ``enabled``, ``i`` being the channel's
index in the table. libiio lays the values out as the client expects: the
enabled channels in order, each at its format (``le:s20/32``),
sign-extended.

The device
~~~~~~~~~~

.. code-block:: c

   int adxl355_iio_get_device_info(struct adxl355_iio_dev *iio_dev,
   				struct noos_iio_device_info *info)
   {
   	const struct noos_iio_value *values;
   	enum adxl355_type type;

   	if (!iio_dev || !info)
   		return -EINVAL;

   	type = iio_dev->adxl355_dev->dev_type;
   	values = type == ID_ADXL359 ? adxl359_iio_values :
   		 type == ID_ADXL357 ? adxl357_iio_values : adxl355_iio_values;

   	*info = (struct noos_iio_device_info) {
   		.name = adxl355_iio_name(type),		/* "adxl355" */
   		.dev = iio_dev,				/* passed to every callback */
   		.channels = adxl355_iio_channels,
   		.num_channels = NO_OS_ARRAY_SIZE(adxl355_iio_channels),
   		.values = values,
   		.nb_values = NO_OS_ARRAY_SIZE(adxl355_iio_values),
   		.read_raw = adxl355_iio_read_raw,
   		.write_raw = adxl355_iio_write_raw,
   		.write_raw_get_fmt = adxl355_iio_write_raw_get_fmt,
   		.read_scan = adxl355_iio_read_scan,
   		.wait_scan = adxl355_iio_wait_drdy,
   		.reg_read = adxl355_iio_reg_read,
   		.reg_write = adxl355_iio_reg_write,
   	};

   	return 0;
   }

Every callback gets ``.dev``, the driver's instance. ``reg_read`` and
``reg_write`` give ``iio_reg`` access to the part's registers.

What registration checks
~~~~~~~~~~~~~~~~~~~~~~~~

``noos_iio_register_device()`` refuses the description with ``-EINVAL``
when:

- an attribute is in two masks, or an ``_available`` bit has no attribute;
- a channel id cannot be made (a differential channel that is not indexed,
  a missing modifier);
- an attribute is served neither by a value entry nor by ``read_raw``, or a
  list neither by a value entry nor by ``read_avail``;
- a value entry has no channel of its type, is given twice, or can be
  written but has a type a write cannot be parsed as (FRACTIONAL).
- a scan element's ``scan_type`` has no sign, storage that is not 8 to 64
  bits in whole bytes, or data and shift wider than its storage.

5. What libiio does with it
---------------------------

From that description, the server shows what Linux's ADXL355 driver shows.
This is ``iio_info`` on the EVAL-ADICUP3029:

.. code-block:: none

   iio:device0: adxl355 (buffer capable)
   	4 channels found:
   		accel_x:  (input, index: 0, format: le:s20/32>>0)
   		7 channel-specific attributes found:
   			attr  0: calibbias (in_accel_x_calibbias) value: 0
   			attr  1: filter_high_pass_3db_frequency (in_accel_filter_high_pass_3db_frequency) value: 0.000000
   			attr  2: filter_high_pass_3db_frequency_available (in_accel_filter_high_pass_3db_frequency_available) options: 0.000000 9.880000 2.483360 0.621800 0.154480 0.038160 0.009520
   			attr  3: raw (in_accel_x_raw) value: 7823
   			attr  4: sampling_frequency (in_accel_sampling_frequency) value: 4000.000000
   			attr  5: sampling_frequency_available (in_accel_sampling_frequency_available) options: 4000.000000 2000.000000 1000.000000 500.000000 250.000000 125.000000 62.500000 31.250000 15.625000 7.813000 3.906000
   			attr  6: scale (in_accel_scale) value: 0.000038245
   		accel_y:  ...
   		accel_z:  ...
   		temp:  (input)
   		3 channel-specific attributes found:
   			attr  0: offset (in_temp_offset) value: -2111.250000
   			attr  1: raw (in_temp_raw) value: 1860
   			attr  2: scale (in_temp_scale) value: -110.497238
   	1 buffer found:
   		buffer0: (input)
   		3 scan elements found: accel_x, accel_y, accel_z

.. list-table::
   :header-rows: 1
   :widths: 26 16 32 26

   * - Attribute
     - Mask
     - Where a client sees it
     - Served by
   * - ``raw``
     - separate
     - each axis, ``in_accel_x_raw``
     - ``read_raw``
   * - ``calibbias``
     - separate
     - each axis, ``in_accel_x_calibbias``
     - ``read_raw``/``write_raw``
   * - ``scale``
     - shared by type
     - each axis, ``in_accel_scale``
     - ``NOOS_IIO_CONST``
   * - ``sampling_frequency``
     - shared by type
     - each axis, ``in_accel_sampling_frequency``
     - ``NOOS_IIO_LIST``
   * - ``filter_high_pass_3db_frequency``
     - shared by type
     - each axis, ``in_accel_filter_high_pass_3db_frequency``
     - ``NOOS_IIO_LIST_FN``
   * - ``temp``: ``raw``, ``scale``, ``offset``
     - separate
     - ``in_temp_raw``, ``in_temp_scale``, ``in_temp_offset``
     - ``read_raw``, ``NOOS_IIO_CONST``

On the board:

- Writing ``sampling_frequency`` through ``accel_y`` changes it on all three
  axes, and the filter list follows it.
- A sampling frequency not in the table (``300``) gives ``-EINVAL`` and
  leaves the old one. Writing ``scale`` gives ``-EPERM``, and
  ``calibbias 1.5`` gives ``-EINVAL``.
- A board lying flat reads z ≈ 257000 LSB × 0.000038245 m/s² per LSB
  ≈ 9.83 m/s², gravity. The temperature is
  (raw + offset) × scale = (1860 − 2111.25) × −110.497 ≈ 27760 m°C, 27.8 °C.

6. The raw-callback version
---------------------------

The table is the usual way to write a driver, but not the only one. A
device can set the raw callbacks of ``noos_iio_device_info`` instead and do
everything by hand with libiio's backend API. ``iio_adxl355_callbacks.c`` is
the same ADXL355 written that way, attribute for attribute. It is built with
``CONFIG_ACCEL_IIO_ADXL355_CALLBACKS=y``
(``projects/eval-adxl355-pmdz/iio_callbacks.conf``).

.. code-block:: c

   static int adxl355_iio_add_channels(void *dev, struct iio_device *iio_dev)
   {
   	const struct iio_data_format fmt = {
   		.length = 32, .bits = 20, .is_signed = true,
   	};
   	...
   	for (axis = 0; axis < ADXL355_IIO_AXES; axis++) {
   		chn = iio_device_add_channel(iio_dev, axis,
   					     adxl355_iio_axis_ids[axis], NULL,
   					     NULL, false, true, &fmt);
   		...
   		/* "scale" -> in_accel_scale, "raw" -> in_accel_x_raw */
   		for (i = 0; i < NO_OS_ARRAY_SIZE(adxl355_iio_accel_attrs); i++)
   			adxl355_iio_add_channel_attrs(chn, ...);
   	}
   	...
   }

   static int adxl355_iio_read_accel_attr(struct adxl355_iio_dev *desc,
   				       unsigned int axis, const char *name,
   				       char *dst, size_t len)
   {
   	...
   	if (!strcmp(name, "scale"))
   		return (int)iio_value_format_fixed(dst, len, 0,
   						   adxl355_iio_scale_nano(desc), 9);

   	if (!strcmp(name, "sampling_frequency"))
   		return (int)iio_value_format_fixed(dst, len, odr[0], odr[1], 6);
   	...
   }

   	*info = (struct noos_iio_device_info) {
   		.name = adxl355_iio_name(type),
   		.dev = iio_dev,
   		.add_channels = adxl355_iio_add_channels,
   		.read_attr = adxl355_iio_read_attr,
   		.write_attr = adxl355_iio_write_attr,
   		.read_samples = adxl355_iio_read_samples,
   		.reg_read = adxl355_iio_reg_read,
   		.reg_write = adxl355_iio_reg_write,
   	};

.. list-table::
   :header-rows: 1
   :widths: 20 38 42

   * -
     - ``iio_adxl355.c``
     - ``iio_adxl355_callbacks.c``
   * - Channels and attributes
     - a channel table from the standard macros
     - ``add_channels``: ``iio_device_add_channel()``,
       ``iio_channel_add_attr()``, Linux file names written out
   * - Values
     - a value table and ``read_raw``/``write_raw``
     - ``read_attr``/``write_attr`` find each attribute by name, format and
       parse the text, match writes against the tables
   * - Buffer
     - ``read_scan`` + ``wait_scan``; libiio packs
     - ``read_samples`` waits for DATA_RDY and packs the enabled axes itself
   * - Size
     - 456 lines
     - 653 lines

The two firmwares give the same ``iio_info``, line for line. The callbacks
are for a device the table cannot describe yet, such as an output device
through ``write_samples``. Anything the table covers is shorter as a table,
and checked when it is registered.

7. Trying it from the PC
------------------------

With libiio v1 built on the PC, one client at a time on the UART:

.. code-block:: bash

   export LD_LIBRARY_PATH=/path/to/libiio/build
   U=serial:/dev/ttyACM0,115200

   iio_info -u $U                                            # the whole device
   iio_attr -u $U -c adxl355 accel_y sampling_frequency 125  # write through one axis
   iio_attr -u $U -c adxl355 accel_z sampling_frequency      # read on another: 125.000000
   iio_attr -u $U -c adxl355 accel_x calibbias 100           # per axis
   iio_reg  -u $U adxl355 0x00                               # DEVID_AD: 0xad
   iio_rwdev -u $U -s 250 -b 25 adxl355 accel_x accel_y accel_z > adxl.bin

The capture holds three little-endian 32-bit words per sample: x, y, z. If a
client is killed in the middle of a capture, reset the board before the next
one.

8. Porting the next driver
--------------------------

For a no-OS driver that has an ``iio_<part>.c`` for no-OS's own server
today:

#. Find the part's Linux driver, if it has one. Its ``iio_chan_spec`` masks,
   ``read_raw`` and ``write_raw`` are the target, so clients see the same
   device on Linux and on the board.
#. Write the channel table with the type's macros (``iio_adc.h``,
   ``iio_accel.h``, ``iio_temp.h``), or by hand as ``noos_iio_chan`` entries
   for a type without a header yet.
#. Move every constant and every value picked from a table into the value
   table.
#. Write ``read_raw``/``write_raw`` for what needs the hardware, switching on
   ``ch->type`` where the part has several types.
#. Attributes outside Linux's standard set become ``noos_iio_ext_attr``
   entries on the channel or in ``.attrs``; a choice among names is a
   ``noos_iio_enum`` (``iio_enum.h``).
#. Add ``read_scan``, and ``wait_scan`` if the part paces itself.
#. Keep the old file for no-OS's server: the new one includes
   ``iio_device.h``, never ``iio_types.h``. Kconfig picks one: the new driver
   ``depends on LIBIIO``, the project's example
   ``depends on LIBIIO && !IIO``.
#. Check it: register the device, then compare ``iio_info`` with the Linux
   driver's. libiio's ``tests/test_table.c`` shows how the table layer is
   checked on the PC without a board.

Where things are
----------------

- libiio, branch ``no-os-backend-support``, ``no-OS/``: the server, the
  backend, the standard channels and ``ARCHITECTURE.md``.
- no-OS, branch ``adxl355-pmdz-libiio-example``: ``drivers/accel/adxl355/``
  and ``projects/eval-adxl355-pmdz/``.
