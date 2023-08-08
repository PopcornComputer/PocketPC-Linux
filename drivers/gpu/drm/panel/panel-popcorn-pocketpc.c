// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2021-2023 Icenowy Zheng <uwu@icenowy.me>
 * Copyright (C) 2021 Akash Gajjar <gajjar04akash@gmail.com>
 */

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/regulator/consumer.h>

#define DRV_NAME "panel-popcorn-pocketpc"

static const char * const regulator_names[] = {
	"vddi",
	"avdd",
	"avee"
};

struct pocketpc_panel_desc {
	const struct drm_display_mode *mode;
	unsigned int lanes;
	unsigned long mode_flags;
	enum mipi_dsi_pixel_format format;
};

struct pocketpc_panel {
	struct drm_panel	panel;
	struct mipi_dsi_device	*dsi;

	struct regulator_bulk_data supplies[ARRAY_SIZE(regulator_names)];
	struct gpio_desc	*reset;
	bool prepared;
	enum drm_panel_orientation orientation;

	const struct pocketpc_panel_desc *desc;
};

static const struct drm_display_mode pocketpc_default_mode = {
	.clock		= 135000,

	.hdisplay	= 1080,
	.hsync_start	= 1080 + 32,
	.hsync_end	= 1080 + 32 + 4,
	.htotal		= 1080 + 32 + 4 + 32,

	.vdisplay	= 1920,
	.vsync_start	= 1920 + 16,
	.vsync_end	= 1920 + 16 + 2,
	.vtotal		= 1920 + 16 + 2 + 26,

	.type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
	.width_mm = 69,
	.height_mm = 122,
};

static const struct pocketpc_panel_desc pocketpc_desc = {
	.mode = &pocketpc_default_mode,
	.lanes = 4,
	.mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST,
	.format = MIPI_DSI_FMT_RGB888,
};

static inline struct pocketpc_panel *panel_to_pocketpc(struct drm_panel *panel)
{
	return container_of(panel, struct pocketpc_panel, panel);
}

#define dcs_write_seq(dsi, cmd, seq...) do {		\
		static const u8 d[] = { seq };				\
		int ret;									\
		ret = mipi_dsi_dcs_write(dsi, cmd, d, ARRAY_SIZE(d));	\
		if (ret < 0) {								\
			pr_info("mipi dsi dcs write failed\n");	\
			return ret;								\
		}											\
	} while (0)

