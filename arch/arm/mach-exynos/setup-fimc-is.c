/* linux/arch/arm/mach-exynos/setup-fimc-is.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * FIMC-IS gpio and clock configuration
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/gpio.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/regulator/consumer.h>
#include <linux/delay.h>
#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <mach/regs-gpio.h>
#include <mach/map.h>
#include <mach/regs-clock.h>
#include <plat/gpio-cfg.h>
#include <plat/map-s5p.h>
#include <plat/cpu.h>
#include <media/exynos_fimc_is.h>
#ifdef CONFIG_OF
#include <linux/of_gpio.h>
#endif
#if defined(CONFIG_SOC_EXYNOS5430)
#include <mach/regs-clock-exynos5430.h>
#endif

struct platform_device; /* don't need the contents */

/* #define EXYNOS5430_CMU_DUMP */

#if defined(CONFIG_ARCH_EXYNOS5)
/*------------------------------------------------------*/
/*		Exynos5 series - FIMC-IS		*/
/*------------------------------------------------------*/
int exynos5_fimc_is_cfg_gpio(struct platform_device *pdev,
				int channel, bool flag_on)
{
	struct pinctrl *pinctrl_mclk;
	struct pinctrl *pinctrl_i2c;
	struct exynos5_platform_fimc_is *pdata;
	struct fimc_is_gpio_info *_gpio_info;

	pdata = dev_get_platdata(&pdev->dev);
	if (!pdata)
		return PTR_ERR(pdata);

	_gpio_info = pdata->_gpio_info;
	if (!_gpio_info)
		return PTR_ERR(_gpio_info);

	pr_debug("%s\n", __func__);

	if ((flag_on == false) && (channel == CSI_ID_A)) {
		gpio_request_one(_gpio_info->gpio_main_sda, GPIOF_IN, "MAIN_CAM_SDA");
		gpio_free(_gpio_info->gpio_main_sda);

		gpio_request_one(_gpio_info->gpio_main_scl, GPIOF_IN, "MAIN_CAM_SCL");
		gpio_free(_gpio_info->gpio_main_scl);

		gpio_request_one(_gpio_info->gpio_main_mclk, GPIOF_IN, "MAIN_CAM_MCLK");
		gpio_free(_gpio_info->gpio_main_mclk);

		usleep_range(1000, 1000);
		__gpio_set_value(_gpio_info->gpio_main_rst, 0);
		usleep_range(1000, 1000);
		__gpio_set_value(_gpio_info->gpio_main_rst, 1);
		gpio_request_one(_gpio_info->gpio_main_rst, GPIOF_IN, "MAIN_CAM_RESET");
		gpio_free(_gpio_info->gpio_main_rst);
		goto exit;
	} else if ((flag_on == false) && (channel == CSI_ID_B)) {
		gpio_request_one(_gpio_info->gpio_vt_sda, GPIOF_IN, "VT_CAM_SDA");
		gpio_free(_gpio_info->gpio_vt_sda);

		gpio_request_one(_gpio_info->gpio_vt_scl, GPIOF_IN, "VT_CAM_SCL");
		gpio_free(_gpio_info->gpio_vt_scl);

		gpio_request_one(_gpio_info->gpio_vt_mclk, GPIOF_IN, "VT_CAM_MCLK");
		gpio_free(_gpio_info->gpio_vt_mclk);

		usleep_range(1000, 1000);
		__gpio_set_value(_gpio_info->gpio_vt_rst, 0);
		usleep_range(1000, 1000);
		__gpio_set_value(_gpio_info->gpio_vt_rst, 1);
		gpio_request_one(_gpio_info->gpio_vt_rst, GPIOF_IN, "VT_CAM_RESET");
		gpio_free(_gpio_info->gpio_vt_rst);
		goto exit;
	} else {
		pr_err("%s: could not find sensor\n", __func__);
	}

	if ((flag_on == true) && (channel == CSI_ID_A)) {
		/* RESET(GPC0)*/
		gpio_request_one(_gpio_info->gpio_main_rst, GPIOF_OUT_INIT_HIGH, "MAIN_CAM_RESET");
		__gpio_set_value(_gpio_info->gpio_main_rst, 0);
		usleep_range(1000, 1000);
		__gpio_set_value(_gpio_info->gpio_main_rst, 1);
		usleep_range(1000, 1000);
		gpio_free(_gpio_info->gpio_main_rst);

		/* I2C(GPC2) */
		pinctrl_i2c = devm_pinctrl_get_select(&pdev->dev,"main-i2c");
		if (IS_ERR(pinctrl_i2c))
			pr_err("%s: main cam i2c pins are not configured\n", __func__);
		msleep(1);

		/* CIS_CLK(GPD7) */
		pinctrl_mclk = devm_pinctrl_get_select(&pdev->dev,"main-mclk");
		if (IS_ERR(pinctrl_mclk))
			pr_err("%s: main cam mclk pin is not configured\n", __func__);
		usleep_range(1000, 1000);
	} else if ((flag_on == true) && (channel == CSI_ID_B)) {
		/* RESET(GPC0) */
		gpio_request_one(_gpio_info->gpio_vt_rst, GPIOF_OUT_INIT_HIGH, "VT_CAM_RESET");
		__gpio_set_value(_gpio_info->gpio_vt_rst, 0);
		usleep_range(1000, 1000);
		__gpio_set_value(_gpio_info->gpio_vt_rst, 1);
		usleep_range(1000, 1000);
		gpio_free(_gpio_info->gpio_vt_rst);

		/* I2C(GPC2) */
		pinctrl_i2c = devm_pinctrl_get_select(&pdev->dev,"vt-i2c");
		if (IS_ERR(pinctrl_i2c))
			pr_err("%s: vt cam i2c pins are not configured\n", __func__);
		msleep(1);

		/* CIS_CLK(GPD7) */
		pinctrl_mclk = devm_pinctrl_get_select(&pdev->dev,"vt-mclk");
		if (IS_ERR(pinctrl_mclk))
			pr_err("%s: vt cam mclk pin is not configured\n", __func__);
		usleep_range(1000, 1000);
	} else {
		pr_err("%s: could not find sensor\n", __func__);
	}

exit:
	return 0;
}

int exynos5_fimc_is_print_cfg(struct platform_device *pdev, u32 channel)
{
	pr_debug("%s\n", __func__);

	return 0;
}

