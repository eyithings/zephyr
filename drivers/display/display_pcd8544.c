/*
 * Copyright (c) 2025 LACOMBE Quentin <quentlace2g@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT philips_pcd8544

#include <zephyr/device.h>
#include <zephyr/drivers/mipi_dbi.h>
#include <zephyr/pm/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(display_pcd8544, CONFIG_DISPLAY_LOG_LEVEL);

#define DISPLAY_WIDTH     84
#define DISPLAY_HEIGHT    48
#define DISPLAY_PAGES     6
#define DISPLAY_PAGE_SIZE 8
#define PXL_FMT           PIXEL_FORMAT_MONO10

/* FUNCTION SET */

#define CMD_OP_FUNCSET 0x20 /* 0b00100000 */

#define CMD_VALUE_POWERDOWN BIT(2)
#define CMD_VALUE_POWERON   0

#define CMD_VALUE_VERTICAL_ADDR   BIT(1)
#define CMD_VALUE_HORIZONTAL_ADDR 0

#define CMD_VALUE_EXTD_INSTRUCTION_SET  BIT(0)
#define CMD_VALUE_BASIC_INSTRUCTION_SET 0

/* DISPLAY CONTROL */

#define CMD_OP_DISP_CTRL 0x08 /* 0b00001000 */

#define CMD_VALUE_DISPLAY_BLANK  0
#define CMD_VALUE_DISPLAY_NORMAL BIT(2)

/* SET Y ADDR */

#define CMD_OP_SETY 0x40 /* 0b01000000 */

/* SET X ADDR */

#define CMD_OP_SETX 0x80 /* 0b10000000 */

/* EXTENDED MODE */

/* Set bias */

#define CMD_EXOP_SET_BIAS 0x10 /* 0b00010000 */

/* Set VOp */

#define CMD_EXOP_SET_VOP 0x80 /* 0b10000000 */

struct pcd8544_config {
	const struct device *bus;
	struct mipi_dbi_config bus_config;

	uint8_t bias;
	uint8_t vop;
};

/**
 * Reset PCD8544 controller
 * @param dev device to reset
 */
static int pcd8544_reset(const struct device *dev)
{
	const struct pcd8544_config *config = dev->config;

	return mipi_dbi_reset(config->bus, 1);
}

/**
 * Send command to PCD8544 controller. Commands are a byte containing a mask to identify the command
 * and its value.
 */
static int pcd8544_cmd_send(const struct device *dev, uint8_t cmd, uint8_t value)
{
	const struct pcd8544_config *config = dev->config;

	return mipi_dbi_command_write(config->bus, &config->bus_config, cmd | value, NULL, 0);
}

/**
 * This function enable the extended instruction set of display controller.
 */
static int pcd8544_extended_instruction(const struct device *dev, bool enabled)
{
	if (enabled) {
		return pcd8544_cmd_send(dev, CMD_OP_FUNCSET, CMD_VALUE_EXTD_INSTRUCTION_SET);
	}

	return pcd8544_cmd_send(dev, CMD_OP_FUNCSET, CMD_VALUE_BASIC_INSTRUCTION_SET);
}

static int pcd8544_set_position(const struct device *dev, uint8_t x, uint8_t y)
{
	if (x < DISPLAY_WIDTH && y < DISPLAY_PAGES) {
		int ret;

		ret = pcd8544_cmd_send(dev, CMD_OP_SETX, 0x7F & x);
		if (ret < 0) {
			return ret;
		}

		ret = pcd8544_cmd_send(dev, CMD_OP_SETY, 0x07 & y);
		return ret;
	} else {
		return -EINVAL;
	}
}

static int pcd8544_clear(const struct device *dev)
{
	const struct pcd8544_config *config = dev->config;
	pcd8544_set_position(dev, 0, 0);
	return 0;
}

