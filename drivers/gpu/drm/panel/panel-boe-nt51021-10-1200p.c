// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018 MediaTek Inc.
 * Author: Jitao Shi <jitao.shi@mediatek.com>
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/regulator/consumer.h>

#include <drm/drm_connector.h>
#include <drm/drm_crtc.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

#include <video/mipi_display.h>

struct panel_desc {
	const struct drm_display_mode *modes;
	unsigned int bpc;

	/**
	 * @width_mm: width of the panel's active display area
	 * @height_mm: height of the panel's active display area
	 */
	struct {
		unsigned int width_mm;
		unsigned int height_mm;
	} size;

	unsigned long mode_flags;
	enum mipi_dsi_pixel_format format;
	const struct panel_init_cmd *init_cmds;
	unsigned int lanes;
	bool discharge_on_disable;
};

struct boe_panel {
	struct drm_panel base;
	struct mipi_dsi_device *dsi;

	const struct panel_desc *desc;

	enum drm_panel_orientation orientation;
	//struct regulator *pp1800;
	struct regulator *vled;
	struct regulator *iovcc;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *backlight_gpio;
	bool prepared;
};

enum dsi_cmd_type {
	INIT_DCS_CMD,
	DELAY_CMD,
};

struct panel_init_cmd {
	enum dsi_cmd_type type;
	size_t len;
	const char *data;
};

#define _INIT_DCS_CMD(...) { \
	.type = INIT_DCS_CMD, \
	.len = sizeof((char[]){__VA_ARGS__}), \
	.data = (char[]){__VA_ARGS__} }

#define _INIT_DELAY_CMD(...) { \
	.type = DELAY_CMD,\
	.len = sizeof((char[]){__VA_ARGS__}), \
	.data = (char[]){__VA_ARGS__} }