/* utility function to set parent with names */
int fimc_is_set_parent(const char *child, const char *parent)
{
	struct clk *p;
	struct clk *c;

	p= __clk_lookup(parent);
	if (IS_ERR(p)) {
		pr_err("%s: could not lookup clock : %s\n", __func__, parent);
		return -EINVAL;
	}

	c= __clk_lookup(child);
	if (IS_ERR(c)) {
		pr_err("%s: could not lookup clock : %s\n", __func__, child);
		return -EINVAL;
	}

	return clk_set_parent(c, p);
}

/* utility function to get parent name with name */
struct clk *fimc_is_get_parent(const char *child)
{
	struct clk *c;

	c = __clk_lookup(child);
	if (IS_ERR(c)) {
		pr_err("%s: could not lookup clock : %s\n", __func__, child);
		return NULL;
	}

	return clk_get_parent(c);
}

/* utility function to set rate with name */
int fimc_is_set_rate(const char *conid, unsigned int rate)
{
	struct clk *target;

	target = __clk_lookup(conid);
	if (IS_ERR(target)) {
		pr_err("%s: could not lookup clock : %s\n", __func__, conid);
		return -EINVAL;
	}

	return clk_set_rate(target, rate);
}

/* utility function to get rate with name */
unsigned int  fimc_is_get_rate(const char *conid)
{
	struct clk *target;
	unsigned int rate_target;

	target = __clk_lookup(conid);
	if (IS_ERR(target)) {
		pr_err("%s: could not lookup clock : %s\n", __func__, conid);
		return -EINVAL;
	}

	rate_target = clk_get_rate(target);
	pr_info("%s : %d\n", conid, rate_target);

	return rate_target;
}

/* utility function to eable with name */
unsigned int  fimc_is_enable(const char *conid)
{
	struct clk *target;

	target = __clk_lookup(conid);
	if (IS_ERR(target)) {
		pr_err("%s: could not lookup clock : %s\n", __func__, conid);
		return -EINVAL;
	}

	clk_prepare(target);

	return clk_enable(target);
}

