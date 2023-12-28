/*
 * driver/platform/tegra/panel-simulator.c
 *
 * Copyright (c) 2014-2022, NVIDIA CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/tegra_pwm_bl.h>
#include <linux/regulator/consumer.h>
#include <linux/backlight.h>
#include <linux/pwm_backlight.h>
#include <linux/leds.h>
#include <linux/ioport.h>
#include <linux/export.h>
#include <soc/tegra/chip-id.h>

#include "dc.h"
#include "board.h"
#include <board-panel.h>

#define TEGRA_DSI_GANGED_MODE	0

#define DSI_PANEL_RESET		1

#define DC_CTRL_MODE	TEGRA_DC_OUT_CONTINUOUS_MODE

static bool reg_requested;
/* static bool gpio_requested; */
static struct platform_device *disp_device;
static struct regulator *avdd_lcd_3v3;
static struct regulator *vdd_lcd_bl;
static struct regulator *vdd_lcd_bl_en;
static struct regulator *dvdd_lcd_1v8;
static struct regulator *vdd_ds_1v8;

static tegra_dc_bl_output panel_sim_bl_output_measured = {
	0, 0, 1, 2, 3, 4, 5, 6,
	7, 8, 9, 9, 10, 11, 12, 13,
	13, 14, 15, 16, 17, 17, 18, 19,
	20, 21, 22, 22, 23, 24, 25, 26,
	27, 27, 28, 29, 30, 31, 32, 32,
	33, 34, 35, 36, 37, 37, 38, 39,
	40, 41, 42, 42, 43, 44, 45, 46,
	47, 48, 48, 49, 50, 51, 52, 53,
	54, 55, 56, 57, 57, 58, 59, 60,
	61, 62, 63, 64, 65, 66, 67, 68,
	69, 70, 71, 71, 72, 73, 74, 75,
	76, 77, 77, 78, 79, 80, 81, 82,
	83, 84, 85, 87, 88, 89, 90, 91,
	92, 93, 94, 95, 96, 97, 98, 99,
	100, 101, 102, 103, 104, 105, 106, 107,
	108, 109, 110, 111, 112, 113, 115, 116,
	117, 118, 119, 120, 121, 122, 123, 124,
	125, 126, 127, 128, 129, 130, 131, 132,
	133, 134, 135, 136, 137, 138, 139, 141,
	142, 143, 144, 146, 147, 148, 149, 151,
	152, 153, 154, 155, 156, 157, 158, 158,
	159, 160, 161, 162, 163, 165, 166, 167,
	168, 169, 170, 171, 172, 173, 174, 176,
	177, 178, 179, 180, 182, 183, 184, 185,
	186, 187, 188, 189, 190, 191, 192, 194,
	195, 196, 197, 198, 199, 200, 201, 202,
	203, 204, 205, 206, 207, 208, 209, 210,
	211, 212, 213, 214, 215, 216, 217, 219,
	220, 221, 222, 224, 225, 226, 227, 229,
	230, 231, 232, 233, 234, 235, 236, 238,
	239, 240, 241, 242, 243, 244, 245, 246,
	247, 248, 249, 250, 251, 252, 253, 255
};

static struct tegra_dsi_cmd panel_sim_init_cmd[] = {
	/* no init command required */
};

static struct tegra_dsi_out panel_sim_pdata = {
	.controller_vs = DSI_VS_1,

	.n_data_lanes = 4,
	.video_burst_mode = TEGRA_DSI_VIDEO_NONE_BURST_MODE_WITH_SYNC_END,

	.pixel_format = TEGRA_DSI_PIXEL_FORMAT_24BIT_P,
	.refresh_rate = 60,
	.virtual_channel = TEGRA_DSI_VIRTUAL_CHANNEL_0,

	.panel_reset = DSI_PANEL_RESET,
	.power_saving_suspend = true,
	.video_data_type = TEGRA_DSI_VIDEO_TYPE_VIDEO_MODE,
	.video_clock_mode = TEGRA_DSI_VIDEO_CLOCK_TX_ONLY,
	.dsi_init_cmd = panel_sim_init_cmd,
	.n_init_cmd = ARRAY_SIZE(panel_sim_init_cmd),
	.dsi_panel_rst_gpio = -1,
};

