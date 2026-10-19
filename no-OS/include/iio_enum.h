/*
 * Copyright (c) 2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef NOOS_INCLUDE_IIO_ENUM_H_
#define NOOS_INCLUDE_IIO_ENUM_H_

/*
 * An attribute that takes one of a list of names, as Linux's IIO_ENUM, for
 * a table-driven device's ext attributes (iio_device.h).
 */

#include <stdbool.h>
#include <stdint.h>

#include <iio_device.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * struct noos_iio_enum - an attribute that takes one of a list of names,
 * as Linux's IIO_ENUM
 * @items:    the names, by index; a NULL entry is never shown nor accepted
 * @nb_items: number of entries in @items
 * @mask:     the indexes the hardware has, as bits (0: all of them); the
 *            others are refused with -ENOTSUP and not listed
 * @get:      the index in use on @ch, or a negative error code
 * @set:      switch @ch to index @idx (may be NULL: read-only); an index
 *            the hardware cannot take returns -ENOTSUP
 */
struct noos_iio_enum {
	const char *const *items;
	unsigned int nb_items;
	uint32_t mask;
	int (*get)(void *dev, const struct noos_iio_chan *ch);
	int (*set)(void *dev, const struct noos_iio_chan *ch, unsigned int idx);
};

int noos_iio_enum_read(void *dev, const struct noos_iio_chan *ch,
		       intptr_t priv, char *dst, size_t len);
int noos_iio_enum_write(void *dev, const struct noos_iio_chan *ch,
			intptr_t priv, const char *src, size_t len);
int noos_iio_enum_available(void *dev, const struct noos_iio_chan *ch,
			    intptr_t priv, char *dst, size_t len);

/* noos_iio_ext_attr entries for a struct noos_iio_enum: the attribute
 * itself, and its "_available" list of names. */
#define NOOS_IIO_ENUM(_name, _e) {				\
	.name = (_name),					\
	.read = noos_iio_enum_read,				\
	.write = noos_iio_enum_write,				\
	.priv = (intptr_t)(_e),					\
}

#define NOOS_IIO_ENUM_AVAILABLE(_name, _e) {			\
	.name = _name "_available",				\
	.read = noos_iio_enum_available,			\
	.priv = (intptr_t)(_e),					\
}

#ifdef __cplusplus
}
#endif

#endif /* NOOS_INCLUDE_IIO_ENUM_H_ */