int cfg_clk_div_max(struct platform_device *pdev)
{
	/* SCLK */
	/* SCLK_SPI0 */
	fimc_is_set_parent("mout_sclk_isp_spi0", "oscclk");
	fimc_is_set_rate("dout_sclk_isp_spi0_a", 1);
	fimc_is_set_rate("dout_sclk_isp_spi0_b", 1);
	fimc_is_set_parent("mout_sclk_isp_spi0_user", "oscclk");

	/* SCLK_SPI1 */
	fimc_is_set_parent("mout_sclk_isp_spi1", "oscclk");
	fimc_is_set_rate("dout_sclk_isp_spi1_a", 1);
	fimc_is_set_rate("dout_sclk_isp_spi1_b", 1);
	fimc_is_set_parent("mout_sclk_isp_spi1_user", "oscclk");

	/* SCLK_UART */
	fimc_is_set_parent("mout_sclk_isp_uart", "oscclk");
	fimc_is_set_rate("dout_sclk_isp_uart", 1);
	fimc_is_set_parent("mout_sclk_isp_uart_user", "oscclk");

	/* CAM0 */
	/* USER_MUX_SEL */
	fimc_is_set_parent("mout_aclk_cam0_552_user", "oscclk");
	fimc_is_set_parent("mout_aclk_cam0_400_user", "oscclk");
	fimc_is_set_parent("mout_aclk_cam0_333_user", "oscclk");
	fimc_is_set_parent("mout_phyclk_rxbyteclkhs0_s4", "oscclk");
	fimc_is_set_parent("mout_phyclk_rxbyteclkhs0_s2a", "oscclk");
	/* LITE A */
	fimc_is_set_rate("dout_aclk_lite_a", 1);
	fimc_is_set_rate("dout_pclk_lite_a", 1);
	/* LITE B */
	fimc_is_set_rate("dout_aclk_lite_b", 1);
	fimc_is_set_rate("dout_pclk_lite_b", 1);
	/* LITE D */
	fimc_is_set_rate("dout_aclk_lite_d", 1);
	fimc_is_set_rate("dout_pclk_lite_d", 1);
	/* LITE C PIXELASYNC */
	fimc_is_set_rate("dout_sclk_pixelasync_lite_c_init", 1);
	fimc_is_set_rate("dout_pclk_pixelasync_lite_c", 1);
	fimc_is_set_rate("dout_sclk_pixelasync_lite_c", 1);
	/* 3AA 0 */
	fimc_is_set_rate("dout_aclk_3aa0", 1);
	fimc_is_set_rate("dout_pclk_3aa0", 1);
	/* 3AA 0 */
	fimc_is_set_rate("dout_aclk_3aa1", 1);
	fimc_is_set_rate("dout_pclk_3aa1", 1);
	/* CSI 0 */
	fimc_is_set_rate("dout_aclk_csis0", 1);
	/* CSI 1 */
	fimc_is_set_rate("dout_aclk_csis1", 1);
	/* CAM0 400 */
	fimc_is_set_rate("dout_aclk_cam0_400", 1);
	fimc_is_set_rate("dout_aclk_cam0_200", 1);
	fimc_is_set_rate("dout_pclk_cam0_50", 1);

	/* CAM1 */
	/* USER_MUX_SEL */
	fimc_is_set_parent("mout_aclk_cam1_552_user", "oscclk");
	fimc_is_set_parent("mout_aclk_cam1_400_user", "oscclk");
	fimc_is_set_parent("mout_aclk_cam1_333_user", "oscclk");
	/* C-A5 */
	fimc_is_set_rate("dout_atclk_cam1", 1);
	fimc_is_set_rate("dout_pclk_dbg_cam1", 1);
	/* LITE A */
	fimc_is_set_rate("dout_aclk_lite_c", 1);
	fimc_is_set_rate("dout_pclk_lite_c", 1);
	/* FD */
	fimc_is_set_rate("dout_aclk_fd", 1);
	fimc_is_set_rate("dout_pclk_fd", 1);
	/* CSI 2 */
	fimc_is_set_rate("dout_aclk_csis2_a", 1);

	/* CMU_ISP */
	/* USER_MUX_SEL */
	fimc_is_set_parent("mout_aclk_isp_400_user", "oscclk");
	fimc_is_set_parent("mout_aclk_isp_dis_400_user", "oscclk");
	/* ISP */
	fimc_is_set_rate("dout_aclk_isp_c_200", 1);
	fimc_is_set_rate("dout_aclk_isp_d_200", 1);
	fimc_is_set_rate("dout_pclk_isp", 1);
	/* DIS */
	fimc_is_set_rate("dout_pclk_isp_dis", 1);

#ifdef EXYNOS5430_CMU_DUMP
	/* CMU_TOP_DUMP */
	pr_info("EXYNOS5430_SRC_SEL_TOP1(0x%x)\n", readl(EXYNOS5430_SRC_SEL_TOP1));
	pr_info("EXYNOS5430_SRC_SEL_TOP2(0x%x)\n", readl(EXYNOS5430_SRC_SEL_TOP2));
	pr_info("EXYNOS5430_SRC_SEL_TOP_CAM1(0x%x)\n", readl(EXYNOS5430_SRC_SEL_TOP_CAM1));
	pr_info("EXYNOS5430_SRC_ENABLE_TOP0(0x%x)\n", readl(EXYNOS5430_SRC_ENABLE_TOP0));
	pr_info("EXYNOS5430_SRC_ENABLE_TOP1(0x%x)\n", readl(EXYNOS5430_SRC_ENABLE_TOP1));
	pr_info("EXYNOS5430_SRC_ENABLE_TOP2(0x%x)\n", readl(EXYNOS5430_SRC_ENABLE_TOP2));
	pr_info("EXYNOS5430_SRC_ENABLE_TOP3(0x%x)\n", readl(EXYNOS5430_SRC_ENABLE_TOP3));
	pr_info("EXYNOS5430_SRC_ENABLE_TOP_CAM1(0x%x)\n", readl(EXYNOS5430_SRC_ENABLE_TOP_CAM1));
	pr_info("EXYNOS5430_DIV_TOP0(0x%x)\n", readl(EXYNOS5430_DIV_TOP0));
	pr_info("EXYNOS5430_DIV_TOP_CAM10(0x%x)\n", readl(EXYNOS5430_DIV_TOP_CAM10));
	pr_info("EXYNOS5430_DIV_TOP_CAM11(0x%x)\n", readl(EXYNOS5430_DIV_TOP_CAM11));
	pr_info("EXYNOS5430_ENABLE_IP_TOP(0x%x)\n", readl(EXYNOS5430_ENABLE_IP_TOP));
	/* CMU_CAM0_DUMP */
	pr_info("EXYNOS5430_SRC_SEL_CAM00(0x%x)\n", readl(EXYNOS5430_SRC_SEL_CAM00));
	pr_info("EXYNOS5430_SRC_SEL_CAM01(0x%x)\n", readl(EXYNOS5430_SRC_SEL_CAM01));
	pr_info("EXYNOS5430_SRC_SEL_CAM02(0x%x)\n", readl(EXYNOS5430_SRC_SEL_CAM02));
	pr_info("EXYNOS5430_SRC_SEL_CAM03(0x%x)\n", readl(EXYNOS5430_SRC_SEL_CAM03));
	pr_info("EXYNOS5430_SRC_SEL_CAM04(0x%x)\n", readl(EXYNOS5430_SRC_SEL_CAM04));
	pr_info("EXYNOS5430_SRC_ENABLE_CAM00(0x%x)\n", readl(EXYNOS5430_SRC_ENABLE_CAM00));
	pr_info("EXYNOS5430_SRC_ENABLE_CAM01(0x%x)\n", readl(EXYNOS5430_SRC_ENABLE_CAM01));
	pr_info("EXYNOS5430_SRC_ENABLE_CAM02(0x%x)\n", readl(EXYNOS5430_SRC_ENABLE_CAM02));
	pr_info("EXYNOS5430_SRC_ENABLE_CAM03(0x%x)\n", readl(EXYNOS5430_SRC_ENABLE_CAM03));
	pr_info("EXYNOS5430_SRC_ENABLE_CAM04(0x%x)\n", readl(EXYNOS5430_SRC_ENABLE_CAM04));
	pr_info("EXYNOS5430_SRC_STAT_CAM00(0x%x)\n", readl(EXYNOS5430_SRC_STAT_CAM00));
	pr_info("EXYNOS5430_SRC_STAT_CAM01(0x%x)\n", readl(EXYNOS5430_SRC_STAT_CAM01));
	pr_info("EXYNOS5430_SRC_STAT_CAM02(0x%x)\n", readl(EXYNOS5430_SRC_STAT_CAM02));
	pr_info("EXYNOS5430_SRC_STAT_CAM03(0x%x)\n", readl(EXYNOS5430_SRC_STAT_CAM03));
	pr_info("EXYNOS5430_SRC_STAT_CAM04(0x%x)\n", readl(EXYNOS5430_SRC_STAT_CAM04));
	pr_info("EXYNOS5430_SRC_IGNORE_CAM01(0x%x)\n", readl(EXYNOS5430_SRC_IGNORE_CAM01));
	pr_info("EXYNOS5430_DIV_CAM00(0x%x)\n", readl(EXYNOS5430_DIV_CAM00));
	pr_info("EXYNOS5430_DIV_CAM01(0x%x)\n", readl(EXYNOS5430_DIV_CAM01));
	pr_info("EXYNOS5430_DIV_CAM02(0x%x)\n", readl(EXYNOS5430_DIV_CAM02));
	pr_info("EXYNOS5430_DIV_CAM03(0x%x)\n", readl(EXYNOS5430_DIV_CAM03));
	pr_info("EXYNOS5430_DIV_STAT_CAM00(0x%x)\n", readl(EXYNOS5430_DIV_STAT_CAM00));
	pr_info("EXYNOS5430_DIV_STAT_CAM01(0x%x)\n", readl(EXYNOS5430_DIV_STAT_CAM01));
	pr_info("EXYNOS5430_DIV_STAT_CAM02(0x%x)\n", readl(EXYNOS5430_DIV_STAT_CAM02));
	pr_info("EXYNOS5430_DIV_STAT_CAM03(0x%x)\n", readl(EXYNOS5430_DIV_STAT_CAM03));
	pr_info("EXYNOS5430_ENABLE_IP_CAM00(0x%x)\n", readl(EXYNOS5430_ENABLE_IP_CAM00));
	pr_info("EXYNOS5430_ENABLE_IP_CAM01(0x%x)\n", readl(EXYNOS5430_ENABLE_IP_CAM01));
	pr_info("EXYNOS5430_ENABLE_IP_CAM02(0x%x)\n", readl(EXYNOS5430_ENABLE_IP_CAM02));
	pr_info("EXYNOS5430_ENABLE_IP_CAM03(0x%x)\n", readl(EXYNOS5430_ENABLE_IP_CAM03));
	/* CMU_CAM1_DUMP */
	pr_info("EXYNOS5430_SRC_SEL_CAM10(0x%x)\n", readl(EXYNOS5430_SRC_SEL_CAM10));
	pr_info("EXYNOS5430_SRC_SEL_CAM11(0x%x)\n", readl(EXYNOS5430_SRC_SEL_CAM11));
	pr_info("EXYNOS5430_SRC_SEL_CAM12(0x%x)\n", readl(EXYNOS5430_SRC_SEL_CAM12));
	pr_info("EXYNOS5430_SRC_ENABLE_CAM10(0x%x)\n", readl(EXYNOS5430_SRC_ENABLE_CAM10));
	pr_info("EXYNOS5430_SRC_ENABLE_CAM11(0x%x)\n", readl(EXYNOS5430_SRC_ENABLE_CAM11));
	pr_info("EXYNOS5430_SRC_ENABLE_CAM12(0x%x)\n", readl(EXYNOS5430_SRC_ENABLE_CAM12));
	pr_info("EXYNOS5430_SRC_STAT_CAM10(0x%x)\n", readl(EXYNOS5430_SRC_STAT_CAM10));
	pr_info("EXYNOS5430_SRC_STAT_CAM11(0x%x)\n", readl(EXYNOS5430_SRC_STAT_CAM11));
	pr_info("EXYNOS5430_SRC_STAT_CAM12(0x%x)\n", readl(EXYNOS5430_SRC_STAT_CAM12));
	pr_info("EXYNOS5430_SRC_IGNORE_CAM11(0x%x)\n", readl(EXYNOS5430_SRC_IGNORE_CAM11));
	pr_info("EXYNOS5430_DIV_CAM10(0x%x)\n", readl(EXYNOS5430_DIV_CAM10));
	pr_info("EXYNOS5430_DIV_CAM11(0x%x)\n", readl(EXYNOS5430_DIV_CAM11));
	pr_info("EXYNOS5430_DIV_STAT_CAM10(0x%x)\n", readl(EXYNOS5430_DIV_STAT_CAM10));
	pr_info("EXYNOS5430_DIV_STAT_CAM11(0x%x)\n", readl(EXYNOS5430_DIV_STAT_CAM11));
	pr_info("EXYNOS5430_ENABLE_IP_CAM10(0x%x)\n", readl(EXYNOS5430_ENABLE_IP_CAM10));
	pr_info("EXYNOS5430_ENABLE_IP_CAM11(0x%x)\n", readl(EXYNOS5430_ENABLE_IP_CAM11));
	pr_info("EXYNOS5430_ENABLE_IP_CAM12(0x%x)\n", readl(EXYNOS5430_ENABLE_IP_CAM12));
	/* CMU_ISP_DUMP */
	pr_info("EXYNOS5430_SRC_SEL_ISP(0x%x)\n", readl(EXYNOS5430_SRC_SEL_ISP));
	pr_info("EXYNOS5430_SRC_ENABLE_ISP(0x%x)\n", readl(EXYNOS5430_SRC_ENABLE_ISP));
	pr_info("EXYNOS5430_SRC_STAT_ISP(0x%x)\n", readl(EXYNOS5430_SRC_STAT_ISP));
	pr_info("EXYNOS5430_DIV_ISP(0x%x)\n", readl(EXYNOS5430_DIV_ISP));
	pr_info("EXYNOS5430_DIV_STAT_ISP(0x%x)\n", readl(EXYNOS5430_DIV_STAT_ISP));
	pr_info("EXYNOS5430_ENABLE_ACLK_ISP0(0x%x)\n", readl(EXYNOS5430_ENABLE_ACLK_ISP0));
	pr_info("EXYNOS5430_ENABLE_ACLK_ISP1(0x%x)\n", readl(EXYNOS5430_ENABLE_ACLK_ISP1));
	pr_info("EXYNOS5430_ENABLE_ACLK_ISP2(0x%x)\n", readl(EXYNOS5430_ENABLE_ACLK_ISP2));
	pr_info("EXYNOS5430_ENABLE_PCLK_ISP(0x%x)\n", readl(EXYNOS5430_ENABLE_PCLK_ISP));
	pr_info("EXYNOS5430_ENABLE_SCLK_ISP(0x%x)\n", readl(EXYNOS5430_ENABLE_SCLK_ISP));
	pr_info("EXYNOS5430_ENABLE_IP_ISP0(0x%x)\n", readl(EXYNOS5430_ENABLE_IP_ISP0));
	pr_info("EXYNOS5430_ENABLE_IP_ISP1(0x%x)\n", readl(EXYNOS5430_ENABLE_IP_ISP1));
	pr_info("EXYNOS5430_ENABLE_IP_ISP2(0x%x)\n", readl(EXYNOS5430_ENABLE_IP_ISP2));
	pr_info("EXYNOS5430_ENABLE_IP_ISP3(0x%x)\n", readl(EXYNOS5430_ENABLE_IP_ISP3));
#endif

	return 0;
}