static int pocketpc_init_sequence(struct pocketpc_panel *ctx) {
	struct mipi_dsi_device *dsi = ctx->dsi;

	dcs_write_seq(dsi, 0x00,0x00);
	dcs_write_seq(dsi, 0xff,0x87,0x16,0x01);
	dcs_write_seq(dsi, 0x00,0x80);
	dcs_write_seq(dsi, 0xff,0x87,0x16);
	dcs_write_seq(dsi, 0x00,0x80);
	dcs_write_seq(dsi, 0xC0,0x00,0x77,0x00,0x10,0x10,0x00,0x77,0x10,0x10,0x00,0x7e,0x00,0x10,0x10,0x00);
	dcs_write_seq(dsi, 0x00,0x80);
	dcs_write_seq(dsi, 0xF3,0x70);
	dcs_write_seq(dsi, 0x00,0xA0);
	dcs_write_seq(dsi, 0xC0,0x05,0x01,0x01,0x09,0x01,0x19,0x09);
	dcs_write_seq(dsi, 0x00,0xD0);
	dcs_write_seq(dsi, 0xC0,0x05,0x01,0x01,0x09,0x01,0x19,0x09);
	dcs_write_seq(dsi, 0x00,0x82);
	dcs_write_seq(dsi, 0xA5,0x20,0x01,0x0C);   //???
	dcs_write_seq(dsi, 0x00,0x87);
	dcs_write_seq(dsi, 0xA5,0x00,0x00,0x00,0x77);
	dcs_write_seq(dsi, 0x00,0xA0);
	dcs_write_seq(dsi, 0xB3,0x32);
	dcs_write_seq(dsi, 0x00,0xA6);
	dcs_write_seq(dsi, 0xB3,0x48);
	dcs_write_seq(dsi, 0x00,0x80);
	dcs_write_seq(dsi, 0xC2,0x82,0x00,0x00,0x00,0x81,0x00,0x00,0x00,0x84,0x00,0x32,0x8A);
	dcs_write_seq(dsi, 0x00,0xB0);
	dcs_write_seq(dsi, 0xC2,0x80,0x04,0x00,0x07,0x86,0x01,0x05,0x00,0x07,0x86,0x82,0x02,0x00,0x07,0x86);
	dcs_write_seq(dsi, 0x00,0xC0);
	dcs_write_seq(dsi, 0xC2,0x81,0x03,0x00,0x07,0x86,0x81,0x03,0x00,0x80,0x00);
	dcs_write_seq(dsi, 0x00,0xDA);
	dcs_write_seq(dsi, 0xC2,0x33,0x33);
	dcs_write_seq(dsi, 0x00,0xAA);
	dcs_write_seq(dsi, 0xC3,0x9C,0x99);
	dcs_write_seq(dsi, 0x00,0xAC);
	dcs_write_seq(dsi, 0xC3,0x99);
	dcs_write_seq(dsi, 0x00,0xD3);
	dcs_write_seq(dsi, 0xC3,0x10);
	dcs_write_seq(dsi, 0x00,0xE3);
	dcs_write_seq(dsi, 0xC3,0x10);
	dcs_write_seq(dsi, 0x00,0x80);
	dcs_write_seq(dsi, 0xCC,0x02,0x03,0x06,0x07,0x08,0x09,0x0A,0x18,0x22,0x22,0x22,0x22);
	dcs_write_seq(dsi, 0x00,0x90);
	dcs_write_seq(dsi, 0xCC,0x03,0x02,0x09,0x08,0x07,0x06,0x19,0x0A,0x22,0x22,0x22,0x22);
	dcs_write_seq(dsi, 0x00,0xA0);
	dcs_write_seq(dsi, 0xCC,0x1A,0x1B,0x1C,0x1D,0x1E,0x1F,0x18,0x19,0x20,0x21,0x04,0x14,0x15,0x0A,0x22);
	dcs_write_seq(dsi, 0x00,0xB0);
	dcs_write_seq(dsi, 0xCC,0x22,0x22,0x22,0x22,0x22);
	dcs_write_seq(dsi, 0x00,0x80);
	dcs_write_seq(dsi, 0xCB,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00);
	dcs_write_seq(dsi, 0x00,0x90);
	dcs_write_seq(dsi, 0xCB,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00);
	dcs_write_seq(dsi, 0x00,0xA0);
	dcs_write_seq(dsi, 0xCB,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00);
	dcs_write_seq(dsi, 0x00,0xB0);
	dcs_write_seq(dsi, 0xCB,0x00,0x00);
	dcs_write_seq(dsi, 0x00,0xC0);
	dcs_write_seq(dsi, 0xCB,0x05,0x05,0x05,0x05,0x05,0x05,0x05,0x05,0x00,0x00,0x00,0x00,0x05,0x05,0x05);
	dcs_write_seq(dsi, 0x00,0xD0);
	dcs_write_seq(dsi, 0xCB,0x00,0x00,0x00,0x05,0x05,0x05,0x05,0x05,0x00,0x00,0x05,0x00,0x00,0x00,0x00);
	dcs_write_seq(dsi, 0x00,0xE0);
	dcs_write_seq(dsi, 0xCB,0x00,0x00);
	dcs_write_seq(dsi, 0x00,0xF0);
	dcs_write_seq(dsi, 0xCB,0x0F,0x00,0x00,0x3F,0x00,0xC0,0x00,0x00);
	dcs_write_seq(dsi, 0x00,0x80);
	dcs_write_seq(dsi, 0xCD,0x22,0x22,0x22,0x22,0x01,0x06,0x04,0x08,0x07,0x18,0x17,0x05,0x03,0x1A,0x22);
	dcs_write_seq(dsi, 0x00,0x90);
	dcs_write_seq(dsi, 0xCD,0x0F,0x0E,0x0D);
	dcs_write_seq(dsi, 0x00,0xA0);
	dcs_write_seq(dsi, 0xCD,0x22,0x02,0x03,0x05,0x07,0x08,0x18,0x17,0x04,0x06,0x1A,0x22,0x22,0x22,0x22);
	dcs_write_seq(dsi, 0x00,0xB0);
	dcs_write_seq(dsi, 0xCD,0x0F,0x0E,0x0D);
	dcs_write_seq(dsi, 0x00,0x81);
	dcs_write_seq(dsi, 0xF3,0x10,0x82,0xC0,0x42,0x80,0xC0,0x10,0x82,0xC0,0x42,0x80,0xC0);
	dcs_write_seq(dsi, 0x00,0x90);
	dcs_write_seq(dsi, 0xCF,0xFF,0x00,0xFE,0x00);
	dcs_write_seq(dsi, 0x00,0x94);
	dcs_write_seq(dsi, 0xCF,0x00,0x00,0x10,0x20);
	dcs_write_seq(dsi, 0x00,0xA4);
	dcs_write_seq(dsi, 0xCF,0x00,0x07,0x01,0x80);
	dcs_write_seq(dsi, 0x00,0xd0);
	dcs_write_seq(dsi, 0xCF,0x08);
	dcs_write_seq(dsi, 0x00,0x80);
	dcs_write_seq(dsi, 0xCE,0x25,0x00,0x78,0x00,0x78,0xFF,0x00,0x20,0x05);
	dcs_write_seq(dsi, 0x00,0x90);
	dcs_write_seq(dsi, 0xCE,0x00,0x5C,0x0a,0x35,0x00,0x5C,0x00,0x7b);
	dcs_write_seq(dsi, 0x00,0xB0);
	dcs_write_seq(dsi, 0xCE,0x00,0x00,0x60,0x60,0x00,0x60);
	dcs_write_seq(dsi, 0x00,0xC0);
	dcs_write_seq(dsi, 0xF4,0x93,0x36);
	dcs_write_seq(dsi, 0x00,0x00);
	dcs_write_seq(dsi, 0xE1,0x00,0x07,0x18,0x2B,0x37,0x42,0x55,0x64,0x6B,0x73,0x7d,0x87,0x70,0x67,0x64,0x5d,0x4f,0x44,0x35,0x2c,0x25,0x18,0x09,0x07);
	dcs_write_seq(dsi, 0x00,0x00);
	dcs_write_seq(dsi, 0xE2,0x00,0x07,0x18,0x2B,0x37,0x42,0x55,0x64,0x6B,0x73,0x7d,0x87,0x70,0x67,0x64,0x5d,0x4f,0x44,0x35,0x2c,0x25,0x18,0x09,0x07);
	dcs_write_seq(dsi, 0x00,0x80);
	dcs_write_seq(dsi, 0xC5,0x00,0xC1,0xDD,0xC4,0x14,0x1E,0x00,0x55,0x50,0x00);
	dcs_write_seq(dsi, 0x00,0x90);
	dcs_write_seq(dsi, 0xC5,0x55,0x1E,0x14,0x00,0x88,0x10,0x4B,0x3c,0x55,0x50);
	dcs_write_seq(dsi, 0x00,0x00);
	dcs_write_seq(dsi, 0xD8,0x31,0x31);
	dcs_write_seq(dsi, 0x00,0x00);
	dcs_write_seq(dsi, 0xD9,0x80,0xB1,0xB1,0xB1,0xB1);
	dcs_write_seq(dsi, 0x00,0x88);
	dcs_write_seq(dsi, 0xC3,0x33,0x33);
	dcs_write_seq(dsi, 0x00,0x98);
	dcs_write_seq(dsi, 0xC3,0x33,0x33);
	dcs_write_seq(dsi, 0x00,0x80);
	dcs_write_seq(dsi, 0xC4,0x41);
	dcs_write_seq(dsi, 0x00,0x94);
	dcs_write_seq(dsi, 0xC5,0x48);
	dcs_write_seq(dsi, 0x00,0xC3);
	dcs_write_seq(dsi, 0xF5,0x26);
	dcs_write_seq(dsi, 0x00,0xC7);
	dcs_write_seq(dsi, 0xF5,0x26);
	dcs_write_seq(dsi, 0x00,0xD3);
	dcs_write_seq(dsi, 0xF5,0x26);
	dcs_write_seq(dsi, 0x00,0xD7);
	dcs_write_seq(dsi, 0xF5,0x26);
	dcs_write_seq(dsi, 0x00,0x95);
	dcs_write_seq(dsi, 0xF5,0x26);
	dcs_write_seq(dsi, 0x00,0x98);
	dcs_write_seq(dsi, 0xF5,0x26);
	dcs_write_seq(dsi, 0x00,0xB1);
	dcs_write_seq(dsi, 0xF5,0x21);
	dcs_write_seq(dsi, 0x00,0x87);
	dcs_write_seq(dsi, 0xC3,0x33,0x33);
	dcs_write_seq(dsi, 0x00,0x97);
	dcs_write_seq(dsi, 0xC3,0x33,0x33);
	dcs_write_seq(dsi, 0x00,0x83);
	dcs_write_seq(dsi, 0xC3,0x44);
	dcs_write_seq(dsi, 0x00,0x93);
	dcs_write_seq(dsi, 0xC3,0x44);
	dcs_write_seq(dsi, 0x00,0x81);
	dcs_write_seq(dsi, 0xC3,0x33);
	dcs_write_seq(dsi, 0x00,0x91);
	dcs_write_seq(dsi, 0xC3,0x33);
	dcs_write_seq(dsi, 0x00,0x81);
	dcs_write_seq(dsi, 0xCF,0x04);
	dcs_write_seq(dsi, 0x00,0x84);
	dcs_write_seq(dsi, 0xCF,0x04);
	dcs_write_seq(dsi, 0x00,0x81);
	dcs_write_seq(dsi, 0xC4,0xC0);
	dcs_write_seq(dsi, 0x00,0x8D);
	dcs_write_seq(dsi, 0xF5,0x21);
	dcs_write_seq(dsi, 0x00,0x8c);
	dcs_write_seq(dsi, 0xF5,0x15);
	dcs_write_seq(dsi, 0x00,0xDA);
	dcs_write_seq(dsi, 0xCF,0x16);
	dcs_write_seq(dsi, 0x00,0x80);
	dcs_write_seq(dsi, 0xCE,0x05);
	dcs_write_seq(dsi, 0x00,0xC1);
	dcs_write_seq(dsi, 0xC0,0x11);
	dcs_write_seq(dsi, 0x00,0x90);
	dcs_write_seq(dsi, 0xC5,0x77);
	dcs_write_seq(dsi, 0x00,0x00);
	dcs_write_seq(dsi, 0xff,0x00,0x00,0x00);
	dcs_write_seq(dsi, 0x00,0x80);
	dcs_write_seq(dsi, 0xff,0x00,0x00);

	return 0;
}