static const struct panel_init_cmd boe_init_cmd[] = {
	_INIT_DELAY_CMD(24),
	_INIT_DCS_CMD(0xB0, 0x05),
	_INIT_DCS_CMD(0xB1, 0xE5),
	_INIT_DCS_CMD(0xB3, 0x52),
	_INIT_DCS_CMD(0xB0, 0x00),
	_INIT_DCS_CMD(0xB3, 0x88),
	_INIT_DCS_CMD(0xB0, 0x04),
	_INIT_DCS_CMD(0xB8, 0x00),
	_INIT_DCS_CMD(0xB0, 0x00),
	_INIT_DCS_CMD(0xB6, 0x03),
	_INIT_DCS_CMD(0xBA, 0x8B),
	_INIT_DCS_CMD(0xBF, 0x1A),
	_INIT_DCS_CMD(0xC0, 0x0F),
	_INIT_DCS_CMD(0xC2, 0x0C),
	_INIT_DCS_CMD(0xC3, 0x02),
	_INIT_DCS_CMD(0xC4, 0x0C),
	_INIT_DCS_CMD(0xC5, 0x02),
	_INIT_DCS_CMD(0xB0, 0x01),
	_INIT_DCS_CMD(0xE0, 0x26),
	_INIT_DCS_CMD(0xE1, 0x26),
	_INIT_DCS_CMD(0xDC, 0x00),
	_INIT_DCS_CMD(0xDD, 0x00),
	_INIT_DCS_CMD(0xCC, 0x26),
	_INIT_DCS_CMD(0xCD, 0x26),
	_INIT_DCS_CMD(0xC8, 0x00),
	_INIT_DCS_CMD(0xC9, 0x00),
	_INIT_DCS_CMD(0xD2, 0x03),
	_INIT_DCS_CMD(0xD3, 0x03),
	_INIT_DCS_CMD(0xE6, 0x04),
	_INIT_DCS_CMD(0xE7, 0x04),
	_INIT_DCS_CMD(0xC4, 0x09),
	_INIT_DCS_CMD(0xC5, 0x09),
	_INIT_DCS_CMD(0xD8, 0x0A),
	_INIT_DCS_CMD(0xD9, 0x0A),
	_INIT_DCS_CMD(0xC2, 0x0B),
	_INIT_DCS_CMD(0xC3, 0x0B),
	_INIT_DCS_CMD(0xD6, 0x0C),
	_INIT_DCS_CMD(0xD7, 0x0C),
	_INIT_DCS_CMD(0xC0, 0x05),
	_INIT_DCS_CMD(0xC1, 0x05),
	_INIT_DCS_CMD(0xD4, 0x06),
	_INIT_DCS_CMD(0xD5, 0x06),
	_INIT_DCS_CMD(0xCA, 0x07),
	_INIT_DCS_CMD(0xCB, 0x07),
	_INIT_DCS_CMD(0xDE, 0x08),
	_INIT_DCS_CMD(0xDF, 0x08),
	_INIT_DCS_CMD(0xB0, 0x02),
	_INIT_DCS_CMD(0xC0, 0x00),
	_INIT_DCS_CMD(0xC1, 0x0D),
	_INIT_DCS_CMD(0xC2, 0x17),
	_INIT_DCS_CMD(0xC3, 0x26),
	_INIT_DCS_CMD(0xC4, 0x31),
	_INIT_DCS_CMD(0xC5, 0x1C),
	_INIT_DCS_CMD(0xC6, 0x2C),
	_INIT_DCS_CMD(0xC7, 0x33),
	_INIT_DCS_CMD(0xC8, 0x31),
	_INIT_DCS_CMD(0xC9, 0x37),
	_INIT_DCS_CMD(0xCA, 0x37),
	_INIT_DCS_CMD(0xCB, 0x37),
	_INIT_DCS_CMD(0xCC, 0x39),
	_INIT_DCS_CMD(0xCD, 0x2E),
	_INIT_DCS_CMD(0xCE, 0x2F),
	_INIT_DCS_CMD(0xCF, 0x2F),
	_INIT_DCS_CMD(0xD0, 0x07),
	_INIT_DCS_CMD(0xD2, 0x00),
	_INIT_DCS_CMD(0xD3, 0x0D),
	_INIT_DCS_CMD(0xD4, 0x17),
	_INIT_DCS_CMD(0xD5, 0x26),
	_INIT_DCS_CMD(0xD6, 0x31),
	_INIT_DCS_CMD(0xD7, 0x3F),
	_INIT_DCS_CMD(0xD8, 0x3F),
	_INIT_DCS_CMD(0xD9, 0x3F),
	_INIT_DCS_CMD(0xDA, 0x3F),
	_INIT_DCS_CMD(0xDB, 0x37),
	_INIT_DCS_CMD(0xDC, 0x37),
	_INIT_DCS_CMD(0xDD, 0x37),
	_INIT_DCS_CMD(0xDE, 0x39),
	_INIT_DCS_CMD(0xDF, 0x2E),
	_INIT_DCS_CMD(0xE0, 0x2F),
	_INIT_DCS_CMD(0xE1, 0x2F),
	_INIT_DCS_CMD(0xE2, 0x07),
	_INIT_DCS_CMD(0xB0, 0x03),
	_INIT_DCS_CMD(0xC8, 0x0B),
	_INIT_DCS_CMD(0xC9, 0x07),
	_INIT_DCS_CMD(0xC3, 0x00),
	_INIT_DCS_CMD(0xE7, 0x00),
	_INIT_DCS_CMD(0xC5, 0x2A),
	_INIT_DCS_CMD(0xDE, 0x2A),
	_INIT_DCS_CMD(0xCA, 0x43),
	_INIT_DCS_CMD(0xC9, 0x07),
	_INIT_DCS_CMD(0xE4, 0xC0),
	_INIT_DCS_CMD(0xE5, 0x0D),
	_INIT_DCS_CMD(0xCB, 0x00),
	_INIT_DCS_CMD(0xB0, 0x06),
	_INIT_DCS_CMD(0xB8, 0xA5),
	_INIT_DCS_CMD(0xC0, 0xA5),
	_INIT_DCS_CMD(0xC7, 0x0F),
	_INIT_DCS_CMD(0xD5, 0x32),
	_INIT_DCS_CMD(0xB8, 0x00),
	_INIT_DCS_CMD(0xC0, 0x00),
	_INIT_DCS_CMD(0xBC, 0x00),
	_INIT_DCS_CMD(0xB0, 0x07),
	_INIT_DCS_CMD(0xB1, 0x00),
	_INIT_DCS_CMD(0xB2, 0x02),
	_INIT_DCS_CMD(0xB3, 0x0F),
	_INIT_DCS_CMD(0xB4, 0x25),
	_INIT_DCS_CMD(0xB5, 0x39),
	_INIT_DCS_CMD(0xB6, 0x4E),
	_INIT_DCS_CMD(0xB7, 0x72),
	_INIT_DCS_CMD(0xB8, 0x97),
	_INIT_DCS_CMD(0xB9, 0xDC),
	_INIT_DCS_CMD(0xBA, 0x22),
	_INIT_DCS_CMD(0xBB, 0xA4),
	_INIT_DCS_CMD(0xBC, 0x2B),
	_INIT_DCS_CMD(0xBD, 0x2F),
	_INIT_DCS_CMD(0xBE, 0xA9),
	_INIT_DCS_CMD(0xBF, 0x25),
	_INIT_DCS_CMD(0xC0, 0x61),
	_INIT_DCS_CMD(0xC1, 0x97),
	_INIT_DCS_CMD(0xC2, 0xB2),
	_INIT_DCS_CMD(0xC3, 0xCD),
	_INIT_DCS_CMD(0xC4, 0xD9),
	_INIT_DCS_CMD(0xC5, 0xE7),
	_INIT_DCS_CMD(0xC6, 0xF4),
	_INIT_DCS_CMD(0xC7, 0xFA),
	_INIT_DCS_CMD(0xC8, 0xFC),
	_INIT_DCS_CMD(0xC9, 0x00),
	_INIT_DCS_CMD(0xCA, 0x00),
	_INIT_DCS_CMD(0xCB, 0x16),
	_INIT_DCS_CMD(0xCC, 0xAF),
	_INIT_DCS_CMD(0xCD, 0xFF),
	_INIT_DCS_CMD(0xCE, 0xFF),
	_INIT_DCS_CMD(0xB0, 0x08),
	_INIT_DCS_CMD(0xB1, 0x04),
	_INIT_DCS_CMD(0xB2, 0x05),
	_INIT_DCS_CMD(0xB3, 0x11),
	_INIT_DCS_CMD(0xB4, 0x24),
	_INIT_DCS_CMD(0xB5, 0x39),
	_INIT_DCS_CMD(0xB6, 0x4F),
	_INIT_DCS_CMD(0xB7, 0x72),
	_INIT_DCS_CMD(0xB8, 0x98),
	_INIT_DCS_CMD(0xB9, 0xDC),
	_INIT_DCS_CMD(0xBA, 0x23),
	_INIT_DCS_CMD(0xBB, 0xA6),
	_INIT_DCS_CMD(0xBC, 0x2C),
	_INIT_DCS_CMD(0xBD, 0x30),
	_INIT_DCS_CMD(0xBE, 0xAA),
	_INIT_DCS_CMD(0xBF, 0x26),
	_INIT_DCS_CMD(0xC0, 0x62),
	_INIT_DCS_CMD(0xC1, 0x9B),
	_INIT_DCS_CMD(0xC2, 0xB5),
	_INIT_DCS_CMD(0xC3, 0xCF),
	_INIT_DCS_CMD(0xC4, 0xDB),
	_INIT_DCS_CMD(0xC5, 0xE8),
	_INIT_DCS_CMD(0xC6, 0xF5),
	_INIT_DCS_CMD(0xC7, 0xFA),
	_INIT_DCS_CMD(0xC8, 0xFC),
	_INIT_DCS_CMD(0xC9, 0x00),
	_INIT_DCS_CMD(0xCA, 0x00),
	_INIT_DCS_CMD(0xCB, 0x16),
	_INIT_DCS_CMD(0xCC, 0xAF),
	_INIT_DCS_CMD(0xCD, 0xFF),
	_INIT_DCS_CMD(0xCE, 0xFF),
	_INIT_DCS_CMD(0xB0, 0x09),
	_INIT_DCS_CMD(0xB1, 0x04),
	_INIT_DCS_CMD(0xB2, 0x02),
	_INIT_DCS_CMD(0xB3, 0x16),
	_INIT_DCS_CMD(0xB4, 0x24),
	_INIT_DCS_CMD(0xB5, 0x3B),
	_INIT_DCS_CMD(0xB6, 0x4F),
	_INIT_DCS_CMD(0xB7, 0x73),
	_INIT_DCS_CMD(0xB8, 0x99),
	_INIT_DCS_CMD(0xB9, 0xE0),
	_INIT_DCS_CMD(0xBA, 0x26),
	_INIT_DCS_CMD(0xBB, 0xAD),
	_INIT_DCS_CMD(0xBC, 0x36),
	_INIT_DCS_CMD(0xBD, 0x3A),
	_INIT_DCS_CMD(0xBE, 0xAE),
	_INIT_DCS_CMD(0xBF, 0x2A),
	_INIT_DCS_CMD(0xC0, 0x66),
	_INIT_DCS_CMD(0xC1, 0x9E),
	_INIT_DCS_CMD(0xC2, 0xB8),
	_INIT_DCS_CMD(0xC3, 0xD1),
	_INIT_DCS_CMD(0xC4, 0xDD),
	_INIT_DCS_CMD(0xC5, 0xE9),
	_INIT_DCS_CMD(0xC6, 0xF6),
	_INIT_DCS_CMD(0xC7, 0xFA),
	_INIT_DCS_CMD(0xC8, 0xFC),
	_INIT_DCS_CMD(0xC9, 0x00),
	_INIT_DCS_CMD(0xCA, 0x00),
	_INIT_DCS_CMD(0xCB, 0x16),
	_INIT_DCS_CMD(0xCC, 0xAF),
	_INIT_DCS_CMD(0xCD, 0xFF),
	_INIT_DCS_CMD(0xCE, 0xFF),
	_INIT_DCS_CMD(0xB0, 0x0A),
	_INIT_DCS_CMD(0xB1, 0x00),
	_INIT_DCS_CMD(0xB2, 0x02),
	_INIT_DCS_CMD(0xB3, 0x0F),
	_INIT_DCS_CMD(0xB4, 0x25),
	_INIT_DCS_CMD(0xB5, 0x39),
	_INIT_DCS_CMD(0xB6, 0x4E),
	_INIT_DCS_CMD(0xB7, 0x72),
	_INIT_DCS_CMD(0xB8, 0x97),
	_INIT_DCS_CMD(0xB9, 0xDC),
	_INIT_DCS_CMD(0xBA, 0x22),
	_INIT_DCS_CMD(0xBB, 0xA4),
	_INIT_DCS_CMD(0xBC, 0x2B),
	_INIT_DCS_CMD(0xBD, 0x2F),
	_INIT_DCS_CMD(0xBE, 0xA9),
	_INIT_DCS_CMD(0xBF, 0x25),
	_INIT_DCS_CMD(0xC0, 0x61),
	_INIT_DCS_CMD(0xC1, 0x97),
	_INIT_DCS_CMD(0xC2, 0xB2),
	_INIT_DCS_CMD(0xC3, 0xCD),
	_INIT_DCS_CMD(0xC4, 0xD9),
	_INIT_DCS_CMD(0xC5, 0xE7),
	_INIT_DCS_CMD(0xC6, 0xF4),
	_INIT_DCS_CMD(0xC7, 0xFA),
	_INIT_DCS_CMD(0xC8, 0xFC),
	_INIT_DCS_CMD(0xC9, 0x00),
	_INIT_DCS_CMD(0xCA, 0x00),
	_INIT_DCS_CMD(0xCB, 0x16),
	_INIT_DCS_CMD(0xCC, 0xAF),
	_INIT_DCS_CMD(0xCD, 0xFF),
	_INIT_DCS_CMD(0xCE, 0xFF),
	_INIT_DCS_CMD(0xB0, 0x0B),
	_INIT_DCS_CMD(0xB1, 0x04),
	_INIT_DCS_CMD(0xB2, 0x05),
	_INIT_DCS_CMD(0xB3, 0x11),
	_INIT_DCS_CMD(0xB4, 0x24),
	_INIT_DCS_CMD(0xB5, 0x39),
	_INIT_DCS_CMD(0xB6, 0x4F),
	_INIT_DCS_CMD(0xB7, 0x72),
	_INIT_DCS_CMD(0xB8, 0x98),
	_INIT_DCS_CMD(0xB9, 0xDC),
	_INIT_DCS_CMD(0xBA, 0x23),
	_INIT_DCS_CMD(0xBB, 0xA6),
	_INIT_DCS_CMD(0xBC, 0x2C),
	_INIT_DCS_CMD(0xBD, 0x30),
	_INIT_DCS_CMD(0xBE, 0xAA),
	_INIT_DCS_CMD(0xBF, 0x26),
	_INIT_DCS_CMD(0xC0, 0x62),
	_INIT_DCS_CMD(0xC1, 0x9B),
	_INIT_DCS_CMD(0xC2, 0xB5),
	_INIT_DCS_CMD(0xC3, 0xCF),
	_INIT_DCS_CMD(0xC4, 0xDB),
	_INIT_DCS_CMD(0xC5, 0xE8),
	_INIT_DCS_CMD(0xC6, 0xF5),
	_INIT_DCS_CMD(0xC7, 0xFA),
	_INIT_DCS_CMD(0xC8, 0xFC),
	_INIT_DCS_CMD(0xC9, 0x00),
	_INIT_DCS_CMD(0xCA, 0x00),
	_INIT_DCS_CMD(0xCB, 0x16),
	_INIT_DCS_CMD(0xCC, 0xAF),
	_INIT_DCS_CMD(0xCD, 0xFF),
	_INIT_DCS_CMD(0xCE, 0xFF),
	_INIT_DCS_CMD(0xB0, 0x0C),
	_INIT_DCS_CMD(0xB1, 0x04),
	_INIT_DCS_CMD(0xB2, 0x02),
	_INIT_DCS_CMD(0xB3, 0x16),
	_INIT_DCS_CMD(0xB4, 0x24),
	_INIT_DCS_CMD(0xB5, 0x3B),
	_INIT_DCS_CMD(0xB6, 0x4F),
	_INIT_DCS_CMD(0xB7, 0x73),
	_INIT_DCS_CMD(0xB8, 0x99),
	_INIT_DCS_CMD(0xB9, 0xE0),
	_INIT_DCS_CMD(0xBA, 0x26),
	_INIT_DCS_CMD(0xBB, 0xAD),
	_INIT_DCS_CMD(0xBC, 0x36),
	_INIT_DCS_CMD(0xBD, 0x3A),
	_INIT_DCS_CMD(0xBE, 0xAE),
	_INIT_DCS_CMD(0xBF, 0x2A),
	_INIT_DCS_CMD(0xC0, 0x66),
	_INIT_DCS_CMD(0xC1, 0x9E),
	_INIT_DCS_CMD(0xC2, 0xB8),
	_INIT_DCS_CMD(0xC3, 0xD1),
	_INIT_DCS_CMD(0xC4, 0xDD),
	_INIT_DCS_CMD(0xC5, 0xE9),
	_INIT_DCS_CMD(0xC6, 0xF6),
	_INIT_DCS_CMD(0xC7, 0xFA),
	_INIT_DCS_CMD(0xC8, 0xFC),
	_INIT_DCS_CMD(0xC9, 0x00),
	_INIT_DCS_CMD(0xCA, 0x00),
	_INIT_DCS_CMD(0xCB, 0x16),
	_INIT_DCS_CMD(0xCC, 0xAF),
	_INIT_DCS_CMD(0xCD, 0xFF),
	_INIT_DCS_CMD(0xCE, 0xFF),
	_INIT_DCS_CMD(0xB0, 0x00),
	_INIT_DCS_CMD(0xB3, 0x08),
	_INIT_DCS_CMD(0xB0, 0x04),
	_INIT_DCS_CMD(0xB8, 0x68),
	_INIT_DELAY_CMD(150),
	{},
};