int cfg_clk_sclk(struct platform_device *pdev)
{
	/* SCLK_SPI0 */
	fimc_is_set_parent("mout_sclk_isp_spi0", "mout_bus_pll_user");
	fimc_is_set_rate("dout_sclk_isp_spi0_a", 100 * 1000000);
	fimc_is_set_rate("dout_sclk_isp_spi0_b", 100 * 1000000);
	fimc_is_get_rate("sclk_isp_spi0_top");
	fimc_is_set_parent("mout_sclk_isp_spi0_user", "sclk_isp_spi0_top");
	fimc_is_get_rate("sclk_isp_spi0");

	/* SCLK_SPI1 */
	fimc_is_set_parent("mout_sclk_isp_spi1", "mout_bus_pll_user");
	fimc_is_set_rate("dout_sclk_isp_spi1_a", 100 * 1000000);
	fimc_is_set_rate("dout_sclk_isp_spi1_b", 100 * 1000000);
	fimc_is_get_rate("sclk_isp_spi1_top");
	fimc_is_set_parent("mout_sclk_isp_spi1_user", "sclk_isp_spi1_top");
	fimc_is_get_rate("sclk_isp_spi1");

	/* SCLK_UART */
	fimc_is_set_parent("mout_sclk_isp_uart", "mout_bus_pll_user");
	fimc_is_set_rate("dout_sclk_isp_uart", 200 * 1000000);
	fimc_is_get_rate("sclk_isp_uart_top");
	fimc_is_set_parent("mout_sclk_isp_uart_user", "sclk_isp_uart_top");
	fimc_is_get_rate("sclk_isp_uart");

#ifdef EXYNOS5430_CMU_DUMP
	/* CMU_DUMP */
	pr_info("EXYNOS5430_SRC_SEL_TOP1(0x%x)\n", readl(EXYNOS5430_SRC_SEL_TOP1));
	pr_info("EXYNOS5430_SRC_SEL_TOP2(0x%x)\n", readl(EXYNOS5430_SRC_SEL_TOP2));
	pr_info("EXYNOS5430_SRC_SEL_TOP_CAM1(0x%x)\n", readl(EXYNOS5430_SRC_SEL_TOP_CAM1));
	pr_info("EXYNOS5430_SRC_ENABLE_TOP0(0x%x)\n", readl(EXYNOS5430_SRC_ENABLE_TOP0));
	pr_info("EXYNOS5430_SRC_ENABLE_TOP1(0x%x)\n", readl(EXYNOS5430_SRC_ENABLE_TOP1));
	pr_info("EXYNOS5430_SRC_ENABLE_TOP2(0x%x)\n", readl(EXYNOS5430_SRC_ENABLE_TOP2));
	pr_info("EXYNOS5430_SRC_ENABLE_TOP3(0x%x)\n", readl(EXYNOS5430_SRC_ENABLE_TOP3));
	pr_info("EXYNOS5430_SRC_ENABLE_TOP_CAM1(0x%x)\n", readl(EXYNOS5430_SRC_ENABLE_TOP_CAM1));
	pr_info("EXYNOS5430_DIV_TOP0(0x%x)\n", readl(EXYNOS5430_DIV_TOP0));
	pr_info("EXYNOS5430_DIV_TOP_CAM10(0x%x)\n", readl(EXYNOS5430_DIV_TOP_CAM10));
	pr_info("EXYNOS5430_DIV_TOP_CAM11(0x%x)\n", readl(EXYNOS5430_DIV_TOP_CAM11));
	pr_info("EXYNOS5430_ENABLE_IP_TOP(0x%x)\n", readl(EXYNOS5430_ENABLE_IP_TOP));
#endif

	return 0;
}