static int pocketpc_prepare(struct drm_panel *panel)
{
	struct pocketpc_panel *ctx = panel_to_pocketpc(panel);
	int ret;

	if (ctx->prepared)
		return 0;

	ret = regulator_bulk_enable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
	if (ret)
		return ret;

	msleep(2);

	gpiod_set_value(ctx->reset, 0);

	msleep(2);

	gpiod_set_value(ctx->reset, 1);

	msleep(3);

	ctx->prepared = true;
	return 0;
}

static int pocketpc_enable(struct drm_panel *panel)
{
	struct pocketpc_panel *ctx = panel_to_pocketpc(panel);
	struct mipi_dsi_device *dsi = ctx->dsi;
	int ret;

	ret = mipi_dsi_dcs_soft_reset(dsi);
	if (ret < 0)
		return ret;

	msleep(5);

	ret = pocketpc_init_sequence(ctx);
	if (ret < 0) {
		dev_err(panel->dev, "Panel init sequence failed: %d\n", ret);
		return ret;
	}

	ret = mipi_dsi_dcs_exit_sleep_mode(dsi);
	if (ret < 0)
		return ret;

	ret = mipi_dsi_dcs_set_display_on(dsi);
	if (ret)
		return ret;

	return 0;
}

static int pocketpc_disable(struct drm_panel *panel)
{
	struct pocketpc_panel *ctx = panel_to_pocketpc(panel);
	int ret;

	ret = mipi_dsi_dcs_set_display_off(ctx->dsi);
	if (ret < 0)
		dev_err(panel->dev, "Failed to turn off the display: %d\n", ret);

	ret = mipi_dsi_dcs_enter_sleep_mode(ctx->dsi);
	if (ret < 0)
		dev_err(panel->dev, "failed to enter sleep mode: %d\n", ret);

	return 0;
}