static inline struct boe_panel *to_boe_panel(struct drm_panel *panel)
{
	return container_of(panel, struct boe_panel, base);
}

static int boe_panel_init_dcs_cmd(struct boe_panel *boe)
{
	struct mipi_dsi_device *dsi = boe->dsi;
	struct drm_panel *panel = &boe->base;
	int i, err = 0;

	if (boe->desc->init_cmds) {
		const struct panel_init_cmd *init_cmds = boe->desc->init_cmds;

		for (i = 0; init_cmds[i].len != 0; i++) {
			const struct panel_init_cmd *cmd = &init_cmds[i];

			switch (cmd->type) {
			case DELAY_CMD:
				msleep(cmd->data[0]);
				err = 0;
				break;

			case INIT_DCS_CMD:
				err = mipi_dsi_dcs_write(dsi, cmd->data[0],
							 cmd->len <= 1 ? NULL :
							 &cmd->data[1],
							 cmd->len - 1);
				break;

			default:
				err = -EINVAL;
			}

			if (err < 0) {
				dev_err(panel->dev,
					"failed to write command %u\n", i);
				return err;
			}
		}
	}
	return 0;
}

static int boe_panel_enter_sleep_mode(struct boe_panel *boe)
{
	struct mipi_dsi_device *dsi = boe->dsi;
	int ret;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_set_display_off(dsi);
	if (ret < 0)
		return ret;

	ret = mipi_dsi_dcs_enter_sleep_mode(dsi);
	if (ret < 0)
		return ret;

	return 0;
}