int cfg_clk_cam0(struct platform_device *pdev)
{
	/* CMU_TOP */
	fimc_is_get_rate("aclk_cam0_552");
	fimc_is_get_rate("aclk_cam0_400");
	fimc_is_get_rate("aclk_cam0_333");

	/* USER_MUX_SEL */
	fimc_is_set_parent("mout_aclk_cam0_552_user", "aclk_cam0_552");
	fimc_is_set_parent("mout_aclk_cam0_400_user", "aclk_cam0_400");
	fimc_is_set_parent("mout_aclk_cam0_333_user", "aclk_cam0_333");
	fimc_is_set_parent("mout_phyclk_rxbyteclkhs0_s4", "phyclk_rxbyteclkhs0_s4");
	fimc_is_set_parent("mout_phyclk_rxbyteclkhs0_s2a", "phyclk_rxbyteclkhs0_s2a");

	/* LITE A */
	fimc_is_set_parent("mout_aclk_lite_a_a", "mout_aclk_cam0_552_user");
	fimc_is_set_parent("mout_aclk_lite_a_b", "mout_aclk_lite_a_a");
	fimc_is_set_rate("dout_aclk_lite_a", 552 * 1000000);
	fimc_is_get_rate("dout_aclk_lite_a");
	fimc_is_set_rate("dout_pclk_lite_a", 276 * 1000000);
	fimc_is_get_rate("dout_pclk_lite_a");

	/* LITE B */
	fimc_is_set_parent("mout_aclk_lite_b_a", "mout_aclk_cam0_552_user");
	fimc_is_set_parent("mout_aclk_lite_b_b", "mout_aclk_lite_b_a");
	fimc_is_set_rate("dout_aclk_lite_b", 552 * 1000000);
	fimc_is_get_rate("dout_aclk_lite_b");
	fimc_is_set_rate("dout_pclk_lite_b", 276 * 1000000);
	fimc_is_get_rate("dout_pclk_lite_b");

	/* LITE D */
	fimc_is_set_parent("mout_aclk_lite_d_a", "mout_aclk_cam0_552_user");
	fimc_is_set_parent("mout_aclk_lite_d_b", "mout_aclk_lite_d_a");
	fimc_is_set_rate("dout_aclk_lite_d", 552 * 1000000);
	fimc_is_get_rate("dout_aclk_lite_d");
	fimc_is_set_rate("dout_pclk_lite_d", 276 * 1000000);
	fimc_is_get_rate("dout_pclk_lite_d");

	/* LITE C PIXELASYNC */
	fimc_is_set_parent("mout_sclk_pixelasync_lite_c_init_a", "mout_aclk_cam0_552_user");
	fimc_is_set_parent("mout_sclk_pixelasync_lite_c_init_b", "mout_sclk_pixelasync_lite_c_init_a");
	fimc_is_set_rate("dout_sclk_pixelasync_lite_c_init", 552 * 1000000);
	fimc_is_get_rate("dout_sclk_pixelasync_lite_c_init");
	fimc_is_set_rate("dout_pclk_pixelasync_lite_c", 276 * 1000000);
	fimc_is_get_rate("dout_pclk_pixelasync_lite_c");

	fimc_is_set_parent("mout_sclk_pixelasync_lite_c_a", "mout_aclk_cam0_552_user");
	fimc_is_set_parent("mout_sclk_pixelasync_lite_c_b", "mout_aclk_cam0_333_user");
	fimc_is_set_rate("dout_sclk_pixelasync_lite_c", 333 * 1000000);
	fimc_is_get_rate("dout_sclk_pixelasync_lite_c");

	/* 3AA 0 */
	fimc_is_set_parent("mout_aclk_3aa0_a", "mout_aclk_cam0_552_user");
	fimc_is_set_parent("mout_aclk_3aa0_b", "mout_aclk_3aa0_a");
	fimc_is_set_rate("dout_aclk_3aa0", 552 * 1000000);
	fimc_is_get_rate("dout_aclk_3aa0");
	fimc_is_set_rate("dout_pclk_3aa0", 276 * 1000000);
	fimc_is_get_rate("dout_pclk_3aa0");

	/* 3AA 0 */
	fimc_is_set_parent("mout_aclk_3aa1_a", "mout_aclk_cam0_552_user");
	fimc_is_set_parent("mout_aclk_3aa1_b", "mout_aclk_3aa1_a");
	fimc_is_set_rate("dout_aclk_3aa1", 552 * 1000000);
	fimc_is_get_rate("dout_aclk_3aa1");
	fimc_is_set_rate("dout_pclk_3aa1", 276 * 1000000);
	fimc_is_get_rate("dout_pclk_3aa1");

	/* CSI 0 */
	fimc_is_set_parent("mout_aclk_csis0_a", "mout_aclk_cam0_552_user");
	fimc_is_set_parent("mout_aclk_csis0_b", "mout_aclk_csis0_a");
	fimc_is_set_rate("dout_aclk_csis0", 552 * 1000000);
	fimc_is_get_rate("dout_aclk_csis0");

	/* CSI 1 */
	fimc_is_set_parent("mout_aclk_csis1_a", "mout_aclk_cam0_552_user");
	fimc_is_set_parent("mout_aclk_csis1_b", "mout_aclk_csis1_a");
	fimc_is_set_rate("dout_aclk_csis1", 552 * 1000000);
	fimc_is_get_rate("dout_aclk_csis1");

	/* CAM0 400 */
	fimc_is_set_parent("mout_aclk_cam0_400", "mout_aclk_cam0_400_user");
	fimc_is_set_rate("dout_aclk_cam0_400", 400 * 1000000);
	fimc_is_get_rate("dout_aclk_cam0_400");
	fimc_is_set_rate("dout_aclk_cam0_200", 200 * 1000000);
	fimc_is_get_rate("dout_aclk_cam0_200");
	fimc_is_set_rate("dout_pclk_cam0_50", 50 * 1000000);
	fimc_is_get_rate("dout_pclk_cam0_50");

#ifdef EXYNOS5430_CMU_DUMP
	/* CMU_DUMP */
	pr_info("EXYNOS5430_SRC_SEL_CAM00(0x%x)\n", readl(EXYNOS5430_SRC_SEL_CAM00));
	pr_info("EXYNOS5430_SRC_SEL_CAM01(0x%x)\n", readl(EXYNOS5430_SRC_SEL_CAM01));
	pr_info("EXYNOS5430_SRC_SEL_CAM02(0x%x)\n", readl(EXYNOS5430_SRC_SEL_CAM02));
	pr_info("EXYNOS5430_SRC_SEL_CAM03(0x%x)\n", readl(EXYNOS5430_SRC_SEL_CAM03));
	pr_info("EXYNOS5430_SRC_SEL_CAM04(0x%x)\n", readl(EXYNOS5430_SRC_SEL_CAM04));
	pr_info("EXYNOS5430_SRC_ENABLE_CAM00(0x%x)\n", readl(EXYNOS5430_SRC_ENABLE_CAM00));
	pr_info("EXYNOS5430_SRC_ENABLE_CAM01(0x%x)\n", readl(EXYNOS5430_SRC_ENABLE_CAM01));
	pr_info("EXYNOS5430_SRC_ENABLE_CAM02(0x%x)\n", readl(EXYNOS5430_SRC_ENABLE_CAM02));
	pr_info("EXYNOS5430_SRC_ENABLE_CAM03(0x%x)\n", readl(EXYNOS5430_SRC_ENABLE_CAM03));
	pr_info("EXYNOS5430_SRC_ENABLE_CAM04(0x%x)\n", readl(EXYNOS5430_SRC_ENABLE_CAM04));
	pr_info("EXYNOS5430_SRC_STAT_CAM00(0x%x)\n", readl(EXYNOS5430_SRC_STAT_CAM00));
	pr_info("EXYNOS5430_SRC_STAT_CAM01(0x%x)\n", readl(EXYNOS5430_SRC_STAT_CAM01));
	pr_info("EXYNOS5430_SRC_STAT_CAM02(0x%x)\n", readl(EXYNOS5430_SRC_STAT_CAM02));
	pr_info("EXYNOS5430_SRC_STAT_CAM03(0x%x)\n", readl(EXYNOS5430_SRC_STAT_CAM03));
	pr_info("EXYNOS5430_SRC_STAT_CAM04(0x%x)\n", readl(EXYNOS5430_SRC_STAT_CAM04));
	pr_info("EXYNOS5430_SRC_IGNORE_CAM01(0x%x)\n", readl(EXYNOS5430_SRC_IGNORE_CAM01));
	pr_info("EXYNOS5430_DIV_CAM00(0x%x)\n", readl(EXYNOS5430_DIV_CAM00));
	pr_info("EXYNOS5430_DIV_CAM01(0x%x)\n", readl(EXYNOS5430_DIV_CAM01));
	pr_info("EXYNOS5430_DIV_CAM02(0x%x)\n", readl(EXYNOS5430_DIV_CAM02));
	pr_info("EXYNOS5430_DIV_CAM03(0x%x)\n", readl(EXYNOS5430_DIV_CAM03));
	pr_info("EXYNOS5430_DIV_STAT_CAM00(0x%x)\n", readl(EXYNOS5430_DIV_STAT_CAM00));
	pr_info("EXYNOS5430_DIV_STAT_CAM01(0x%x)\n", readl(EXYNOS5430_DIV_STAT_CAM01));
	pr_info("EXYNOS5430_DIV_STAT_CAM02(0x%x)\n", readl(EXYNOS5430_DIV_STAT_CAM02));
	pr_info("EXYNOS5430_DIV_STAT_CAM03(0x%x)\n", readl(EXYNOS5430_DIV_STAT_CAM03));
	pr_info("EXYNOS5430_ENABLE_IP_CAM00(0x%x)\n", readl(EXYNOS5430_ENABLE_IP_CAM00));
	pr_info("EXYNOS5430_ENABLE_IP_CAM01(0x%x)\n", readl(EXYNOS5430_ENABLE_IP_CAM01));
	pr_info("EXYNOS5430_ENABLE_IP_CAM02(0x%x)\n", readl(EXYNOS5430_ENABLE_IP_CAM02));
	pr_info("EXYNOS5430_ENABLE_IP_CAM03(0x%x)\n", readl(EXYNOS5430_ENABLE_IP_CAM03));
#endif

	return 0;
}

