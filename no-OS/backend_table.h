/*
 * Copyright (c) 2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef NOOS_BACKEND_TABLE_H_
#define NOOS_BACKEND_TABLE_H_

#include <iio_device.h>

/* Check a table-driven device and fill the callbacks it left NULL with the
 * generic ones (backend_table.c). Returns -EINVAL for a table that is
 * wrong or too large. */
int noos_iio_table_install(struct noos_iio_device_info *info);

#endif /* NOOS_BACKEND_TABLE_H_ */