static int pocketpc_unprepare(struct drm_panel *panel)
{
	struct pocketpc_panel *ctx = panel_to_pocketpc(panel);

	if (!ctx->prepared)
		return 0;

	gpiod_set_value(ctx->reset, 0);

	regulator_bulk_disable(ARRAY_SIZE(ctx->supplies), ctx->supplies);

	ctx->prepared = false;

	return 0;
}

static int pocketpc_get_modes(struct drm_panel *panel,
			     struct drm_connector *connector)
{
	struct pocketpc_panel *ctx = panel_to_pocketpc(panel);
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, ctx->desc->mode);
	if (!mode) {
		dev_err(&ctx->dsi->dev, "failed to add mode %ux%u@%u\n",
			ctx->desc->mode->hdisplay,
			ctx->desc->mode->vdisplay,
			drm_mode_vrefresh(ctx->desc->mode));
		return -ENOMEM;
	}
	drm_mode_set_name(mode);

	connector->display_info.width_mm = mode->width_mm;
	connector->display_info.height_mm = mode->height_mm;
	drm_mode_probed_add(connector, mode);
	drm_connector_set_panel_orientation(connector, ctx->orientation);

	return 1;
}

static const struct drm_panel_funcs pocketpc_funcs = {
	.disable = pocketpc_disable,
	.unprepare = pocketpc_unprepare,
	.prepare = pocketpc_prepare,
	.enable = pocketpc_enable,
	.get_modes = pocketpc_get_modes,
};