int cfg_clk_cam1(struct platform_device *pdev)
{
	/* CMU_TOP */
	fimc_is_get_rate("aclk_cam1_552");
	fimc_is_get_rate("aclk_cam1_400");
	fimc_is_get_rate("aclk_cam1_333");

	/* USER_MUX_SEL */
	fimc_is_set_parent("mout_aclk_cam1_552_user", "aclk_cam1_552");
	fimc_is_set_parent("mout_aclk_cam1_400_user", "aclk_cam1_400");
	fimc_is_set_parent("mout_aclk_cam1_333_user", "aclk_cam1_333");

	/* C-A5 */
	fimc_is_set_rate("dout_atclk_cam1", 276 * 1000000);
	fimc_is_get_rate("dout_atclk_cam1");
	fimc_is_set_rate("dout_pclk_dbg_cam1", 138 * 1000000);
	fimc_is_get_rate("dout_pclk_dbg_cam1");

	/* LITE A */
	fimc_is_set_parent("mout_aclk_lite_c_a", "mout_aclk_cam1_400_user");
	fimc_is_set_parent("mout_aclk_lite_c_b", "mout_aclk_cam1_333_user");
	fimc_is_set_rate("dout_aclk_lite_c", 333 * 1000000);
	fimc_is_get_rate("dout_aclk_lite_c");
	fimc_is_set_rate("dout_pclk_lite_c", 166 * 1000000);
	fimc_is_get_rate("dout_pclk_lite_c");

	/* FD */
	fimc_is_set_parent("mout_aclk_fd_a", "mout_aclk_cam1_400_user");
	fimc_is_set_parent("mout_aclk_fd_b", "mout_aclk_fd_a");
	fimc_is_set_rate("dout_aclk_fd", 400 * 1000000);
	fimc_is_get_rate("dout_aclk_fd");
	fimc_is_set_rate("dout_pclk_fd", 200 * 1000000);
	fimc_is_get_rate("dout_pclk_fd");

	/* CSI 2 */
	fimc_is_set_parent("mout_aclk_csis2_a", "mout_aclk_cam1_400_user");
	fimc_is_set_parent("mout_aclk_csis2_b", "mout_aclk_cam1_333_user");
	fimc_is_set_rate("dout_aclk_csis2_a", 333 * 1000000);
	fimc_is_get_rate("dout_aclk_csis2_a");

#ifdef EXYNOS5430_CMU_DUMP
	/* CMU_DUMP */
	pr_info("EXYNOS5430_SRC_SEL_CAM10(0x%x)\n", readl(EXYNOS5430_SRC_SEL_CAM10));
	pr_info("EXYNOS5430_SRC_SEL_CAM11(0x%x)\n", readl(EXYNOS5430_SRC_SEL_CAM11));
	pr_info("EXYNOS5430_SRC_SEL_CAM12(0x%x)\n", readl(EXYNOS5430_SRC_SEL_CAM12));
	pr_info("EXYNOS5430_SRC_ENABLE_CAM10(0x%x)\n", readl(EXYNOS5430_SRC_ENABLE_CAM10));
	pr_info("EXYNOS5430_SRC_ENABLE_CAM11(0x%x)\n", readl(EXYNOS5430_SRC_ENABLE_CAM11));
	pr_info("EXYNOS5430_SRC_ENABLE_CAM12(0x%x)\n", readl(EXYNOS5430_SRC_ENABLE_CAM12));
	pr_info("EXYNOS5430_SRC_STAT_CAM10(0x%x)\n", readl(EXYNOS5430_SRC_STAT_CAM10));
	pr_info("EXYNOS5430_SRC_STAT_CAM11(0x%x)\n", readl(EXYNOS5430_SRC_STAT_CAM11));
	pr_info("EXYNOS5430_SRC_STAT_CAM12(0x%x)\n", readl(EXYNOS5430_SRC_STAT_CAM12));
	pr_info("EXYNOS5430_SRC_IGNORE_CAM11(0x%x)\n", readl(EXYNOS5430_SRC_IGNORE_CAM11));
	pr_info("EXYNOS5430_DIV_CAM10(0x%x)\n", readl(EXYNOS5430_DIV_CAM10));
	pr_info("EXYNOS5430_DIV_CAM11(0x%x)\n", readl(EXYNOS5430_DIV_CAM11));
	pr_info("EXYNOS5430_DIV_STAT_CAM10(0x%x)\n", readl(EXYNOS5430_DIV_STAT_CAM10));
	pr_info("EXYNOS5430_DIV_STAT_CAM11(0x%x)\n", readl(EXYNOS5430_DIV_STAT_CAM11));
	pr_info("EXYNOS5430_ENABLE_IP_CAM10(0x%x)\n", readl(EXYNOS5430_ENABLE_IP_CAM10));
	pr_info("EXYNOS5430_ENABLE_IP_CAM11(0x%x)\n", readl(EXYNOS5430_ENABLE_IP_CAM11));
	pr_info("EXYNOS5430_ENABLE_IP_CAM12(0x%x)\n", readl(EXYNOS5430_ENABLE_IP_CAM12));
#endif

	return 0;
}