static int panel_sim_regulator_get(struct device *dev)
{
	int err = 0;

	if (reg_requested)
		return 0;

	avdd_lcd_3v3 = regulator_get(dev, "avdd_lcd");
	if (IS_ERR(avdd_lcd_3v3)) {
		pr_err("avdd_lcd regulator get failed\n");
		err = PTR_ERR(avdd_lcd_3v3);
		avdd_lcd_3v3 = NULL;
		goto fail;
	}

	dvdd_lcd_1v8 = regulator_get(dev, "dvdd_lcd");
	if (IS_ERR(dvdd_lcd_1v8)) {
		pr_err("dvdd_lcd_1v8 regulator get failed\n");
		err = PTR_ERR(dvdd_lcd_1v8);
		dvdd_lcd_1v8 = NULL;
		goto fail;
	}

	vdd_lcd_bl = regulator_get(dev, "vdd_lcd_bl");
	if (IS_ERR(vdd_lcd_bl)) {
		pr_err("vdd_lcd_bl regulator get failed\n");
		err = PTR_ERR(vdd_lcd_bl);
		vdd_lcd_bl = NULL;
		goto fail;
	}

	vdd_lcd_bl_en = regulator_get(dev, "vdd_lcd_bl_en");
	if (IS_ERR(vdd_lcd_bl_en)) {
		pr_err("vdd_lcd_bl_en regulator get failed\n");
		err = PTR_ERR(vdd_lcd_bl_en);
		vdd_lcd_bl_en = NULL;
		goto fail;
	}
	reg_requested = true;
	return 0;
fail:
	return err;
}

/* static int panel_sim_gpio_get(void) */
/* { */
/* 	int err = 0; */

/* 	if (gpio_requested) */
/* 		return 0; */

/* 	err = gpio_request(panel_sim_pdata.dsi_panel_rst_gpio, */
/* 		"panel rst"); */
/* 	if (err < 0) { */
/* 		pr_err("panel reset gpio request failed\n"); */
/* 		goto fail; */
/* 	} */

/* 	/\* free pwm GPIO *\/ */
/* 	err = gpio_request(panel_sim_pdata.dsi_panel_bl_pwm_gpio, */
/* 		"panel pwm"); */
/* 	if (err < 0) { */
/* 		pr_err("panel pwm gpio request failed\n"); */
/* 		goto fail; */
/* 	} */
/* 	gpio_free(panel_sim_pdata.dsi_panel_bl_pwm_gpio); */
/* 	gpio_requested = true; */
/* 	return 0; */
/* fail: */
/* 	return err; */
/* } */

static int panel_sim_enable(struct device *dev)
{
	int err = 0;

	err = panel_sim_regulator_get(dev);
	if (err < 0) {
		pr_err("dsi regulator get failed\n");
		goto fail;
	}

	/* err = tegra_panel_gpio_get_dt("p,wuxga-10-1", &panel_of); */
	/* if (err < 0) { */
	/* 	/\* try to request gpios from board file *\/ */
	/* 	err = panel_sim_gpio_get(); */
	/* 	if (err < 0) { */
	/* 		pr_err("dsi gpio request failed\n"); */
	/* 		goto fail; */
	/* 	} */
	/* } */

	if (vdd_ds_1v8) {
		err = regulator_enable(vdd_ds_1v8);
		if (err < 0) {
			pr_err("vdd_ds_1v8 regulator enable failed\n");
			goto fail;
		}
	}

	if (dvdd_lcd_1v8) {
		err = regulator_enable(dvdd_lcd_1v8);
		if (err < 0) {
			pr_err("dvdd_lcd regulator enable failed\n");
			goto fail;
		}
	}

	if (avdd_lcd_3v3) {
		err = regulator_enable(avdd_lcd_3v3);
		if (err < 0) {
			pr_err("avdd_lcd regulator enable failed\n");
			goto fail;
		}
	}

	if (vdd_lcd_bl) {
		err = regulator_enable(vdd_lcd_bl);
		if (err < 0) {
			pr_err("vdd_lcd_bl regulator enable failed\n");
			goto fail;
		}
	}

	if (vdd_lcd_bl_en) {
		err = regulator_enable(vdd_lcd_bl_en);
		if (err < 0) {
			pr_err("vdd_lcd_bl_en regulator enable failed\n");
			goto fail;
		}
	}

	msleep(100);
/* #if DSI_PANEL_RESET */
/* 	err = tegra_panel_reset(&panel_of, 20); */
/* 	if (err < 0) { */
/* 		/\* use platform data *\/ */
/* 		gpio_direction_output( */
/* 		panel_sim_pdata.dsi_panel_rst_gpio, 1); */
/* 		usleep_range(1000, 5000); */
/* 		gpio_set_value(panel_sim_pdata.dsi_panel_rst_gpio, 0); */
/* 		msleep(150); */
/* 		gpio_set_value(panel_sim_pdata.dsi_panel_rst_gpio, 1); */
/* 		msleep(20); */
/* 	} */
/* #endif */

	return 0;
fail:
	return err;
}