static int boe_panel_unprepare(struct drm_panel *panel)
{
	struct boe_panel *boe = to_boe_panel(panel);
	int ret;

	if (!boe->prepared)
		return 0;

	ret = boe_panel_enter_sleep_mode(boe);
	if (ret < 0) {
		dev_err(panel->dev, "failed to set panel off: %d\n", ret);
		return ret;
	}

	msleep(150);

	if (boe->desc->discharge_on_disable) {
		regulator_disable(boe->vled);
		regulator_disable(boe->iovcc);
		usleep_range(5000, 7000);
		gpiod_set_value(boe->reset_gpio, 0);
		gpiod_set_value(boe->backlight_gpio, 0);
		usleep_range(5000, 7000);
	} else {
		gpiod_set_value(boe->reset_gpio, 0);
		usleep_range(500, 1000);
		regulator_disable(boe->vled);
		regulator_disable(boe->iovcc);
		usleep_range(5000, 7000);
		gpiod_set_value(boe->backlight_gpio, 0);
		usleep_range(500, 1000);
	}

	boe->prepared = false;

	return 0;
}

static int boe_panel_prepare(struct drm_panel *panel)
{
	struct boe_panel *boe = to_boe_panel(panel);
	//struct mipi_dsi_device *dsi = boe->dsi;
	int ret;

	if (boe->prepared)
		return 0;

	gpiod_set_value(boe->reset_gpio, 0);
	usleep_range(1000, 1500);

	gpiod_set_value(boe->backlight_gpio, 1);
	usleep_range(3000, 5000);

	ret = regulator_enable(boe->iovcc);
	if (ret < 0)
		gpiod_set_value(boe->backlight_gpio, 0);
	ret = regulator_enable(boe->vled);
	if (ret < 0)
		goto poweroffiovcc;

	usleep_range(5000, 10000);

	gpiod_set_value(boe->reset_gpio, 1);
	usleep_range(1000, 2000);
	gpiod_set_value(boe->reset_gpio, 0);
	usleep_range(1000, 2000);
	gpiod_set_value(boe->reset_gpio, 1);
	usleep_range(6000, 10000);

	ret = boe_panel_init_dcs_cmd(boe);
	if (ret < 0) {
		dev_err(panel->dev, "failed to init panel: %d\n", ret);
		goto poweroff;
	}

	/*ret = mipi_dsi_dcs_set_display_brightness(dsi, 0x00ff);
	if (ret < 0) {
		dev_err(panel->dev "Failed to set display brightness: %d\n", ret);
		return ret;
	}
	usleep_range(5000, 6000);*/

	boe->prepared = true;

	return 0;

poweroff:
	regulator_disable(boe->vled);
poweroffiovcc:
	regulator_disable(boe->iovcc);
	usleep_range(5000, 7000);
	gpiod_set_value(boe->backlight_gpio, 0);
	gpiod_set_value(boe->reset_gpio, 0);

	return ret;
}

