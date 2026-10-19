/*
 * Copyright (c) 2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

/* The name-list attributes of iio_enum.h. */

#include <errno.h>
#include <stdint.h>

#include <iio/iio.h>
#include <iio/iio-backend.h>
#include <iio_enum.h>

static const struct noos_iio_enum *enum_of(intptr_t priv)
{
	return (const struct noos_iio_enum *)priv;
}

static bool enum_has(const struct noos_iio_enum *e, unsigned int idx)
{
	if (idx >= e->nb_items || !e->items[idx])
		return false;

	return !e->mask || (idx < 32 && (e->mask & (UINT32_C(1) << idx)));
}

int noos_iio_enum_read(void *dev, const struct noos_iio_chan *ch,
		       intptr_t priv, char *dst, size_t len)
{
	const struct noos_iio_enum *e = enum_of(priv);
	int idx;

	if (!e || !e->get)
		return -EINVAL;

	idx = e->get(dev, ch);
	if (idx < 0)
		return idx;

	if ((unsigned int)idx >= e->nb_items || !e->items[idx])
		return -EINVAL;

	return (int)iio_value_format_str(dst, len, e->items[idx]);
}

int noos_iio_enum_write(void *dev, const struct noos_iio_chan *ch,
			intptr_t priv, const char *src, size_t len)
{
	const struct noos_iio_enum *e = enum_of(priv);
	int idx, ret;

	if (!e)
		return -EINVAL;
	if (!e->set)
		return -EPERM;

	idx = iio_value_match_str(e->items, e->nb_items, src, len);
	if (idx < 0)
		return idx;

	if (!enum_has(e, (unsigned int)idx))
		return -ENOTSUP;

	ret = e->set(dev, ch, (unsigned int)idx);
	if (ret)
		return ret;

	return (int)len;
}

int noos_iio_enum_available(void *dev, const struct noos_iio_chan *ch,
			    intptr_t priv, char *dst, size_t len)
{
	const struct noos_iio_enum *e = enum_of(priv);
	size_t pos = 0;
	unsigned int i;
	ssize_t ret;

	if (!e)
		return -EINVAL;
	if (!len)
		return -ERANGE;

	dst[0] = '\0';

	for (i = 0; i < e->nb_items; i++) {
		if (!enum_has(e, i))
			continue;

		if (pos) {
			ret = iio_value_format_str(dst + pos, len - pos, " ");
			if (ret < 0)
				return (int)ret;
			pos += (size_t)ret - 1;
		}

		ret = iio_value_format_str(dst + pos, len - pos, e->items[i]);
		if (ret < 0)
			return (int)ret;
		pos += (size_t)ret - 1;
	}

	return (int)pos + 1;
}