static int panel_sim_disable(struct device *dev)
{
	if (gpio_is_valid(panel_sim_pdata.dsi_panel_rst_gpio))
		gpio_set_value(panel_sim_pdata.dsi_panel_rst_gpio, 0);

	if (vdd_lcd_bl)
		regulator_disable(vdd_lcd_bl);

	if (vdd_lcd_bl_en)
		regulator_disable(vdd_lcd_bl_en);

	if (avdd_lcd_3v3)
		regulator_disable(avdd_lcd_3v3);

	if (dvdd_lcd_1v8)
		regulator_disable(dvdd_lcd_1v8);

	if (vdd_ds_1v8)
		regulator_disable(vdd_ds_1v8);

	return 0;
}

static int panel_sim_postsuspend(void)
{
	return 0;
}

static struct tegra_dc_mode panel_sim_modes[] = {
	{
		.pclk = 27000000,
		.h_ref_to_sync = 4,
		.v_ref_to_sync = 1,
		.h_sync_width = 16,
		.v_sync_width = 2,
		.h_back_porch = 32,
		.v_back_porch = 16,
		.h_active = 640,
		.v_active = 480,
		.h_front_porch = 120,
		.v_front_porch = 17,
	},
};

#ifdef CONFIG_TEGRA_DC_CMU
static struct tegra_dc_cmu panel_sim_cmu = {
	/* lut1 maps sRGB to linear space. */
	{
	0,    1,    2,    4,    5,    6,    7,    9,
	10,   11,   12,   14,   15,   16,   18,   20,
	21,   23,   25,   27,   29,   31,   33,   35,
	37,   40,   42,   45,   48,   50,   53,   56,
	59,   62,   66,   69,   72,   76,   79,   83,
	87,   91,   95,   99,   103,  107,  112,  116,
	121,  126,  131,  136,  141,  146,  151,  156,
	162,  168,  173,  179,  185,  191,  197,  204,
	210,  216,  223,  230,  237,  244,  251,  258,
	265,  273,  280,  288,  296,  304,  312,  320,
	329,  337,  346,  354,  363,  372,  381,  390,
	400,  409,  419,  428,  438,  448,  458,  469,
	479,  490,  500,  511,  522,  533,  544,  555,
	567,  578,  590,  602,  614,  626,  639,  651,
	664,  676,  689,  702,  715,  728,  742,  755,
	769,  783,  797,  811,  825,  840,  854,  869,
	884,  899,  914,  929,  945,  960,  976,  992,
	1008, 1024, 1041, 1057, 1074, 1091, 1108, 1125,
	1142, 1159, 1177, 1195, 1213, 1231, 1249, 1267,
	1286, 1304, 1323, 1342, 1361, 1381, 1400, 1420,
	1440, 1459, 1480, 1500, 1520, 1541, 1562, 1582,
	1603, 1625, 1646, 1668, 1689, 1711, 1733, 1755,
	1778, 1800, 1823, 1846, 1869, 1892, 1916, 1939,
	1963, 1987, 2011, 2035, 2059, 2084, 2109, 2133,
	2159, 2184, 2209, 2235, 2260, 2286, 2312, 2339,
	2365, 2392, 2419, 2446, 2473, 2500, 2527, 2555,
	2583, 2611, 2639, 2668, 2696, 2725, 2754, 2783,
	2812, 2841, 2871, 2901, 2931, 2961, 2991, 3022,
	3052, 3083, 3114, 3146, 3177, 3209, 3240, 3272,
	3304, 3337, 3369, 3402, 3435, 3468, 3501, 3535,
	3568, 3602, 3636, 3670, 3705, 3739, 3774, 3809,
	3844, 3879, 3915, 3950, 3986, 4022, 4059, 4095,
	},
	/* csc */
	{
		0x138, 0x3Ba, 0x00D, /* 1.220 -0.270 0.050 */
		0x3F5, 0x120, 0x3E6, /* 0.038 1.124 0.100 */
		0x3FE, 0x3F8, 0x0E9, /* -0.002 -0.026 0.913 */
	},
	/* lut2 maps linear space to sRGB */
	{
		0, 1, 2, 3, 4, 5, 6, 6,
		7, 8, 9, 10, 11, 11, 12, 13,
		13, 14, 15, 15, 16, 17, 17, 18,
		18, 19, 19, 20, 20, 21, 21, 22,
		22, 23, 23, 23, 24, 24, 24, 25,
		25, 25, 26, 26, 26, 27, 27, 27,
		28, 28, 28, 28, 29, 29, 29, 29,
		30, 30, 30, 30, 31, 31, 31, 31,
		32, 32, 32, 32, 33, 33, 33, 33,
		34, 34, 34, 35, 35, 35, 35, 36,
		36, 36, 37, 37, 37, 37, 38, 38,
		38, 39, 39, 39, 39, 40, 40, 40,
		41, 41, 41, 41, 42, 42, 42, 43,
		43, 43, 43, 44, 44, 44, 45, 45,
		45, 45, 46, 46, 46, 46, 47, 47,
		47, 47, 48, 48, 48, 48, 49, 49,
		49, 49, 50, 50, 50, 50, 50, 51,
		51, 51, 51, 52, 52, 52, 52, 52,
		53, 53, 53, 53, 53, 53, 54, 54,
		54, 54, 54, 55, 55, 55, 55, 55,
		55, 56, 56, 56, 56, 56, 56, 57,
		57, 57, 57, 57, 57, 57, 58, 58,
		58, 58, 58, 58, 59, 59, 59, 59,
		59, 59, 59, 60, 60, 60, 60, 60,
		60, 60, 61, 61, 61, 61, 61, 61,
		61, 62, 62, 62, 62, 62, 62, 62,
		63, 63, 63, 63, 63, 63, 63, 64,
		64, 64, 64, 64, 64, 64, 65, 65,
		65, 65, 65, 65, 66, 66, 66, 66,
		66, 66, 66, 67, 67, 67, 67, 67,
		67, 68, 68, 68, 68, 68, 68, 69,
		69, 69, 69, 69, 69, 70, 70, 70,
		70, 70, 70, 71, 71, 71, 71, 71,
		71, 72, 72, 72, 72, 72, 72, 73,
		73, 73, 73, 73, 73, 74, 74, 74,
		74, 74, 74, 74, 75, 75, 75, 75,
		75, 75, 76, 76, 76, 76, 76, 76,
		77, 77, 77, 77, 77, 77, 77, 78,
		78, 78, 78, 78, 78, 79, 79, 79,
		79, 79, 79, 79, 80, 80, 80, 80,
		80, 80, 80, 80, 81, 81, 81, 81,
		81, 81, 81, 82, 82, 82, 82, 82,
		82, 82, 82, 83, 83, 83, 83, 83,
		83, 83, 83, 83, 84, 84, 84, 84,
		84, 84, 84, 84, 85, 85, 85, 85,
		85, 85, 85, 85, 85, 85, 86, 86,
		86, 86, 86, 86, 86, 86, 86, 86,
		87, 87, 87, 87, 87, 87, 87, 87,
		87, 87, 88, 88, 88, 88, 88, 88,
		88, 88, 88, 88, 88, 88, 89, 89,
		89, 89, 89, 89, 89, 89, 89, 89,
		89, 89, 90, 90, 90, 90, 90, 90,
		90, 90, 90, 90, 90, 90, 91, 91,
		91, 91, 91, 91, 91, 91, 91, 91,
		91, 91, 91, 92, 92, 92, 92, 92,
		92, 92, 92, 92, 92, 92, 92, 92,
		93, 93, 93, 93, 93, 93, 93, 93,
		93, 93, 93, 93, 93, 93, 94, 94,
		94, 94, 94, 94, 94, 94, 94, 94,
		94, 94, 94, 94, 95, 95, 95, 95,
		95, 95, 95, 95, 95, 95, 95, 95,
		95, 96, 96, 96, 96, 96, 96, 96,
		96, 96, 96, 96, 96, 96, 97, 97,
		97, 97, 97, 97, 97, 97, 97, 97,
		98, 99, 99, 100, 101, 101, 102, 103,
		103, 104, 105, 105, 106, 107, 107, 108,
		109, 110, 110, 111, 112, 112, 113, 114,
		114, 115, 115, 116, 117, 117, 118, 119,
		119, 120, 120, 121, 121, 122, 123, 123,
		124, 124, 125, 125, 126, 126, 127, 128,
		128, 129, 129, 130, 130, 131, 131, 132,
		132, 133, 133, 134, 134, 135, 135, 136,
		136, 137, 138, 138, 139, 139, 140, 140,
		141, 141, 142, 142, 143, 143, 144, 144,
		144, 145, 145, 146, 146, 147, 147, 148,
		148, 149, 149, 150, 150, 151, 151, 152,
		152, 153, 153, 153, 154, 154, 155, 155,
		156, 156, 157, 157, 157, 158, 158, 159,
		159, 160, 160, 160, 161, 161, 162, 162,
		162, 163, 163, 164, 164, 164, 165, 165,
		165, 166, 166, 167, 167, 167, 168, 168,
		168, 169, 169, 169, 170, 170, 171, 171,
		171, 172, 172, 172, 173, 173, 173, 174,
		174, 174, 175, 175, 175, 176, 176, 176,
		177, 177, 177, 178, 178, 178, 179, 179,
		179, 180, 180, 180, 181, 181, 181, 182,
		182, 182, 183, 183, 183, 184, 184, 184,
		185, 185, 185, 185, 186, 186, 186, 187,
		187, 187, 188, 188, 188, 189, 189, 189,
		190, 190, 190, 191, 191, 191, 191, 192,
		192, 192, 193, 193, 193, 194, 194, 194,
		195, 195, 195, 195, 196, 196, 196, 197,
		197, 197, 198, 198, 198, 199, 199, 199,
		199, 200, 200, 200, 201, 201, 201, 202,
		202, 202, 203, 203, 203, 203, 204, 204,
		204, 205, 205, 205, 206, 206, 206, 207,
		207, 207, 208, 208, 208, 208, 209, 209,
		209, 210, 210, 210, 211, 211, 211, 212,
		212, 212, 213, 213, 213, 214, 214, 214,
		215, 215, 215, 215, 216, 216, 216, 217,
		217, 217, 218, 218, 218, 219, 219, 219,
		220, 220, 220, 220, 221, 221, 221, 222,
		222, 222, 222, 223, 223, 223, 224, 224,
		224, 224, 225, 225, 225, 226, 226, 226,
		226, 227, 227, 227, 227, 228, 228, 228,
		229, 229, 229, 229, 230, 230, 230, 230,
		230, 231, 231, 231, 231, 232, 232, 232,
		232, 233, 233, 233, 233, 234, 234, 234,
		234, 234, 235, 235, 235, 235, 236, 236,
		236, 236, 236, 237, 237, 237, 237, 238,
		238, 238, 238, 238, 239, 239, 239, 239,
		239, 240, 240, 240, 240, 240, 241, 241,
		241, 241, 241, 242, 242, 242, 242, 243,
		243, 243, 243, 243, 244, 244, 244, 244,
		244, 245, 245, 245, 245, 245, 246, 246,
		246, 246, 246, 247, 247, 247, 247, 248,
		248, 248, 248, 248, 249, 249, 249, 249,
		250, 250, 250, 250, 251, 251, 251, 251,
		251, 252, 252, 252, 253, 253, 253, 253,
		254, 254, 254, 254, 255, 255, 255, 255,
	},
};
#endif

