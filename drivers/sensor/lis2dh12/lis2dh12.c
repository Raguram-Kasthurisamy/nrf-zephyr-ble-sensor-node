/*
 * Author: Raguram Kasthurisamy
 *
 * LIS2DH12 / LIS3DH accelerometer driver for the Zephyr sensor API, over SPI.
 *
 * - Probes WHO_AM_I at init and refuses to bind to the wrong chip.
 * - Configures high-resolution mode (12-bit, +/-2 g) with block data update, so the high and
 *   low bytes of a sample always come from the same conversion.
 * - Reads all six output bytes in one SPI burst using the auto-increment bit.
 * - Optional data-ready trigger on INT1: the GPIO interrupt only submits a work item; the
 *   user's handler runs in the system work queue, where it may use the SPI bus.
 */
#define DT_DRV_COMPAT rks_lis2dh12

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>
#include <zephyr/version.h>

#include <rks/lis2dh12_math.h>

LOG_MODULE_REGISTER(rks_lis2dh12, CONFIG_SENSOR_LOG_LEVEL);

#define REG_WHO_AM_I       0x0F
#define REG_CTRL_REG1      0x20
#define REG_CTRL_REG3      0x22
#define REG_CTRL_REG4      0x23
#define REG_OUT_X_L        0x28
#define WHO_AM_I_VALUE     0x33

#define SPI_READ           BIT(7)
#define SPI_AUTO_INCREMENT BIT(6)
#define CTRL1_XYZ_ENABLE   0x07
#define CTRL3_I1_ZYXDA     BIT(4)
#define CTRL4_BDU          BIT(7)
#define CTRL4_HR           BIT(3)

/* SPI mode 3 (clock idles high, sample on the rising edge), 8-bit words, MSB first. */
#define LIS2DH12_SPI_OPERATION \
	(SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_MODE_CPOL | SPI_MODE_CPHA | SPI_TRANSFER_MSB)

/* Zephyr 4.3 dropped the chip-select delay argument from SPI_DT_SPEC_INST_GET. */
#if KERNEL_VERSION_NUMBER >= ZEPHYR_VERSION(4, 3, 0)
#define LIS2DH12_SPI_SPEC(inst) SPI_DT_SPEC_INST_GET(inst, LIS2DH12_SPI_OPERATION)
#else
#define LIS2DH12_SPI_SPEC(inst) SPI_DT_SPEC_INST_GET(inst, LIS2DH12_SPI_OPERATION, 0)
#endif

struct lis2dh12_config {
	struct spi_dt_spec bus;
	struct gpio_dt_spec irq;
	uint16_t odr_hz;
};

struct lis2dh12_data {
	int16_t raw[3];
#ifdef CONFIG_RKS_LIS2DH12_TRIGGER
	const struct device *dev;
	struct gpio_callback irq_cb;
	struct k_work work;
	sensor_trigger_handler_t drdy_handler;
	const struct sensor_trigger *drdy_trigger;
#endif
};

static int reg_write(const struct device *dev, uint8_t reg, uint8_t value)
{
	const struct lis2dh12_config *cfg = dev->config;
	uint8_t tx[2] = {reg, value};
	const struct spi_buf buf = {.buf = tx, .len = sizeof(tx)};
	const struct spi_buf_set set = {.buffers = &buf, .count = 1};

	return spi_write_dt(&cfg->bus, &set);
}

static int reg_read(const struct device *dev, uint8_t reg, uint8_t *data, size_t len)
{
	const struct lis2dh12_config *cfg = dev->config;
	uint8_t cmd = (uint8_t)(reg | SPI_READ | (len > 1 ? SPI_AUTO_INCREMENT : 0));
	const struct spi_buf tx_buf = {.buf = &cmd, .len = 1};
	const struct spi_buf_set tx = {.buffers = &tx_buf, .count = 1};
	/* The first received byte is clocked in while the command goes out: skip it. */
	struct spi_buf rx_bufs[2] = {{.buf = NULL, .len = 1}, {.buf = data, .len = len}};
	const struct spi_buf_set rx = {.buffers = rx_bufs, .count = 2};

	return spi_transceive_dt(&cfg->bus, &tx, &rx);
}