static int pcd8544_init(const struct device *dev)
{
	int ret;
	const struct pcd8544_config *config = dev->config;

	if (!device_is_ready(config->bus)) {
		return -ENODEV;
	}

	ret = pcd8544_reset(dev);
	if (ret < 0) {
		return ret;
	}

	ret = pcd8544_extended_instruction(dev, true);
	if (ret < 0) {
		return ret;
	}

	/* Set bias */
	ret = pcd8544_cmd_send(dev, CMD_EXOP_SET_BIAS, 0x07 & config->bias);
	if (ret < 0) {
		return ret;
	}

	/* Set vop */
	ret = pcd8544_cmd_send(dev, CMD_EXOP_SET_VOP, 0x7F & config->vop);
	if (ret < 0) {
		return ret;
	}

	ret = pcd8544_extended_instruction(dev, false);
	if (ret < 0) {
		return ret;
	}

	/* set to normal mode */
	ret = pcd8544_cmd_send(dev, CMD_OP_DISP_CTRL, CMD_VALUE_DISPLAY_NORMAL);
	if (ret < 0) {
		return ret;
	}

	ret = pcd8544_clear(dev);
	if (ret < 0) {
		return ret;
	}

	return 0;
}

static void pcd8544_get_capabilities(const struct device *dev, struct display_capabilities *caps)
{
	memset(caps, 0, sizeof(struct display_capabilities));

	caps->x_resolution = DISPLAY_WIDTH;
	caps->y_resolution = DISPLAY_HEIGHT;

	caps->supported_pixel_formats = PXL_FMT;
	caps->screen_info = 0;
	caps->current_pixel_format = PXL_FMT;
}

static int pcd8544_write(const struct device *dev, const uint16_t x, const uint16_t y,
			 const struct display_buffer_descriptor *desc, const void *buf)
{
	const struct pcd8544_config *config = dev->config;
	const uint8_t *pixels_buffer = buf;
	return 0;
}

static inline int pcd8544_blanking_on(const struct device *dev)
{
	return pcd8544_cmd_send(dev, CMD_OP_DISP_CTRL, CMD_VALUE_DISPLAY_BLANK);
}

static inline int pcd8544_blanking_off(const struct device *dev)
{
	return pcd8544_cmd_send(dev, CMD_OP_DISP_CTRL, CMD_VALUE_DISPLAY_NORMAL);
}

static int pcd8544_set_contrast(const struct device *dev, const uint8_t contrast)
{
	int ret;

	ret = pcd8544_extended_instruction(dev, true);
	if (ret < 0) {
		return ret;
	}

	ret = pcd8544_cmd_send(dev, CMD_EXOP_SET_VOP, contrast >> 1);
	if (ret < 0) {
		return ret;
	}

	return pcd8544_extended_instruction(dev, false);
}

static DEVICE_API(display, pcd8544_api) = {
	.write = pcd8544_write,
	.get_capabilities = pcd8544_get_capabilities,
	.blanking_on = pcd8544_blanking_on,
	.blanking_off = pcd8544_blanking_off,
	.set_contrast = pcd8544_set_contrast,
	.clear = pcd8544_clear,
};

#define PCD8544_INIT(inst)                                                                         \
	static const struct pcd8544_config pcd8544_config_##inst = {                               \
		.bus = DEVICE_DT_GET(DT_INST_PARENT(inst)),                                        \
		.bus_config =                                                                      \
			{                                                                          \
				.mode = MIPI_DBI_MODE_SPI_4WIRE,                                   \
				.config = MIPI_DBI_SPI_CONFIG_DT_INST(                             \
					inst,                                                      \
					SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_LOCK_ON, 0),    \
			},                                                                         \
		.bias = DT_INST_PROP(inst, bias),                                                  \
		.vop = DT_INST_PROP(inst, vop),                                                    \
	};                                                                                         \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(inst, &pcd8544_init, NULL, NULL,                    \
			      &pcd8544_config_##inst, POST_KERNEL, CONFIG_DISPLAY_INIT_PRIORITY,   \
			      &pcd8544_api);

DT_INST_FOREACH_STATUS_OKAY(PCD8544_INIT)
