// SPDX-License-Identifier: GPL-2.0-only
/* LG SW43103 AMOLED panel, based on the timelm downstream command table. */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>

#include <drm/display/drm_dsc.h>
#include <drm/display/drm_dsc_helper.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>

#include <video/mipi_display.h>

struct sw43103 {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct regulator_bulk_data supplies[2];
	struct gpio_desc *reset_gpio;
	struct gpio_desc *enable_gpio;
	struct drm_dsc_config dsc;
};

static inline struct sw43103 *to_sw43103(struct drm_panel *panel)
{
	return container_of(panel, struct sw43103, panel);
}

static void sw43103_reset(struct sw43103 *ctx)
{
	/* Downstream sequence: high 10 ms, low 2 ms, high 10 ms. */
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	msleep(10);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(2000, 3000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	msleep(10);
}

static int sw43103_program(struct sw43103 *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };
	struct drm_dsc_picture_parameter_set pps;

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0xa1);
	mipi_dsi_dcs_set_column_address_multi(&dsi_ctx, 0, 1079);
	mipi_dsi_dcs_set_page_address_multi(&dsi_ctx, 0, 2459);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x53, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x30, 0x00, 0x00, 0x09, 0x9b);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x31, 0x00, 0x00, 0x04, 0x37);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x35, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0xca);
	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 100);

	/* Fingerprint circle disabled; retain the vendor's normal-mode defaults. */
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0xa5);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xcd, 0x10, 0x12, 0x01, 0x5a,
			       0x90, 0x00, 0x00, 0x00, 0x00, 0x00, 0x5d, 0x00,
			       0x01, 0x01, 0x00, 0x01, 0x11, 0x40, 0x00, 0x00,
			       0x00, 0x00, 0x3f, 0xff, 0xff, 0xff, 0x70, 0xff, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0xa1);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x55, 0x08, 0x00, 0x82, 0xa8,
			       0x1c, 0x61, 0x0a, 0x90, 0x04, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0xa3);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb3, 0x1f, 0x63, 0x00, 0x00, 0x06);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x53, 0x84);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0xa4);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb4, 0x00, 0x03, 0x05, 0xaf,
			       0x03, 0x05, 0xaf, 0x03, 0x05, 0xaf);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb5, 0x00, 0xff, 0x94, 0x1f,
			       0xae, 0x37, 0xae, 0x1f, 0x94, 0x00, 0xfb, 0x94,
			       0x1e, 0xad, 0x35, 0xab, 0x1c, 0x90, 0x00, 0xdd,
			       0x8f, 0x16, 0x9f, 0x24, 0x97, 0x04, 0x75);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb7, 0x80, 0x80, 0x80, 0x80,
			       0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb8, 0x80, 0x80, 0x80, 0x80,
			       0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb9, 0x35, 0x2d, 0x2d, 0x37,
			       0x34, 0x35, 0x36, 0x36, 0x36, 0x2d, 0x35, 0x33);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xba, 0x80, 0x80, 0x80, 0x80,
			       0x88, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x7d);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xbb, 0x80, 0x5a, 0x7b, 0x95,
			       0x65, 0x70, 0x96, 0x90, 0x90, 0x90, 0x83, 0x72);

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0xa1);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc2, 0xf3, 0x20, 0x00, 0x00, 0x1d);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0xa5);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd1, 0x0a, 0x00, 0x01);
	mipi_dsi_msleep(&dsi_ctx, 20);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd1, 0x00, 0x00, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0xa1);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xbc, 0x20, 0x05, 0x0a, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0xa2);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb7, 0x30, 0x0f, 0x0f, 0x00, 0x10);

	/* Lineage sends the DSC PPS in HS mode before enabling the display. */
	drm_dsc_pps_payload_pack(&pps, &ctx->dsc);
	ctx->dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;
	mipi_dsi_picture_parameter_set_multi(&dsi_ctx, &pps);
	ctx->dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 20);

	return dsi_ctx.accum_err;
}

static int sw43103_prepare(struct drm_panel *panel)
{
	struct sw43103 *ctx = to_sw43103(panel);
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
	if (ret)
		return ret;

	msleep(20);
	gpiod_set_value_cansleep(ctx->enable_gpio, 1);
	msleep(10);
	sw43103_reset(ctx);

	ret = sw43103_program(ctx);
	if (!ret)
		return 0;

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	gpiod_set_value_cansleep(ctx->enable_gpio, 0);
	regulator_bulk_disable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
	return ret;
}