static int boe_panel_enable(struct drm_panel *panel)
{
	msleep(130);
	return 0;
}

static const struct drm_display_mode boe_nt51021_10_default_mode = {
	.clock = 160392,
	.hdisplay = 1200,
	.hsync_start = 1200 + 64,
	.hsync_end = 1200 + 64 + 4,
	.htotal = 1200 + 64 + 4 + 36,
	.vdisplay = 1920,
	.vsync_start = 1920 + 104,
	.vsync_end = 1920 + 104 + 2,
	.vtotal = 1920 + 104 + 2 + 24,
	.type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
};

static const struct panel_desc boe_nt51021_10_desc = {
	.modes = &boe_nt51021_10_default_mode,
	.bpc = 8,
	.size = {
		.width_mm = 135,
		.height_mm = 217,
	},
	.lanes = 4,
	.format = MIPI_DSI_FMT_RGB888,
	.mode_flags = MIPI_DSI_MODE_VIDEO |
			MIPI_DSI_MODE_VIDEO_BURST |
			//MIPI_DSI_MODE_VIDEO_SYNC_PULSE |
			MIPI_DSI_MODE_VIDEO_HSE |
			MIPI_DSI_MODE_NO_EOT_PACKET |
            MIPI_DSI_MODE_LPM,
			//MIPI_DSI_CLOCK_NON_CONTINUOUS | #pixels & stripes
	.init_cmds = boe_init_cmd,
	.discharge_on_disable = true,
};

