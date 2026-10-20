/*
 * Copyright (c) 2025 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include <iio/iio-backend.h>
#include <iio-private.h>
#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <no_os_print_log.h>
#include <iio_device.h>

#include "backend_table.h"

#define NOOS_BACKEND_VERSION "no-OS 1.0 " __DATE__ " " __TIME__

struct noos_iio_device_info noos_iio_devices[NOOS_IIO_MAX_DEVICES];
unsigned int noos_iio_device_count;

/* Buffers open per registered device: no format may change under one. */
static unsigned int noos_open_buffers[NOOS_IIO_MAX_DEVICES];

/* The debug attribute a device with reg_read/reg_write gets, as on Linux. */
#define NOOS_REG_ACCESS_ATTR "direct_reg_access"

/* The register direct_reg_access reads, per registered device. */
static uint32_t noos_reg_address[NOOS_IIO_MAX_DEVICES];

struct iio_buffer_pdata {
	const struct iio_device *dev;
	/* Owned by the buffer stream, valid until the buffer is closed. */
	const struct iio_channels_mask *mask;
	bool enabled;
};

struct iio_block_pdata {
	struct iio_buffer_pdata *buf;
	size_t size;
	void *data;
	size_t bytes_used;
	int error;
};

static unsigned int *noos_open_count(const struct iio_device *dev)
{
	const struct noos_iio_device_info *info = (const struct noos_iio_device_info *)
		iio_device_get_pdata(dev);

	return &noos_open_buffers[info - noos_iio_devices];
}

static struct iio_buffer_pdata *
noos_open_buffer(const struct iio_device *dev,
		 unsigned int idx,
		 struct iio_channels_mask *mask)
{
	struct iio_buffer_pdata *pdata = zalloc(sizeof(*pdata));

	if (!pdata)
		return iio_ptr(-ENOMEM);

	pdata->dev = dev;
	pdata->mask = mask;
	(*noos_open_count(dev))++;

	return pdata;
}

static struct noos_iio_device_info *
noos_buffer_info(const struct iio_buffer_pdata *pdata)
{
	return (struct noos_iio_device_info *)iio_device_get_pdata(pdata->dev);
}

static int noos_enable_buffer(struct iio_buffer_pdata *pdata,
			      size_t nb_samples, bool enable, bool cyclic)
{
	struct noos_iio_device_info *info = noos_buffer_info(pdata);
	int ret;

	if (info && pdata->enabled != enable) {
		if (info->enable_buffer) {
			ret = info->enable_buffer(info->dev, enable);
			if (ret)
				return ret;
		}
	}

	pdata->enabled = enable;

	return 0;
}

static void noos_close_buffer(struct iio_buffer_pdata *pdata)
{
	/* A client that drops the connection never disables the buffer. */
	if (pdata->enabled)
		noos_enable_buffer(pdata, 0, false, false);

	(*noos_open_count(pdata->dev))--;
	free(pdata);
}

static void noos_cancel_buffer(struct iio_buffer_pdata *pdata)
{
}

static ssize_t noos_readbuf(struct iio_buffer_pdata *pdata,
			    void *dst, size_t len)
{
	struct noos_iio_device_info *info;
	int ret;

	if (!pdata || !pdata->dev)
		return -EINVAL;

	info = (struct noos_iio_device_info *)iio_device_get_pdata(pdata->dev);
	if (!info || !info->read_samples)
		return -ENOSYS;

	ret = info->read_samples(info->dev, pdata->dev, pdata->mask, dst, len);
	if (ret)
		return ret;

	return (ssize_t)len;
}

static ssize_t noos_writebuf(struct iio_buffer_pdata *pdata,
			     const void *src, size_t len)
{
	struct noos_iio_device_info *info;
	int ret;

	if (!pdata || !pdata->dev)
		return -EINVAL;

	info = (struct noos_iio_device_info *)iio_device_get_pdata(pdata->dev);
	if (!info || !info->write_samples)
		return -ENOSYS;

	ret = info->write_samples(info->dev, pdata->dev, pdata->mask, src,
				  len);
	if (ret)
		return ret;

	return (ssize_t)len;
}