int cfg_clk_isp(struct platform_device *pdev)
{
	/* CMU_TOP */
	fimc_is_get_rate("aclk_isp_400");
	fimc_is_get_rate("aclk_isp_dis_400");

	/* CMU_ISP */
	/* USER_MUX_SEL */
	fimc_is_set_parent("mout_aclk_isp_400_user", "aclk_isp_400");
	fimc_is_set_parent("mout_aclk_isp_dis_400_user", "aclk_isp_dis_400");
	/* ISP */
	fimc_is_set_rate("dout_aclk_isp_c_200", 200 * 1000000);
	fimc_is_get_rate("dout_aclk_isp_c_200");
	fimc_is_set_rate("dout_aclk_isp_d_200", 200 * 1000000);
	fimc_is_get_rate("dout_aclk_isp_d_200");
	fimc_is_set_rate("dout_pclk_isp", 80 * 1000000);
	fimc_is_get_rate("dout_pclk_isp");
	/* DIS */
	fimc_is_set_rate("dout_pclk_isp_dis", 200 * 1000000);
	fimc_is_get_rate("dout_pclk_isp_dis");

#ifdef EXYNOS5430_CMU_DUMP
	/* CMU_DUMP */
	pr_info("EXYNOS5430_SRC_SEL_ISP(0x%x)\n", readl(EXYNOS5430_SRC_SEL_ISP));
	pr_info("EXYNOS5430_SRC_ENABLE_ISP(0x%x)\n", readl(EXYNOS5430_SRC_ENABLE_ISP));
	pr_info("EXYNOS5430_SRC_STAT_ISP(0x%x)\n", readl(EXYNOS5430_SRC_STAT_ISP));
	pr_info("EXYNOS5430_DIV_ISP(0x%x)\n", readl(EXYNOS5430_DIV_ISP));
	pr_info("EXYNOS5430_DIV_STAT_ISP(0x%x)\n", readl(EXYNOS5430_DIV_STAT_ISP));
	pr_info("EXYNOS5430_ENABLE_ACLK_ISP0(0x%x)\n", readl(EXYNOS5430_ENABLE_ACLK_ISP0));
	pr_info("EXYNOS5430_ENABLE_ACLK_ISP1(0x%x)\n", readl(EXYNOS5430_ENABLE_ACLK_ISP1));
	pr_info("EXYNOS5430_ENABLE_ACLK_ISP2(0x%x)\n", readl(EXYNOS5430_ENABLE_ACLK_ISP2));
	pr_info("EXYNOS5430_ENABLE_PCLK_ISP(0x%x)\n", readl(EXYNOS5430_ENABLE_PCLK_ISP));
	pr_info("EXYNOS5430_ENABLE_SCLK_ISP(0x%x)\n", readl(EXYNOS5430_ENABLE_SCLK_ISP));
	pr_info("EXYNOS5430_ENABLE_IP_ISP0(0x%x)\n", readl(EXYNOS5430_ENABLE_IP_ISP0));
	pr_info("EXYNOS5430_ENABLE_IP_ISP1(0x%x)\n", readl(EXYNOS5430_ENABLE_IP_ISP1));
	pr_info("EXYNOS5430_ENABLE_IP_ISP2(0x%x)\n", readl(EXYNOS5430_ENABLE_IP_ISP2));
	pr_info("EXYNOS5430_ENABLE_IP_ISP3(0x%x)\n", readl(EXYNOS5430_ENABLE_IP_ISP3));
#endif

	return 0;
}

