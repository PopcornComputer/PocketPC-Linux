// SPDX-License-Identifier: GPL-2.0+
/*
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

#define DRV_NAME "panel-ft8716"

static const char * const regulator_names[] = {
	"vddi",
	"avdd",
	"avee"
};

struct ft8716_panel_desc {
	const struct drm_display_mode *mode;
	unsigned int lanes;
	unsigned long mode_flags;
	enum mipi_dsi_pixel_format format;
};

struct ft8716 {
	struct drm_panel	panel;
	struct mipi_dsi_device	*dsi;

	struct regulator_bulk_data supplies[ARRAY_SIZE(regulator_names)];
	struct gpio_desc	*reset;
	bool prepared;

	const struct ft8716_panel_desc *desc;
};

static const struct drm_display_mode ft8716_default_mode = {
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

static const struct ft8716_panel_desc ft8716_desc = {
	.mode = &ft8716_default_mode,
	.lanes = 4,
	.mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST,
	.format = MIPI_DSI_FMT_RGB888,
};

static inline struct ft8716 *panel_to_ft8716(struct drm_panel *panel)
{
	return container_of(panel, struct ft8716, panel);
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

static int ft8716_init_sequence(struct ft8716 *ctx) {
	struct mipi_dsi_device *dsi = ctx->dsi;

	/**
	 * For MIPI interface, registers can be accessed by low power mode (LPDT).
	 * Low power mode can use either short packet
	 * (DCS Short Write, DT=0x15 or Generic Short write, DT=0x23)
	 * or long packet (DCS long write, DT=0x39 or Generic Long write, DT=0x29).
	 */

	/* Step 1: */
	/* CMD2 Enable & Enable Shift Function */
	dcs_write_seq(dsi, 0x00,0x00);
	dcs_write_seq(dsi, 0xff,0x87,0x16,0x01);
	/* Step 2: */
	/* FOCAL CMD Enable */
	dcs_write_seq(dsi, 0x00,0x80);			// Set Parameter Shift value = 80h
	dcs_write_seq(dsi, 0xff,0x87,0x16);		// Enable Focal Command

	/*
	 * Command: (C080) TCON RTN Setting
	 * Register: TCON_RTN (C080h ~ C08Dh)
	 * Description: These commands are used to set LCD Line/Frame timing.
	 * C080h ~ C084h : Define TCON line period & vertical porch line number.
	 * C089h ~ C08Dh : Define video timing.
	 */
	dcs_write_seq(dsi, 0x00,0x80);
	dcs_write_seq(dsi, 0xC0,0x00,0x77,0x00,0x10,0x10,0x00,0x77,0x10,0x10,0x00,0x7e,0x00,0x10,0x10,0x00);

	/**
	 * Register: TP_INFO (F380h): Touch Infomation
	 * Set I2C Slave Address for TP interface when BOOT_DEVICE = 1
	 */
	dcs_write_seq(dsi, 0x00,0x80);
	dcs_write_seq(dsi, 0xF3,0x70);

	/**
	 * Command: (C0A0) TCON LTPS PCG & SEL R/G/B Setting
	 * Register: TCON_LTPS2 (C0A0h ~ C0A6h)
	 * Description: These commands are used to set TCON source control.
	 */
	dcs_write_seq(dsi, 0x00,0xA0);
	dcs_write_seq(dsi, 0xC0,0x05,0x01,0x01,0x09,0x01,0x19,0x09);

	/**
	 * Command: (C0D0) TCON LTPS PCG & SEL R/G/B Setting for Video Mode
	 * Register: TCON_LTPS_VDO (C0D0h ~ C0DEh):
	 * Description: These parameters are used to create PCG and SEL R/G/B for video mode
	 * For detail description, please refer to TCON_LTPS
	 */
	dcs_write_seq(dsi, 0x00,0xD0);
	dcs_write_seq(dsi, 0xC0,0x05,0x01,0x01,0x09,0x01,0x19,0x09);

	//LTPS ckh dummy shif&&width with vbp,vfp,ckh set
	//dcs_write_seq(dsi, 0x00,0xA8);  5p5
	//dcs_write_seq(dsi, 0xF5,0x22);  5p5

	dcs_write_seq(dsi, 0x00,0x82);
	dcs_write_seq(dsi, 0xA5,0x20,0x01,0x0C);   //???

	//ckh_rgb setting in TP term 123
	//why need xR/G/B different setting at TP Term 123
	dcs_write_seq(dsi, 0x00,0x87);
	dcs_write_seq(dsi, 0xA5,0x00,0x00,0x00,0x77);

	//dcs_write_seq(dsi, 0x00,0xA0);  //5P5 ONLY ..  don't konow the register ??
	//dcs_write_seq(dsi, 0xCE,0x00,0x05,0x01,0x01,0x01,0x01,0x3F,0x0A);

	/* Command: (B3A0 ~ B3A6) Software Panel Setting
	 * Register: SW_PANSET (B3A0h ~ B3A6h)
	 * Original Comment: LTPS Initial Code & G SWAP
	 */
	dcs_write_seq(dsi, 0x00,0xA0);
	dcs_write_seq(dsi, 0xB3,0x32);
	dcs_write_seq(dsi, 0x00,0xA6);
	dcs_write_seq(dsi, 0xB3,0x48);

	/* Command: (C280) LTPS VST Setting
	 * Register: LTPS_VST_SET1 (C280h ~ C28Bh)
	 * Description: These parameters set the gate signal of a single pulse
	 * with adjustable width and location in the beginning of a display frame.
	 * Original Comment: STVL & STVL & RESET Setting //20170308
	 */
	dcs_write_seq(dsi, 0x00,0x80);
	dcs_write_seq(dsi, 0xC2,0x82,0x00,0x00,0x00,0x81,0x00,0x00,0x00,0x84,0x00,0x32,0x8A);

	/* Command: (C2B0) LTPS CKV Setting 1
	 * Register: LTPS_CKV_SET1 (C2B0h ~ C2BEh)
	 * Description: These parameters set the gate clock-type signals LTPS_CKV1-3.
	 * Original Comment: CKV Setting //20170308
	 */
	dcs_write_seq(dsi, 0x00,0xB0);
	dcs_write_seq(dsi, 0xC2,0x80,0x04,0x00,0x07,0x86,0x01,0x05,0x00,0x07,0x86,0x82,0x02,0x00,0x07,0x86);

	//CKV Setting //20170421 add CKV5 setting for D2U & GAS2
	dcs_write_seq(dsi, 0x00,0xC0);
	dcs_write_seq(dsi, 0xC2,0x81,0x03,0x00,0x07,0x86,0x81,0x03,0x00,0x80,0x00);

	//CKV Setting
	dcs_write_seq(dsi, 0x00,0xDA);
	dcs_write_seq(dsi, 0xC2,0x33,0x33);

	//dummy CKV setting //20170308
	dcs_write_seq(dsi, 0x00,0xAA);
	dcs_write_seq(dsi, 0xC3,0x9C,0x99);
	//CKV5 TP term setting //20170421
	dcs_write_seq(dsi, 0x00,0xAC);
	dcs_write_seq(dsi, 0xC3,0x99);


	//DUMMY LINE CKV ENABLE HI
	//dcs_write_seq(dsi, 0x00,0xC0);
	//dcs_write_seq(dsi, 0xC3,0x40);
	//PORCH RELATION
	//dcs_write_seq(dsi, 0x00,0xD0); //C3D0
	//dcs_write_seq(dsi, 0xC3,0x00,0x02,0x02,0x00,0x00,0x00,0x00,0x01,0x08,0x00,0x00,0x00,0x6B,0x00,0x09);

	dcs_write_seq(dsi, 0x00,0xD3);
	dcs_write_seq(dsi, 0xC3,0x10);
	dcs_write_seq(dsi, 0x00,0xE3);
	dcs_write_seq(dsi, 0xC3,0x10);

	//PanelIF Initial Code //20170502
	dcs_write_seq(dsi, 0x00,0x80);// U 2 DCC80
	dcs_write_seq(dsi, 0xCC,0x02,0x03,0x06,0x07,0x08,0x09,0x0A,0x18,0x22,0x22,0x22,0x22);
	dcs_write_seq(dsi, 0x00,0x90);// D 2 UCC90
	dcs_write_seq(dsi, 0xCC,0x03,0x02,0x09,0x08,0x07,0x06,0x19,0x0A,0x22,0x22,0x22,0x22);
	dcs_write_seq(dsi, 0x00,0xA0);// no dir 1CCA0
	dcs_write_seq(dsi, 0xCC,0x1A,0x1B,0x1C,0x1D,0x1E,0x1F,0x18,0x19,0x20,0x21,0x04,0x14,0x15,0x0A,0x22);
	dcs_write_seq(dsi, 0x00,0xB0);// no dir 2CCB0
	dcs_write_seq(dsi, 0xCC,0x22,0x22,0x22,0x22,0x22);
	dcs_write_seq(dsi, 0x00,0x80);// slp inCB80 //0508
	dcs_write_seq(dsi, 0xCB,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00);
	dcs_write_seq(dsi, 0x00,0x90);// power on 1CB90
	dcs_write_seq(dsi, 0xCB,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00);
	dcs_write_seq(dsi, 0x00,0xA0);// power on 2CBA0
	dcs_write_seq(dsi, 0xCB,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00);
	dcs_write_seq(dsi, 0x00,0xB0);// power on 3CBB0
	dcs_write_seq(dsi, 0xCB,0x00,0x00);
	dcs_write_seq(dsi, 0x00,0xC0);// power off 1CBC0 0508 change poweroff2 timing
	dcs_write_seq(dsi, 0xCB,0x05,0x05,0x05,0x05,0x05,0x05,0x05,0x05,0x00,0x00,0x00,0x00,0x05,0x05,0x05);
	dcs_write_seq(dsi, 0x00,0xD0);// power off 2CBD0
	dcs_write_seq(dsi, 0xCB,0x00,0x00,0x00,0x05,0x05,0x05,0x05,0x05,0x00,0x00,0x05,0x00,0x00,0x00,0x00);
	dcs_write_seq(dsi, 0x00,0xE0);// power off 3CBE0
	dcs_write_seq(dsi, 0xCB,0x00,0x00);
	dcs_write_seq(dsi, 0x00,0xF0);// L V DCBF0
	dcs_write_seq(dsi, 0xCB,0x0F,0x00,0x00,0x3F,0x00,0xC0,0x00,0x00);

	//20170502
	dcs_write_seq(dsi, 0x00,0x80);	// CGOUT R 1	CD80
	dcs_write_seq(dsi, 0xCD,0x22,0x22,0x22,0x22,0x01,0x06,0x04,0x08,0x07,0x18,0x17,0x05,0x03,0x1A,0x22); //0421 add CKV5 = GAS2 & D2U
	dcs_write_seq(dsi, 0x00,0x90);	// CGOUT R 2	CD90
	dcs_write_seq(dsi, 0xCD,0x0F,0x0E,0x0D);
	dcs_write_seq(dsi, 0x00,0xA0);	// CGOUT L 1	CDA0
	dcs_write_seq(dsi, 0xCD,0x22,0x02,0x03,0x05,0x07,0x08,0x18,0x17,0x04,0x06,0x1A,0x22,0x22,0x22,0x22); //0421 add CKV5 = GAS2 & D2U

	dcs_write_seq(dsi, 0x00,0xB0);	// CGOUT L 2	CDB0
	dcs_write_seq(dsi, 0xCD,0x0F,0x0E,0x0D);

	//20170313
	dcs_write_seq(dsi, 0x00,0x81);	// All gate on off	F381
	dcs_write_seq(dsi, 0xF3,0x10,0x82,0xC0,0x42,0x80,0xC0,0x10,0x82,0xC0,0x42,0x80,0xC0);      //20170811
	//Slpin All gate on
	//dcs_write_seq(dsi, 0x00,0x81);
	//dcs_write_seq(dsi, 0xF3,0x70,0x9A,0xC0,0x4E,0x83,0xC0,0x70,0x9A,0xC0,0x4E,0x83,0xC0);  //
	//dcs_write_seq(dsi, 0xF3,0x00,0x24,0x00,0x80,0x04,0x00,0x00,0x24,0x00,0x80,0x04,0x00);  //

	// CF90H - TP STB //20170308
	dcs_write_seq(dsi, 0x00,0x90);
	dcs_write_seq(dsi, 0xCF,0xFF,0x00,0xFE,0x00);

	// TP start&&count //20170308
	dcs_write_seq(dsi, 0x00,0x94);
	dcs_write_seq(dsi, 0xCF,0x00,0x00,0x10,0x20);
	dcs_write_seq(dsi, 0x00,0xA4);
	dcs_write_seq(dsi, 0xCF,0x00,0x07,0x01,0x80);
	dcs_write_seq(dsi, 0x00,0xd0);
	dcs_write_seq(dsi, 0xCF,0x08);

	//TP Initial Code
	//CE80=0x25 touch function enable
	//TP control setting(term1/2/3/4)
	dcs_write_seq(dsi, 0x00,0x80);
	dcs_write_seq(dsi, 0xCE,0x25,0x00,0x78,0x00,0x78,0xFF,0x00,0x20,0x05);
	// TP term1/2/3/4 widths control
	dcs_write_seq(dsi, 0x00,0x90);
	dcs_write_seq(dsi, 0xCE,0x00,0x5C,0x0a,0x35,0x00,0x5C,0x00,0x7b);
	//TP source RTN setting(dummy)
	dcs_write_seq(dsi, 0x00,0xB0);
	dcs_write_seq(dsi, 0xCE,0x00,0x00,0x60,0x60,0x00,0x60);
	//trim OSC48M,32K(48M?F4C0[7:0]?A32K?F4C1[3:0])
	dcs_write_seq(dsi, 0x00,0xC0);
	dcs_write_seq(dsi, 0xF4,0x93,0x36);
	// lcd_busy -> TE
	//dcs_write_seq(dsi, 0x00,0xB0);
	//dcs_write_seq(dsi, 0xF6,0x69,0x16,0x1f);

	//GAMMA SET follow 5P5
	dcs_write_seq(dsi, 0x00,0x00);
	dcs_write_seq(dsi, 0xE1,0x00,0x07,0x18,0x2B,0x37,0x42,0x55,0x64,0x6B,0x73,0x7d,0x87,0x70,0x67,0x64,0x5d,0x4f,0x44,0x35,0x2c,0x25,0x18,0x09,0x07);

	dcs_write_seq(dsi, 0x00,0x00);
	dcs_write_seq(dsi, 0xE2,0x00,0x07,0x18,0x2B,0x37,0x42,0x55,0x64,0x6B,0x73,0x7d,0x87,0x70,0x67,0x64,0x5d,0x4f,0x44,0x35,0x2c,0x25,0x18,0x09,0x07);

	//pump & regulator follow 5P5
	dcs_write_seq(dsi, 0x00,0x80);
	dcs_write_seq(dsi, 0xC5,0x00,0xC1,0xDD,0xC4,0x14,0x1E,0x00,0x55,0x50,0x00);  //C589 change to 00 from 05 for MP test

	dcs_write_seq(dsi, 0x00,0x90);  //C590
	dcs_write_seq(dsi, 0xC5,0x55,0x1E,0x14,0x00,0x88,0x10,0x4B,0x3c,0x55,0x50);//VGLO=-7V,VGH=11V,VGL=-11V  //VGH = 13.5V 20170328

	dcs_write_seq(dsi, 0x00,0x00);
	dcs_write_seq(dsi, 0xD8,0x31,0x31);// GVDD 5.3V

	dcs_write_seq(dsi, 0x00,0x00);
	dcs_write_seq(dsi, 0xD9,0x80,0xB1,0xB1,0xB1,0xB1); //VCOM flicker fine tuning

	dcs_write_seq(dsi, 0x00,0x88);
	dcs_write_seq(dsi, 0xC3,0x33,0x33);
	dcs_write_seq(dsi, 0x00,0x98);
	dcs_write_seq(dsi, 0xC3,0x33,0x33);

	//dcs_write_seq(dsi, 0x00,0xC1);
	//dcs_write_seq(dsi, 0xC5,0x44);

	dcs_write_seq(dsi, 0x00,0x80);
	dcs_write_seq(dsi, 0xC4,0x41);

	dcs_write_seq(dsi, 0x00,0x94);
	dcs_write_seq(dsi, 0xC5,0x48);

	//dcs_write_seq(dsi, 0x00,0x80); //TP_disable
	//dcs_write_seq(dsi, 0xCE,0x00);

	//dcs_write_seq(dsi, 0x00,A0);  // Freerun mode

	//dcs_write_seq(dsi, 0xF6,0x0F,0x01,0x23,0x45,0x67,0x89,0xAB,0xCD,0xEF);

	//dcs_write_seq(dsi, 0x00,A9);
	//dcs_write_seq(dsi, 0xF6,5a);

	//vgl pump disable   20170328
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

	//GOFF(GAS2),CKH EQ enable
	dcs_write_seq(dsi, 0x00,0x87);
	dcs_write_seq(dsi, 0xC3,0x33,0x33);
	dcs_write_seq(dsi, 0x00,0x97);
	dcs_write_seq(dsi, 0xC3,0x33,0x33);
	//CKV EQ enable
	dcs_write_seq(dsi, 0x00,0x83);
	dcs_write_seq(dsi, 0xC3,0x44);
	dcs_write_seq(dsi, 0x00,0x93);
	dcs_write_seq(dsi, 0xC3,0x44);

	//REST EQ enable
	dcs_write_seq(dsi, 0x00,0x81);
	dcs_write_seq(dsi, 0xC3,0x33);
	dcs_write_seq(dsi, 0x00,0x91);
	dcs_write_seq(dsi, 0xC3,0x33);

	//0410 add vb shift
	dcs_write_seq(dsi, 0x00,0x81);
	dcs_write_seq(dsi, 0xCF,0x04);
	dcs_write_seq(dsi, 0x00,0x84);
	dcs_write_seq(dsi, 0xCF,0x04);

	//0411 add pwroff SD pull GND
	dcs_write_seq(dsi, 0x00,0x81);
	dcs_write_seq(dsi, 0xC4,0xC0);//C4 mix V

	//0411 add Vcom pull GND
	dcs_write_seq(dsi, 0x00,0x8D);
	dcs_write_seq(dsi, 0xF5,0x21);//pwr off
	dcs_write_seq(dsi, 0x00,0x8c);
	dcs_write_seq(dsi, 0xF5,0x15);//pwr on

	//0426 change vgh drop
	dcs_write_seq(dsi, 0x00,0xDA);
	dcs_write_seq(dsi, 0xCF,0x16);

	//0427
	dcs_write_seq(dsi, 0x00,0x80);
	dcs_write_seq(dsi, 0xCE,0x05);

	//0508 poweroff2 add one frame
	dcs_write_seq(dsi, 0x00,0xC1);
	dcs_write_seq(dsi, 0xC0,0x11);

	/* CMD: C590 -- PWR_CTRL2 (Power Control Setting 2) */
	dcs_write_seq(dsi, 0x00,0x90);  //C590
	dcs_write_seq(dsi, 0xC5,0x77);

	/* CMD2 Disable & Disable Shift Function */
	dcs_write_seq(dsi, 0x00,0x00);
	dcs_write_seq(dsi, 0xff,0x00,0x00,0x00);

	dcs_write_seq(dsi, 0x00,0x80);
	dcs_write_seq(dsi, 0xff,0x00,0x00);

	return 0;
}