static int pocketpc_dsi_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct pocketpc_panel *ctx;
	int ret, i;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ret = of_drm_get_panel_orientation(dev->of_node, &ctx->orientation);
	if (ret < 0) {
		dev_err(dev, "%pOF: failed to get orientation %d\n", dev->of_node, ret);
		return ret;
	}

	mipi_dsi_set_drvdata(dsi, ctx);
	ctx->dsi = dsi;
	ctx->desc = of_device_get_match_data(dev);

	dsi->format = ctx->desc->format;
	dsi->lanes = ctx->desc->lanes;

	drm_panel_init(&ctx->panel, dev, &pocketpc_funcs,
		       DRM_MODE_CONNECTOR_DSI);

	for (i = 0; i < ARRAY_SIZE(ctx->supplies); i++)
		ctx->supplies[i].supply = regulator_names[i];

	ret = devm_regulator_bulk_get(&dsi->dev, ARRAY_SIZE(ctx->supplies),
				      ctx->supplies);
	if (ret < 0) {
		dev_err(&dsi->dev, "Couldn't get regulators\n");
		return ret;
	}

	ctx->reset = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset)) {
		dev_err(dev, "Couldn't get our reset GPIO\n");
		return PTR_ERR(ctx->reset);
	}

	ret = drm_panel_of_backlight(&ctx->panel);
	if (ret)
		return ret;

	drm_panel_add(&ctx->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		dev_err(dev, "mipi_dsi_attach failed (%d). Is host ready?\n", ret);
		drm_panel_remove(&ctx->panel);
		return ret;
	}
    return 0;
}

static void pocketpc_dsi_remove(struct mipi_dsi_device *dsi)
{
	struct pocketpc_panel *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id pocketpc_of_match[] = {
	{ .compatible = "sourceparts,popcorn-pocketpc-panel", .data = &pocketpc_desc },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, pocketpc_of_match);

static struct mipi_dsi_driver pocketpc_driver = {
	.probe = pocketpc_dsi_probe,
	.remove = pocketpc_dsi_remove,
	.driver = {
		.name = "popcorn-pocketpc-panel",
		.of_match_table = pocketpc_of_match,
	},
};
module_mipi_dsi_driver(pocketpc_driver);

MODULE_AUTHOR("Icenowy Zheng <uwu@icenowy.me>");
MODULE_AUTHOR("Akash Gajjar <gajjar04akash@gmail.com>");
MODULE_DESCRIPTION("Popcorn Computer Pocket-PC MIPI-DSI LCD panel");
MODULE_LICENSE("GPL");