static int lis2dh12_sample_fetch(const struct device *dev, enum sensor_channel chan)
{
	struct lis2dh12_data *data = dev->data;
	uint8_t buf[6];
	int ret;

	if (chan != SENSOR_CHAN_ALL && chan != SENSOR_CHAN_ACCEL_XYZ &&
	    chan != SENSOR_CHAN_ACCEL_X && chan != SENSOR_CHAN_ACCEL_Y &&
	    chan != SENSOR_CHAN_ACCEL_Z) {
		return -ENOTSUP;
	}

	ret = reg_read(dev, REG_OUT_X_L, buf, sizeof(buf));
	if (ret < 0) {
		return ret;
	}
	for (int i = 0; i < 3; i++) {
		data->raw[i] = (int16_t)sys_get_le16(&buf[2 * i]);
	}
	return 0;
}

static void raw_to_sensor_value(int16_t raw, struct sensor_value *val)
{
	sensor_ug_to_ms2(lis2dh12_raw_to_ug(raw), val);
}

static int lis2dh12_channel_get(const struct device *dev, enum sensor_channel chan,
				struct sensor_value *val)
{
	const struct lis2dh12_data *data = dev->data;

	switch (chan) {
	case SENSOR_CHAN_ACCEL_X:
		raw_to_sensor_value(data->raw[0], val);
		return 0;
	case SENSOR_CHAN_ACCEL_Y:
		raw_to_sensor_value(data->raw[1], val);
		return 0;
	case SENSOR_CHAN_ACCEL_Z:
		raw_to_sensor_value(data->raw[2], val);
		return 0;
	case SENSOR_CHAN_ACCEL_XYZ:
		for (int i = 0; i < 3; i++) {
			raw_to_sensor_value(data->raw[i], &val[i]);
		}
		return 0;
	default:
		return -ENOTSUP;
	}
}

#ifdef CONFIG_RKS_LIS2DH12_TRIGGER
static void lis2dh12_work_handler(struct k_work *work)
{
	struct lis2dh12_data *data = CONTAINER_OF(work, struct lis2dh12_data, work);

	if (data->drdy_handler != NULL) {
		data->drdy_handler(data->dev, data->drdy_trigger);
	}
}

static void lis2dh12_gpio_callback(const struct device *port, struct gpio_callback *cb,
				   gpio_port_pins_t pins)
{
	struct lis2dh12_data *data = CONTAINER_OF(cb, struct lis2dh12_data, irq_cb);

	ARG_UNUSED(port);
	ARG_UNUSED(pins);
	(void)k_work_submit(&data->work); /* SPI cannot be used from interrupt context */
}

static int lis2dh12_trigger_set(const struct device *dev, const struct sensor_trigger *trig,
				sensor_trigger_handler_t handler)
{
	const struct lis2dh12_config *cfg = dev->config;
	struct lis2dh12_data *data = dev->data;
	uint8_t discard[6];
	int ret;

	if (trig->type != SENSOR_TRIG_DATA_READY) {
		return -ENOTSUP;
	}
	if (cfg->irq.port == NULL) {
		return -ENOTSUP; /* no irq-gpios in devicetree */
	}

	data->drdy_handler = handler;
	data->drdy_trigger = trig;

	ret = reg_write(dev, REG_CTRL_REG3, handler != NULL ? CTRL3_I1_ZYXDA : 0);
	if (ret < 0) {
		return ret;
	}
	if (handler != NULL) {
		/* Reading the outputs clears a data-ready level that may already be pending. */
		(void)reg_read(dev, REG_OUT_X_L, discard, sizeof(discard));
	}
	return gpio_pin_interrupt_configure_dt(&cfg->irq, handler != NULL ? GPIO_INT_EDGE_TO_ACTIVE
									   : GPIO_INT_DISABLE);
}