static int boe_panel_get_modes(struct drm_panel *panel,
			       struct drm_connector *connector)
{
	struct boe_panel *boe = to_boe_panel(panel);
	const struct drm_display_mode *m = boe->desc->modes;
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, m);
	if (!mode) {
		dev_err(panel->dev, "failed to add mode %ux%u@%u\n",
			m->hdisplay, m->vdisplay, drm_mode_vrefresh(m));
		return -ENOMEM;
	}

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_set_name(mode);
	drm_mode_probed_add(connector, mode);

	connector->display_info.width_mm = boe->desc->size.width_mm;
	connector->display_info.height_mm = boe->desc->size.height_mm;
	connector->display_info.bpc = boe->desc->bpc;
	drm_connector_set_panel_orientation(connector, boe->orientation);

	return 1;
}

static const struct drm_panel_funcs boe_panel_funcs = {
	.unprepare = boe_panel_unprepare,
	.prepare = boe_panel_prepare,
	.enable = boe_panel_enable,
	.get_modes = boe_panel_get_modes,
};

static int boe_panel_bl_update_status(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	struct boe_panel *boe = mipi_dsi_get_drvdata(dsi);
	u16 brightness = backlight_get_brightness(bl);
	int ret;

	gpiod_set_value_cansleep(boe->backlight_gpio, !!brightness);

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_set_display_brightness(dsi, brightness);
	if (ret < 0)
		return ret;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	return 0;
}