static struct iio_block_pdata *
noos_create_block(struct iio_buffer_pdata *buf, size_t size, void **data)
{
	struct iio_block_pdata *pdata = zalloc(sizeof(*pdata));

	if (!pdata)
		return iio_ptr(-ENOMEM);

	pdata->data = malloc(size);
	if (!pdata->data) {
		free(pdata);
		return iio_ptr(-ENOMEM);
	}

	pdata->buf = buf;
	pdata->size = size;
	*data = pdata->data;

	return pdata;
}

static void noos_free_block(struct iio_block_pdata *pdata)
{
	free(pdata->data);
	free(pdata);
}

static int noos_enqueue_block(struct iio_block_pdata *pdata,
			      size_t bytes_used, bool cyclic)
{
	struct iio_buffer_pdata *buf = pdata->buf;
	struct noos_iio_device_info *info = noos_buffer_info(buf);

	if (!info)
		return -ENOSYS;

	pdata->bytes_used = bytes_used;

	if (info->direction) {
		if (!info->write_samples)
			return -ENOSYS;
		pdata->error = info->write_samples(info->dev, buf->dev,
						   buf->mask, pdata->data,
						   bytes_used);
	} else {
		if (!info->read_samples)
			return -ENOSYS;
		pdata->error = info->read_samples(info->dev, buf->dev,
						  buf->mask, pdata->data,
						  bytes_used);
	}

	return 0;
}

static int noos_dequeue_block(struct iio_block_pdata *pdata, bool nonblock)
{
	return pdata->error;
}

int noos_iio_register_device(const struct noos_iio_device_info *info)
{
	int ret;

	if (!info)
		return -EINVAL;

	if (noos_iio_device_count >= NOOS_IIO_MAX_DEVICES)
		return -ENOMEM;

	noos_iio_devices[noos_iio_device_count] = *info;

	/* A table-driven device gets the generic callbacks it left NULL. */
	ret = noos_iio_table_install(&noos_iio_devices[noos_iio_device_count]);
	if (ret)
		return ret;

	noos_iio_device_count++;
	return 0;
}

static bool noos_is_reg_access(const struct iio_attr *attr)
{
	return attr->type == IIO_ATTR_TYPE_DEBUG &&
	       !strcmp(iio_attr_get_name(attr), NOOS_REG_ACCESS_ATTR);
}

/* A read gives the register last written to direct_reg_access. */
static ssize_t noos_reg_access_read(struct noos_iio_device_info *info,
				    char *dst, size_t len)
{
	uint32_t value;
	int ret;

	if (!info->reg_read)
		return -EPERM;

	ret = info->reg_read(info->dev, noos_reg_address[info - noos_iio_devices],
			     &value);
	if (ret)
		return ret;

	ret = snprintf(dst, len, "0x%" PRIX32, value);
	if (ret < 0 || (size_t)ret >= len)
		return -ENOMEM;

	/* With the '\0', as the other attributes' lengths are. */
	return ret + 1;
}

/* "<reg>" selects the register a read gives, "<reg> <value>" writes it, as
 * Linux's iio_debugfs_write_reg(); each in any base strtoul takes. */
static ssize_t noos_reg_access_write(struct noos_iio_device_info *info,
				     const char *src, size_t len)
{
	unsigned long address, value;
	char buf[32], *end;
	int ret;

	if (len >= sizeof(buf))
		return -EINVAL;

	memcpy(buf, src, len);
	buf[len] = '\0';

	address = strtoul(buf, &end, 0);
	if (end == buf || address > UINT32_MAX)
		return -EINVAL;

	src = end;
	value = strtoul(src, &end, 0);
	if (end == src) {
		/* No value: only select the register. */
		while (*end == ' ' || *end == '\n')
			end++;
		if (*end)
			return -EINVAL;

		noos_reg_address[info - noos_iio_devices] = address;
		return (ssize_t)len;
	}

	while (*end == ' ' || *end == '\n')
		end++;
	if (*end || value > UINT32_MAX)
		return -EINVAL;

	if (!info->reg_write)
		return -EPERM;

	noos_reg_address[info - noos_iio_devices] = address;

	ret = info->reg_write(info->dev, address, value);
	if (ret)
		return ret;

	return (ssize_t)len;
}