int exynos5430_fimc_is_cfg_clk(struct platform_device *pdev)
{
	pr_debug("%s\n", __func__);

	cfg_clk_div_max(pdev);

	/* initialize Clocks */
	cfg_clk_sclk(pdev);
	cfg_clk_cam0(pdev);
	cfg_clk_cam1(pdev);
	cfg_clk_isp(pdev);

	return 0;
}

int exynos5430_fimc_is_clk_on(struct platform_device *pdev)
{
	pr_debug("%s\n", __func__);

#ifdef EXYNOS5430_CMU_DUMP
	/* CMU_DUMP */
	pr_info("EXYNOS5430_ENABLE_IP_TOP(0x%x)\n", readl(EXYNOS5430_ENABLE_IP_TOP));
	pr_info("EXYNOS5430_ENABLE_IP_CAM00(0x%x)\n", readl(EXYNOS5430_ENABLE_IP_CAM00));
	pr_info("EXYNOS5430_ENABLE_IP_CAM01(0x%x)\n", readl(EXYNOS5430_ENABLE_IP_CAM01));
	pr_info("EXYNOS5430_ENABLE_IP_CAM02(0x%x)\n", readl(EXYNOS5430_ENABLE_IP_CAM02));
	pr_info("EXYNOS5430_ENABLE_IP_CAM03(0x%x)\n", readl(EXYNOS5430_ENABLE_IP_CAM03));
	pr_info("EXYNOS5430_ENABLE_IP_CAM10(0x%x)\n", readl(EXYNOS5430_ENABLE_IP_CAM10));
	pr_info("EXYNOS5430_ENABLE_IP_CAM11(0x%x)\n", readl(EXYNOS5430_ENABLE_IP_CAM11));
	pr_info("EXYNOS5430_ENABLE_IP_CAM12(0x%x)\n", readl(EXYNOS5430_ENABLE_IP_CAM12));
	pr_info("EXYNOS5430_ENABLE_IP_ISP0(0x%x)\n", readl(EXYNOS5430_ENABLE_IP_ISP0));
	pr_info("EXYNOS5430_ENABLE_IP_ISP1(0x%x)\n", readl(EXYNOS5430_ENABLE_IP_ISP1));
	pr_info("EXYNOS5430_ENABLE_IP_ISP2(0x%x)\n", readl(EXYNOS5430_ENABLE_IP_ISP2));
	pr_info("EXYNOS5430_ENABLE_IP_ISP3(0x%x)\n", readl(EXYNOS5430_ENABLE_IP_ISP3));
#endif


	return 0;
}

int exynos5430_fimc_is_clk_off(struct platform_device *pdev)
{
	pr_debug("%s\n", __func__);

	return 0;
}

int exynos5430_fimc_is_sensor_clk_on(struct platform_device *pdev, u32 source)
{
	char mux_name[30];
	char div_a_name[30];
	char div_b_name[30];
	char sclk_name[30];

	pr_debug("%s\n", __func__);

	snprintf(mux_name, sizeof(mux_name), "mout_sclk_isp_sensor%d", source);
	snprintf(div_a_name, sizeof(div_a_name), "dout_sclk_isp_sensor%d_a", source);
	snprintf(div_b_name, sizeof(div_b_name), "dout_sclk_isp_sensor%d_b", source);
	snprintf(sclk_name, sizeof(sclk_name), "sclk_isp_sensor%d", source);

	fimc_is_set_parent(mux_name, "oscclk");
	fimc_is_set_rate(div_a_name, 24 * 1000000);
	fimc_is_set_rate(div_b_name, 24 * 1000000);
	fimc_is_get_rate(sclk_name);

	return 0;
}

int exynos5430_fimc_is_sensor_clk_off(struct platform_device *pdev, u32 source)
{
	char mux_name[30];
	char div_a_name[30];
	char div_b_name[30];
	char sclk_name[30];

	pr_debug("%s\n", __func__);

	snprintf(mux_name, sizeof(mux_name), "mout_sclk_isp_sensor%d", source);
	snprintf(div_a_name, sizeof(div_a_name), "dout_sclk_isp_sensor%d_a", source);
	snprintf(div_b_name, sizeof(div_b_name), "dout_sclk_isp_sensor%d_b", source);
	snprintf(sclk_name, sizeof(sclk_name), "sclk_isp_sensor%d", source);

	fimc_is_set_parent(mux_name, "oscclk");
	fimc_is_set_rate(div_a_name, 1);
	fimc_is_set_rate(div_b_name, 1);
	fimc_is_get_rate(sclk_name);

	return 0;
}

/* sequence is important, don't change order */
int exynos5_fimc_is_sensor_power_on(struct platform_device *pdev, int sensor_id)
{
	pr_debug("%s\n", __func__);

	return 0;
}

/* sequence is important, don't change order */
int exynos5_fimc_is_sensor_power_off(struct platform_device *pdev, int sensor_id)
{
	pr_debug("%s\n", __func__);

	return 0;
}
#endif