static int boe_panel_bl_get_brightness(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	u16 brightness;
	int ret;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_get_display_brightness(dsi, &brightness);
	if (ret < 0)
		return ret;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	return brightness & 0xff;
}

static const struct backlight_ops boe_bl_ops = {
	.update_status = boe_panel_bl_update_status,
	.get_brightness = boe_panel_bl_get_brightness,
};

static struct backlight_device *
boe_create_backlight(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	const struct backlight_properties props = {
		.type = BACKLIGHT_RAW,
		.brightness = 255,
		.max_brightness = 255,
	};

	return devm_backlight_device_register(dev, dev_name(dev), dev, dsi,
					      &boe_bl_ops, &props);
}

static int boe_panel_add(struct boe_panel *boe)
{
	struct device *dev = &boe->dsi->dev;
	struct mipi_dsi_device *dsi = boe->dsi;
	int err;

	boe->iovcc = devm_regulator_get(dev, "iovcc");
	if (IS_ERR(boe->iovcc))
		return PTR_ERR(boe->iovcc);

	boe->vled = devm_regulator_get(dev, "vled");
	if (IS_ERR(boe->vled))
		return PTR_ERR(boe->vled);

	boe->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(boe->reset_gpio)) {
		dev_err(dev, "cannot get reset-gpios %ld\n",
			PTR_ERR(boe->reset_gpio));
		return PTR_ERR(boe->reset_gpio);
	}

	boe->backlight_gpio = devm_gpiod_get(dev, "backlight", GPIOD_OUT_LOW);
	if (IS_ERR(boe->backlight_gpio)) {
		dev_err(dev, "cannot get backlight-gpios %ld\n",
			PTR_ERR(boe->backlight_gpio));
		return PTR_ERR(boe->backlight_gpio);
	}

	gpiod_set_value(boe->reset_gpio, 0);
	gpiod_set_value(boe->backlight_gpio, 0);

	drm_panel_init(&boe->base, dev, &boe_panel_funcs,
		       DRM_MODE_CONNECTOR_DSI);
	err = of_drm_get_panel_orientation(dev->of_node, &boe->orientation);
	if (err < 0) {
		dev_err(dev, "%pOF: failed to get orientation %d\n", dev->of_node, err);
		return err;
	}

	boe->base.backlight = boe_create_backlight(dsi);
	if (IS_ERR(boe->base.backlight))
		return dev_err_probe(dev, PTR_ERR(boe->base.backlight),
				     "Failed to create backlight\n");

	err = drm_panel_of_backlight(&boe->base);
	if (err)
		return err;

	boe->base.funcs = &boe_panel_funcs;
	boe->base.dev = &boe->dsi->dev;

	drm_panel_add(&boe->base);

	return 0;
}

