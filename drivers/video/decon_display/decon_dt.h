/* drivers/video/decon_display/decon_dt.h
 *
 * Copyright (c) 2011 Samsung Electronics
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __DECON_DISPLAY_DT_HEADER__
#define __DECON_DISPLAY_DT_HEADER__

#ifdef CONFIG_SOC_EXYNOS5430
void dump_driver_data(void);
void dump_s3c_fb_variant(struct s3c_fb_variant *p_fb_variant);
void dump_s3c_fb_win_variant(struct s3c_fb_win_variant *p_fb_win_variant);
void dump_s3c_fb_win_variants(struct s3c_fb_win_variant p_fb_win_variant[],
	int num);

int parse_display_dt_exynos5430(struct device_node *np);
struct s3c_fb_driverdata *get_display_drvdata_exynos5430(void);
struct s3c_fb_platdata *get_display_platdata_exynos5430(void);
struct mipi_dsim_config *get_display_dsi_drvdata_exynos5430(void);

int parse_display_dsi_dt_exynos5430(struct device_node *np);
int get_display_dsi_power_gpio_exynos5430(void);

#define parse_display_dt(node) parse_display_dt_exynos5430(node)
#define get_display_drvdata() get_display_drvdata_exynos5430()
#define get_display_platdata() get_display_platdata_exynos5430()
#define get_display_dsi_drvdata() get_display_dsi_drvdata_exynos5430()

/* Temporary code for parsinng DSI device tree */
#define parse_display_dsi_dt(node) parse_display_dsi_dt_exynos5430(node)
#define get_display_dsi_power_gpio() get_display_dsi_power_gpio_exynos5430()

#endif

#endif