static int panel_sim_bl_notify(struct device *dev, int brightness)
{
	struct backlight_device *bl = NULL;
	struct pwm_bl_data *pb = NULL;
	bl = (struct backlight_device *)dev_get_drvdata(dev);
	pb = (struct pwm_bl_data *)dev_get_drvdata(&bl->dev);

	/* Apply any backlight response curve */
	if (brightness > 255)
		pr_info("Error: Brightness > 255!\n");
	else if (pb->bl_measured)
		brightness = pb->bl_measured[brightness];

	return brightness;
}

static int panel_sim_check_fb(struct device *dev, struct fb_info *info)
{
	return info->device == &disp_device->dev;
}

static struct platform_pwm_backlight_data panel_sim_bl_data = {
	.pwm_id		= 1,
	.max_brightness	= 255,
	.dft_brightness	= 224,
	.pwm_period_ns	= 1000000,
	.bl_measured    = panel_sim_bl_output_measured,
	.pwm_gpio	= UINT_MAX,
	.notify		= panel_sim_bl_notify,
	/* Only toggle backlight on fb blank notifications for disp1 */
	.check_fb	= panel_sim_check_fb,
};

static struct platform_device __maybe_unused
		panel_sim_bl_device = {
	.name	= "pwm-backlight",
	.id	= -1,
	.dev	= {
		.platform_data = &panel_sim_bl_data,
	},
};