static int boe_panel_probe(struct mipi_dsi_device *dsi)
{
	struct boe_panel *boe;
	int ret;
	const struct panel_desc *desc;

	boe = devm_kzalloc(&dsi->dev, sizeof(*boe), GFP_KERNEL);
	if (!boe)
		return -ENOMEM;

	desc = of_device_get_match_data(&dsi->dev);
	dsi->lanes = desc->lanes;
	dsi->format = desc->format;
	dsi->mode_flags = desc->mode_flags;
	boe->desc = desc;
	boe->dsi = dsi;
	ret = boe_panel_add(boe);
	if (ret < 0)
		return ret;

	mipi_dsi_set_drvdata(dsi, boe);

	ret = mipi_dsi_attach(dsi);
	if (ret)
		drm_panel_remove(&boe->base);

	return ret;
}

static void boe_panel_shutdown(struct mipi_dsi_device *dsi)
{
	struct boe_panel *boe = mipi_dsi_get_drvdata(dsi);

	drm_panel_disable(&boe->base);
	drm_panel_unprepare(&boe->base);
}

static int boe_panel_remove(struct mipi_dsi_device *dsi)
{
	struct boe_panel *boe = mipi_dsi_get_drvdata(dsi);
	int ret;

	boe_panel_shutdown(dsi);

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "failed to detach from DSI host: %d\n", ret);

	if (boe->base.dev)
		drm_panel_remove(&boe->base);

	return 0;
}

static const struct of_device_id boe_of_match[] = {
	{ .compatible = "boe,nt51021-10-1200p",
	  .data = &boe_nt51021_10_desc
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, boe_of_match);

static struct mipi_dsi_driver boe_panel_driver = {
	.driver = {
		.name = "panel-boe-nt51021-10-1200p",
		.of_match_table = boe_of_match,
	},
	.probe = boe_panel_probe,
	.remove = boe_panel_remove,
	.shutdown = boe_panel_shutdown,
};
module_mipi_dsi_driver(boe_panel_driver);

MODULE_AUTHOR("Jitao Shi <jitao.shi@mediatek.com>");
MODULE_DESCRIPTION("DRM driver for BOE_NT51021_10_1200P_VIDEO");
MODULE_LICENSE("GPL v2");