static int ft8716_prepare(struct drm_panel *panel)
{
	struct ft8716 *ctx = panel_to_ft8716(panel);
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

static int ft8716_enable(struct drm_panel *panel)
{
	struct ft8716 *ctx = panel_to_ft8716(panel);
	struct mipi_dsi_device *dsi = ctx->dsi;
	int ret;

	ret = mipi_dsi_dcs_soft_reset(dsi);
	if (ret < 0)
		return ret;

	msleep(5);

	ret = ft8716_init_sequence(ctx);
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

static int ft8716_disable(struct drm_panel *panel)
{
	struct ft8716 *ctx = panel_to_ft8716(panel);
	int ret;

	ret = mipi_dsi_dcs_set_display_off(ctx->dsi);
	if (ret < 0)
		dev_err(panel->dev, "Failed to turn off the display: %d\n", ret);

	ret = mipi_dsi_dcs_enter_sleep_mode(ctx->dsi);
	if (ret < 0)
		dev_err(panel->dev, "failed to enter sleep mode: %d\n", ret);

	return 0;
}

static int ft8716_unprepare(struct drm_panel *panel)
{
	struct ft8716 *ctx = panel_to_ft8716(panel);

	if (!ctx->prepared)
		return 0;

	gpiod_set_value(ctx->reset, 0);

	regulator_bulk_disable(ARRAY_SIZE(ctx->supplies), ctx->supplies);

	ctx->prepared = false;

	return 0;
}

static int ft8716_get_modes(struct drm_panel *panel,
			     struct drm_connector *connector)
{
	struct ft8716 *ctx = panel_to_ft8716(panel);
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

	return 1;
}

static const struct drm_panel_funcs ft8716_funcs = {
	.disable = ft8716_disable,
	.unprepare = ft8716_unprepare,
	.prepare = ft8716_prepare,
	.enable = ft8716_enable,
	.get_modes = ft8716_get_modes,
};

static int ft8716_dsi_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct ft8716 *ctx;
	int ret, i;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	mipi_dsi_set_drvdata(dsi, ctx);
	ctx->dsi = dsi;
	ctx->desc = of_device_get_match_data(dev);

	dsi->format = ctx->desc->format;
	dsi->lanes = ctx->desc->lanes;

	drm_panel_init(&ctx->panel, dev, &ft8716_funcs,
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

static int ft8716_dsi_remove(struct mipi_dsi_device *dsi)
{
	struct ft8716 *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);

	return 0;
}

static const struct of_device_id ft8716_of_match[] = {
	{ .compatible = "focaltech,8716", .data = &ft8716_desc },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, ft8716_of_match);

static struct mipi_dsi_driver ft8716_driver = {
	.probe = ft8716_dsi_probe,
	.remove = ft8716_dsi_remove,
	.driver = {
		.name = "focaltech,8716",
		.of_match_table = ft8716_of_match,
	},
};
module_mipi_dsi_driver(ft8716_driver);

MODULE_AUTHOR("Akash Gajjar <gajjar04akash@gmail.com>");
MODULE_DESCRIPTION("FT8716 Pocket-PC MIPI-DSI LCD panel");
MODULE_LICENSE("GPL");