static struct platform_device __maybe_unused
			*panel_sim_bl_devices[] __initdata = {
	&panel_sim_bl_device,
};

static int  __init panel_sim_register_bl_dev(void)
{
	int err = 0;
	/* struct device_node *dc1_node = NULL; */
	/* struct device_node *dc2_node = NULL; */
	/* struct device_node *pwm_bl_node = NULL; */

	/* find_dc_node(&dc1_node, &dc2_node); */
	/* pwm_bl_node = of_find_compatible_node(NULL, NULL, */
	/* 	"pwm-backlight"); */

	/* if (!of_have_populated_dt() || !dc1_node || */
	/* 	!of_device_is_available(dc1_node) || */
	/* 	!pwm_bl_node || */
	/* 	!of_device_is_available(pwm_bl_node)) { */
	/* 	err = platform_add_devices(panel_sim_bl_devices, */
	/* 		ARRAY_SIZE(panel_sim_bl_devices)); */
	/* 	if (err) { */
	/* 		pr_err("disp1 bl device registration failed"); */
	/* 		return err; */
	/* 	} */
	/* } */
	return err;
}

static void panel_sim_set_disp_device(
	struct platform_device *dalmore_display_device)
{
	disp_device = dalmore_display_device;
}

static void panel_sim_dc_out_init(struct tegra_dc_out *dc)
{
	dc->dsi = &panel_sim_pdata;
	dc->parent_clk = "pll_d_out0";
	dc->modes = panel_sim_modes;
	dc->n_modes = ARRAY_SIZE(panel_sim_modes);
	dc->enable = panel_sim_enable;
	dc->disable = panel_sim_disable;
	dc->postsuspend	= panel_sim_postsuspend,
	dc->width = 217;
	dc->height = 135;
	dc->flags = DC_CTRL_MODE;
}