static ssize_t
noos_read_attr(const struct iio_attr *attr, char *dst, size_t len)
{
	const struct iio_device *iio_dev = iio_attr_get_device(attr);
	struct noos_iio_device_info *info =
		(struct noos_iio_device_info *)iio_device_get_pdata(iio_dev);

	if (info && noos_is_reg_access(attr))
		return noos_reg_access_read(info, dst, len);

	if (!info || !info->read_attr)
		return -ENOSYS;

	return info->read_attr(info->dev, iio_dev, attr, dst, len);
}

static ssize_t
noos_write_attr(const struct iio_attr *attr, const char *src, size_t len)
{
	const struct iio_device *iio_dev = iio_attr_get_device(attr);
	struct noos_iio_device_info *info =
		(struct noos_iio_device_info *)iio_device_get_pdata(iio_dev);

	if (info && noos_is_reg_access(attr))
		return noos_reg_access_write(info, src, len);

	if (!info || !info->write_attr)
		return -ENOSYS;

	return info->write_attr(info->dev, iio_dev, attr, src, len);
}

static const struct iio_device *
noos_get_trigger(const struct iio_device *dev)
{
	const struct noos_iio_device_info *info =
		(const struct noos_iio_device_info *)iio_device_get_pdata(dev);
	const struct iio_device *trigger;

	if (!info || !info->trigger)
		return iio_ptr(-ENODEV);

	trigger = iio_context_find_device(iio_device_get_context(dev),
					  info->trigger);
	if (!trigger)
		return iio_ptr(-ENODEV);

	return trigger;
}

/*
 * The pairing is fixed at build time (info->trigger), so selecting the trigger
 * the device already has is accepted and anything else is refused. That lets
 * a client name it (iio_rwdev -t) without the port pretending to re-route it.
 */
static int noos_set_trigger(const struct iio_device *dev,
			    const struct iio_device *trigger)
{
	const struct iio_device *fixed = noos_get_trigger(dev);

	if (iio_err(fixed))
		return trigger ? iio_err(fixed) : 0;

	return trigger == fixed ? 0 : -EINVAL;
}

/*
 * A scan element's format can depend on the device's configuration (a
 * differential input is signed, an oversampled one wider). The device
 * reports its current one; clients ask for it before opening a buffer,
 * after changing that configuration. Not while a buffer is open: blocks
 * already queued were laid out with the old format.
 */
static int noos_refresh_format(const struct iio_channel *chn)
{
	const struct iio_device *dev = iio_channel_get_device(chn);
	const struct noos_iio_device_info *info = (const struct noos_iio_device_info *)
		iio_device_get_pdata(dev);
	struct iio_channel *chn_rw = (struct iio_channel *)chn;
	struct iio_data_format fmt;
	int ret;

	if (!iio_channel_is_scan_element(chn))
		return -EINVAL;

	/* A device without the hook has static formats: nothing changes. */
	if (!info || !info->get_scan_format)
		return 0;

	if (*noos_open_count(dev))
		return -EBUSY;

	fmt = chn->format;
	ret = info->get_scan_format(info->dev, chn, &fmt);
	if (ret)
		return ret;

	if (!fmt.length || fmt.length % 8 || fmt.bits > fmt.length ||
	    fmt.shift >= fmt.length)
		return -EINVAL;

	if (!fmt.repeat)
		fmt.repeat = 1;

	/* The scale and offset come from their attributes, as on the client. */
	fmt.with_scale = chn->format.with_scale;
	fmt.scale = chn->format.scale;
	fmt.offset = chn->format.offset;
	chn_rw->format = fmt;

	return 0;
}