static int sw43103_unprepare(struct drm_panel *panel)
{
	struct sw43103 *ctx = to_sw43103(panel);
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_ENTER_NORMAL_MODE);
	mipi_dsi_msleep(&dsi_ctx, 70);
	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);
	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 70);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	gpiod_set_value_cansleep(ctx->enable_gpio, 0);
	regulator_bulk_disable(ARRAY_SIZE(ctx->supplies), ctx->supplies);

	return dsi_ctx.accum_err;
}

static const struct drm_display_mode sw43103_mode = {
	.clock = 179176,
	.hdisplay = 1080,
	.hsync_start = 1130,
	.hsync_end = 1160,
	.htotal = 1210,
	.vdisplay = 2460,
	.vsync_start = 2464,
	.vsync_end = 2466,
	.vtotal = 2468,
	.width_mm = 69,
	.height_mm = 158,
	.type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
};

static int sw43103_get_modes(struct drm_panel *panel,
			     struct drm_connector *connector)
{
	return drm_connector_helper_get_modes_fixed(connector, &sw43103_mode);
}

static int sw43103_backlight_update_status(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	u16 brightness = backlight_get_brightness(bl);

	return mipi_dsi_dcs_set_display_brightness_large(dsi, brightness);
}

static const struct backlight_ops sw43103_backlight_ops = {
	.update_status = sw43103_backlight_update_status,
};

static const struct drm_panel_funcs sw43103_panel_funcs = {
	.prepare = sw43103_prepare,
	.unprepare = sw43103_unprepare,
	.get_modes = sw43103_get_modes,
};

static int sw43103_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct backlight_properties props = {
		.type = BACKLIGHT_RAW,
		.max_brightness = 2047,
		.brightness = 158,
	};
	struct sw43103 *ctx;
	int ret;

	ctx = devm_drm_panel_alloc(dev, __typeof(*ctx), panel,
				   &sw43103_panel_funcs, DRM_MODE_CONNECTOR_DSI);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	ctx->dsi = dsi;
	ctx->supplies[0].supply = "vddi";
	ctx->supplies[1].supply = "vpnl";
	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(ctx->supplies), ctx->supplies);
	if (ret)
		return dev_err_probe(dev, ret, "failed to get panel supplies\n");

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio), "failed to get reset GPIO\n");

	ctx->enable_gpio = devm_gpiod_get_optional(dev, "enable", GPIOD_OUT_LOW);
	if (IS_ERR(ctx->enable_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->enable_gpio), "failed to get enable GPIO\n");

	ctx->panel.backlight = devm_backlight_device_register(dev, dev_name(dev), dev,
							     dsi, &sw43103_backlight_ops,
							     &props);
	if (IS_ERR(ctx->panel.backlight))
		return dev_err_probe(dev, PTR_ERR(ctx->panel.backlight),
				     "failed to register backlight\n");

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_LPM;

	ctx->dsc.dsc_version_major = 1;
	ctx->dsc.dsc_version_minor = 1;
	ctx->dsc.slice_height = 60;
	ctx->dsc.slice_width = 540;
	ctx->dsc.slice_count = 2;
	ctx->dsc.bits_per_component = 8;
	ctx->dsc.bits_per_pixel = 8 << 4;
	ctx->dsc.block_pred_enable = true;
	dsi->dsc = &ctx->dsc;
	dsi->dsc_slice_per_pkt = 2;

	ctx->panel.prepare_prev_first = true;
	drm_panel_add(&ctx->panel);
	mipi_dsi_set_drvdata(dsi, ctx);

	ret = mipi_dsi_attach(dsi);
	if (ret)
		drm_panel_remove(&ctx->panel);

	return ret;
}

static void sw43103_remove(struct mipi_dsi_device *dsi)
{
	struct sw43103 *ctx = mipi_dsi_get_drvdata(dsi);

	mipi_dsi_detach(dsi);
	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id sw43103_of_match[] = {
	{ .compatible = "lg,sw43103" },
	{ }
};
MODULE_DEVICE_TABLE(of, sw43103_of_match);

static struct mipi_dsi_driver sw43103_driver = {
	.probe = sw43103_probe,
	.remove = sw43103_remove,
	.driver = {
		.name = "panel-lg-sw43103",
		.of_match_table = sw43103_of_match,
	},
};
module_mipi_dsi_driver(sw43103_driver);

MODULE_DESCRIPTION("LG SW43103 AMOLED panel driver");
MODULE_LICENSE("GPL");