static void panel_sim_fb_data_init(struct tegra_fb_data *fb)
{
	fb->xres = panel_sim_modes[0].h_active;
	fb->yres = panel_sim_modes[0].v_active;
}

#ifdef CONFIG_TEGRA_DC_CMU
static void panel_sim_cmu_init(struct tegra_dc_platform_data *pdata)
{
	pdata->cmu = &panel_sim_cmu;
}
#endif

static struct pwm_bl_data_dt_ops panel_sim_pwm_bl_ops = {
	.notify = panel_sim_bl_notify,
	.check_fb = panel_sim_check_fb,
	.blnode_compatible = "p,wuxga-10-1-bl",
};

struct tegra_panel_ops panel_sim_ops = {
	.enable = panel_sim_enable,
	.disable = panel_sim_disable,
	.postsuspend = panel_sim_postsuspend,
	.pwm_bl_ops = &panel_sim_pwm_bl_ops,
};

struct tegra_panel __initdata panel_sim = {
	.init_dc_out = panel_sim_dc_out_init,
	.init_fb_data = panel_sim_fb_data_init,
	.register_bl_dev = panel_sim_register_bl_dev,
#ifdef CONFIG_TEGRA_DC_CMU
	.init_cmu_data = panel_sim_cmu_init,
#endif
	.set_disp_device = panel_sim_set_disp_device,
};