static int noos_reg_read(const struct iio_device *dev, uint32_t address,
			 uint32_t *value)
{
	struct noos_iio_device_info *info =
		(struct noos_iio_device_info *)iio_device_get_pdata(dev);

	if (!info || !info->reg_read)
		return -ENOSYS;

	return info->reg_read(info->dev, address, value);
}

static int noos_reg_write(const struct iio_device *dev, uint32_t address,
			  uint32_t value)
{
	struct noos_iio_device_info *info =
		(struct noos_iio_device_info *)iio_device_get_pdata(dev);

	if (!info || !info->reg_write)
		return -ENOSYS;

	return info->reg_write(info->dev, address, value);
}

static struct iio_context *
noos_create_context(const struct iio_context_params *params, const char *args)
{
	struct iio_context *ctx;
	struct iio_device *iio_dev;
	unsigned int i, nb_devices = 0, nb_triggers = 0;
	char id[32];
	int ret;

	ctx = iio_context_create_from_backend(params, &iio_external_backend,
					      NOOS_BACKEND_VERSION, 1, 0, 0, "v1.0");
	if (iio_err(ctx))
		return iio_err_cast(ctx);

	for (i = 0; i < noos_iio_device_count; i++) {
		struct noos_iio_device_info *info = &noos_iio_devices[i];

		/* iio_device_is_trigger() goes by the "trigger" id prefix. */
		if (info->is_trigger)
			snprintf(id, sizeof(id), "trigger%u", nb_triggers++);
		else
			snprintf(id, sizeof(id), "iio:device%u", nb_devices++);

		iio_dev = iio_context_add_device(ctx, id, info->name, NULL);
		if (!iio_dev)
			continue;

		iio_device_set_pdata(iio_dev,
				     (struct iio_device_pdata *)info);

		if (info->add_channels) {
			ret = info->add_channels(info->dev, iio_dev);
			if (ret)
				pr_err("%s: add_channels failed: %d\n",
				       info->name, ret);
		}

		if (info->reg_read || info->reg_write) {
			noos_reg_address[i] = 0;
			ret = iio_device_add_attr(iio_dev, NOOS_REG_ACCESS_ATTR,
						  IIO_ATTR_TYPE_DEBUG);
			if (ret)
				pr_err("%s: %s failed: %d\n", info->name,
				       NOOS_REG_ACCESS_ATTR, ret);
		}

		if (iio_device_get_channels_count(iio_dev) > 0) {
			struct iio_buffer *buf;

			buf = iio_device_add_buffer(iio_dev, 0);
			if (buf) {
				unsigned int c, nb_channels;

				iio_buffer_set_direction(buf,
							info->direction ? "out" : "in");
				nb_channels = iio_device_get_channels_count(iio_dev);
				for (c = 0; c < nb_channels; c++) {
					struct iio_channel *chn =
						iio_device_get_channel(iio_dev, c);

					if (chn && iio_channel_is_scan_element(chn))
						iio_buffer_add_scan_element(buf, chn, NULL);
				}
			}
		}
	}

	return ctx;
}

static const struct iio_backend_ops noos_ops = {
	.create = noos_create_context,
	.read_attr = noos_read_attr,
	.write_attr = noos_write_attr,
	.get_trigger = noos_get_trigger,
	.set_trigger = noos_set_trigger,

	.open_buffer = noos_open_buffer,
	.close_buffer = noos_close_buffer,
	.enable_buffer = noos_enable_buffer,
	.cancel_buffer = noos_cancel_buffer,

	.readbuf = noos_readbuf,
	.writebuf = noos_writebuf,

	.create_block = noos_create_block,
	.free_block = noos_free_block,
	.enqueue_block = noos_enqueue_block,
	.dequeue_block = noos_dequeue_block,

	.reg_read = noos_reg_read,
	.reg_write = noos_reg_write,
	.refresh_format = noos_refresh_format,
};

const struct iio_backend iio_external_backend = {
	.name = "no-os",
	.api_version = IIO_BACKEND_API_V1,
	.default_timeout_ms = 0,
	.uri_prefix = "no-os:",
	.ops = &noos_ops,
};