static int lis2dh12_init_interrupt(const struct device *dev)
{
	const struct lis2dh12_config *cfg = dev->config;
	struct lis2dh12_data *data = dev->data;
	int ret;

	if (cfg->irq.port == NULL) {
		return 0;
	}
	if (!gpio_is_ready_dt(&cfg->irq)) {
		LOG_ERR("interrupt GPIO not ready");
		return -ENODEV;
	}
	data->dev = dev;
	k_work_init(&data->work, lis2dh12_work_handler);
	ret = gpio_pin_configure_dt(&cfg->irq, GPIO_INPUT);
	if (ret < 0) {
		return ret;
	}
	gpio_init_callback(&data->irq_cb, lis2dh12_gpio_callback, BIT(cfg->irq.pin));
	return gpio_add_callback(cfg->irq.port, &data->irq_cb);
}
#endif /* CONFIG_RKS_LIS2DH12_TRIGGER */

static int lis2dh12_init(const struct device *dev)
{
	const struct lis2dh12_config *cfg = dev->config;
	uint8_t id = 0;
	int ret;

	if (!spi_is_ready_dt(&cfg->bus)) {
		LOG_ERR("SPI bus not ready");
		return -ENODEV;
	}
	ret = reg_read(dev, REG_WHO_AM_I, &id, 1);
	if (ret < 0) {
		LOG_ERR("WHO_AM_I read failed (%d)", ret);
		return ret;
	}
	if (id != WHO_AM_I_VALUE) {
		LOG_ERR("unexpected WHO_AM_I 0x%02x (expected 0x%02x)", id, WHO_AM_I_VALUE);
		return -EIO;
	}
	ret = reg_write(dev, REG_CTRL_REG4, CTRL4_BDU | CTRL4_HR); /* +/-2 g, 12-bit */
	if (ret < 0) {
		return ret;
	}
	ret = reg_write(dev, REG_CTRL_REG1,
			(uint8_t)((lis2dh12_odr_bits(cfg->odr_hz) << 4) | CTRL1_XYZ_ENABLE));
	if (ret < 0) {
		return ret;
	}
#ifdef CONFIG_RKS_LIS2DH12_TRIGGER
	ret = lis2dh12_init_interrupt(dev);
	if (ret < 0) {
		return ret;
	}
#endif
	LOG_INF("LIS2DH12 ready, %u Hz", cfg->odr_hz);
	return 0;
}

static const struct sensor_driver_api lis2dh12_api = {
	.sample_fetch = lis2dh12_sample_fetch,
	.channel_get = lis2dh12_channel_get,
#ifdef CONFIG_RKS_LIS2DH12_TRIGGER
	.trigger_set = lis2dh12_trigger_set,
#endif
};

#define LIS2DH12_DEFINE(inst)                                                                  \
	static struct lis2dh12_data lis2dh12_data_##inst;                                      \
	static const struct lis2dh12_config lis2dh12_config_##inst = {                         \
		.bus = LIS2DH12_SPI_SPEC(inst),                                                \
		.irq = GPIO_DT_SPEC_INST_GET_OR(inst, irq_gpios, {0}),                         \
		.odr_hz = DT_INST_PROP(inst, odr_hz),                                          \
	};                                                                                     \
	SENSOR_DEVICE_DT_INST_DEFINE(inst, lis2dh12_init, NULL, &lis2dh12_data_##inst,         \
				     &lis2dh12_config_##inst, POST_KERNEL,                     \
				     CONFIG_SENSOR_INIT_PRIORITY, &lis2dh12_api);

DT_INST_FOREACH_STATUS_OKAY(LIS2DH12_DEFINE)
