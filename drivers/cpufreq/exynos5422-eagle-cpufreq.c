/*
 * Copyright (c) 2013 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * EXYNOS5422 - EAGLE Core frequency scaling support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/cpufreq.h>
#include <linux/suspend.h>
#include <linux/clk-private.h>
#include <linux/pm_qos.h>

#include <mach/map.h>
#include <mach/regs-clock.h>
#include <mach/cpufreq.h>
#include <mach/asv-exynos.h>

#define CPUFREQ_LEVEL_END_CA15  (L22 + 1)
#define L2_LOCAL_PWR_EN     0x3

#undef PRINT_DIV_VAL

#define ENABLE_CLKOUT
#define SUPPORT_APLL_BYPASS

static int max_support_idx_CA15;
static int min_support_idx_CA15 = (CPUFREQ_LEVEL_END_CA15 - 1);
static struct pm_qos_request resume_max_cpu_qos;

static struct clk *dout_cpu;
static struct clk *mout_cpu;
static struct clk *mout_hpm_cpu;
static struct clk *mout_apll;
static struct clk *fout_apll;
static struct clk *mx_mspll_cpu;
static struct clk *fout_spll;

struct pm_qos_request exynos5_cpu_mif_qos;
static unsigned int exynos5422_volt_table_CA15[CPUFREQ_LEVEL_END_CA15];

static struct cpufreq_frequency_table exynos5422_freq_table_CA15[] = {
	{L0,  2400 * 1000},
	{L1,  2300 * 1000},
	{L2,  2200 * 1000},
	{L3,  2100 * 1000},
	{L4,  2000 * 1000},
	{L5,  1900 * 1000},
	{L6,  1800 * 1000},
	{L7,  1700 * 1000},
	{L8,  1600 * 1000},
	{L9,  1500 * 1000},
	{L10, 1400 * 1000},
	{L11, 1300 * 1000},
	{L12, 1200 * 1000},
	{L13, 1100 * 1000},
	{L14, 1000 * 1000},
	{L15,  900 * 1000},
	{L16,  800 * 1000},
	{L17,  700 * 1000},
	{L18,  600 * 1000},
	{L19,  500 * 1000},
	{L20,  400 * 1000},
	{L21,  300 * 1000},
	{L22,  200 * 1000},
	{0, CPUFREQ_TABLE_END},
};

static struct cpufreq_clkdiv exynos5422_clkdiv_table_CA15[CPUFREQ_LEVEL_END_CA15];
static unsigned int clkdiv_cpu0_5422_CA15[CPUFREQ_LEVEL_END_CA15][7] = {
	/*
	 * Clock divider value for following
	 * { CPUD, ATB, PCLK_DBG, APLL, ARM2}
	 */
	/* ARM L0: 2.4GHz */
	{ 2, 7, 7, 3, 0 },

	/* ARM L1: 2.3GHz */
	{ 2, 7, 7, 3, 0 },

	/* ARM L2: 2.2GHz */
	{ 2, 7, 7, 3, 0 },

	/* ARM L3: 2.1GHz */
	{ 2, 7, 7, 3, 0 },

	/* ARM L4: 2.0GHz */
	{ 2, 7, 7, 3, 0 },

	/* ARM L5: 1.9GHz */
	{ 2, 7, 7, 3, 0 },

	/* ARM L6: 1.8GHz */
	{ 2, 7, 7, 3, 0 },

	/* ARM L7: 1.7GHz */
	{ 2, 7, 7, 3, 0 },

	/* ARM L8: 1.6GHz */
	{ 2, 7, 7, 3, 0 },

	/* ARM L9: 1.5GHz */
	{ 2, 7, 7, 3, 0 },

	/* ARM L10: 1.4GHz */
	{ 2, 7, 7, 3, 0 },

	/* ARM L11: 1.3GHz */
	{ 2, 7, 7, 3, 0 },

	/* ARM L12: 1.2GHz */
	{ 2, 7, 7, 3, 0 },

	/* ARM L13: 1.1GHz */
	{ 2, 7, 7, 3, 0 },

	/* ARM L14: 1000MHz */
	{ 2, 6, 6, 3, 0 },

	/* ARM L15: 900MHz */
	{ 2, 6, 6, 3, 0 },

	/* ARM L16: 800MHz */
	{ 2, 5, 5, 3, 0 },

	/* ARM L17: 700MHz */
	{ 2, 5, 5, 3, 0 },

	/* ARM L18: 600MHz */
	{ 2, 4, 4, 3, 0 },

	/* ARM L19: 500MHz */
	{ 2, 3, 3, 3, 0 },

	/* ARM L20: 400MHz */
	{ 2, 3, 3, 3, 0 },

	/* ARM L21: 300MHz */
	{ 2, 3, 3, 3, 0 },

	/* ARM L22: 200MHz */
	{ 2, 3, 3, 3, 0 },
};

static unsigned int clkdiv_cpu1_5422_CA15[CPUFREQ_LEVEL_END_CA15][2] = {
	/*
	 * Clock divider value for following
	 * { copy, HPM }
	 */

	/* ARM L0: 2.4GHz */
	{ 7, 7 },

	/* ARM L1: 2.3GHz */
	{ 7, 7 },

	/* ARM L2: 2.2GHz */
	{ 7, 7 },

	/* ARM L3: 2.1GHz */
	{ 7, 7 },

	/* ARM L4: 2.0GHz */
	{ 7, 7 },

	/* ARM L5: 1.9GHz */
	{ 7, 7 },

	/* ARM L6: 1.8GHz */
	{ 7, 7 },

	/* ARM L7: 1.7GHz */
	{ 7, 7 },

	/* ARM L8: 1.6GHz */
	{ 7, 7 },

	/* ARM L9: 1.5GHz */
	{ 7, 7 },

	/* ARM L10: 1.4GHz */
	{ 7, 7 },

	/* ARM L11: 1.3GHz */
	{ 7, 7 },

	/* ARM L12: 1.2GHz */
	{ 7, 7 },

	/* ARM L13: 1.1GHz */
	{ 7, 7 },

	/* ARM L14: 1000MHz */
	{ 7, 7 },

	/* ARM L15: 900MHz */
	{ 7, 7 },

	/* ARM L16: 800MHz */
	{ 7, 7 },

	/* ARM L17: 700MHz */
	{ 7, 7 },

	/* ARM L18: 600MHz */
	{ 7, 7 },

	/* ARM L19: 500MHz */
	{ 7, 7 },

	/* ARM L20: 400MHz */
	{ 7, 7 },

	/* ARM L21: 300MHz */
	{ 7, 7 },

	/* ARM L22: 200MHz */
	{ 7, 7 },
};

static unsigned int exynos5422_apll_pms_table_CA15[CPUFREQ_LEVEL_END_CA15] = {
	/*PLL2450X_PMS(400, 6, 3),*/
	/* APLL FOUT L0: 2.4GHz */
	((200 << 16) | (2 << 8) | (0x0)),

	/* APLL FOUT L1: 2.3GHz */
	((575 << 16) | (6 << 8) | (0x0)),

	/* APLL FOUT L2: 2.2GHz */
	((275 << 16) | (3 << 8) | (0x0)),

	/* APLL FOUT L3: 2.1GHz */
	((175 << 16) | (2 << 8) | (0x0)),

	/* APLL FOUT L4: 2.0GHz */
	((250 << 16) | (3 << 8) | (0x0)),

	/* APLL FOUT L5: 1.9GHz */
	((475 << 16) | (6 << 8) | (0x0)),

	/* APLL FOUT L6: 1.8GHz */
	((225 << 16) | (3 << 8) | (0x0)),

	/* APLL FOUT L7: 1.7GHz */
	((425 << 16) | (6 << 8) | (0x0)),

	/* APLL FOUT L8: 1.6GHz */
	((200 << 16) | (3 << 8) | (0x0)),

	/* APLL FOUT L9: 1.5GHz */
	((250 << 16) | (4 << 8) | (0x0)),

	/* APLL FOUT L10: 1.4GHz */
	((175 << 16) | (3 << 8) | (0x0)),

	/* APLL FOUT L11: 1.3GHz */
	((325 << 16) | (6 << 8) | (0x0)),

	/* APLL FOUT L12: 1.2GHz */
	((200 << 16) | (2 << 8) | (0x1)),

	/* APLL FOUT L13: 1.1GHz */
	((275 << 16) | (3 << 8) | (0x1)),

	/* APLL FOUT L14: 1000MHz */
	((250 << 16) | (3 << 8) | (0x1)),

	/* APLL FOUT L15: 900MHz */
	((150 << 16) | (2 << 8) | (0x1)),

	/* APLL FOUT L16: 800MHz */
	((200 << 16) | (3 << 8) | (0x1)),

	/* APLL FOUT L17: 700MHz */
	((175 << 16) | (3 << 8) | (0x1)),

	/* APLL FOUT L18: 600MHz */
	((100 << 16) | (2 << 8) | (0x1)),
	/* APLL FOUT L19: 500MHz */
	((250 << 16) | (3 << 8) | (0x2)),

	/* APLL FOUT L20: 400MHz */
	((200 << 16) | (3 << 8) | (0x2)),

	/* APLL FOUT L21: 300MHz */
	((100 << 16) | (2 << 8) | (0x2)),

	/* APLL FOUT L22: 200MHz */
	((200 << 16) | (3 << 8) | (0x3)),
};

/*
 * ASV group voltage table
 */
static const unsigned int asv_voltage_5422_CA15[CPUFREQ_LEVEL_END_CA15] = {
	1200000,    /* L0  2400 */
	1200000,    /* L1  2300 */
	1200000,    /* L2  2200 */
	1200000,    /* L3  2100 */
	1200000,    /* L4  2000 */
	1200000,    /* L5  1900 */
	1200000,    /* L6  1800 */
	1200000,    /* L7  1700 */
	1200000,    /* L8  1600 */
	1100000,    /* L9  1500 */
	1100000,    /* L10 1400 */
	1100000,    /* L11 1300 */
	1000000,    /* L12 1200 */
	1000000,    /* L13 1100 */
	1000000,    /* L14 1000 */
	1000000,    /* L15  900 */
	 900000,    /* L16  800 */
	 900000,    /* L17  700 */
	 900000,    /* L18  600 */
	 900000,    /* L19  500 */
	 900000,    /* L20  400 */
	 900000,    /* L22  300 */
	 900000,    /* L22  200 */
};

#if defined(CONFIG_ARM_EXYNOS5422_BUS_DEVFREQ)
/* Minimum memory throughput in megabytes per second */
static int exynos5422_bus_table_CA15[CPUFREQ_LEVEL_END_CA15] = {
	800000, /* 2.4 GHz */
	800000, /* 2.3 GHz */
	800000, /* 2.2 GHz */
	800000, /* 2.1 GHz */
	800000, /* 2.0 GHz */
	733000, /* 1.9 GHz */
	733000, /* 1.8 GHz */
	733000, /* 1.7 MHz */
	733000, /* 1.6 GHz */
	667000, /* 1.5 GHz */
	667000, /* 1.4 GHz */
	667000, /* 1.3 GHz */
	667000, /* 1.2 GHz */
	533000, /* 1.1 GHz */
	533000, /* 1.0 GHz */
	400000, /* 900 MHz */
	400000, /* 800 MHz */
	400000, /* 700 MHz */
	400000, /* 600 MHz */
	400000, /* 500 MHz */
	400000, /* 400 MHz */
	400000, /* 300 MHz */
	400000, /* 200 MHz */
};
#endif

static void exynos5422_set_clkdiv_CA15(unsigned int div_index)
{
	unsigned int tmp;

	/* Change Divider - CPU0 */

	tmp = exynos5422_clkdiv_table_CA15[div_index].clkdiv0;

	__raw_writel(tmp, EXYNOS5_CLKDIV_CPU0);
	do {
		cpu_relax();
		tmp = __raw_readl(EXYNOS5_CLKDIV_STATCPU0);
	} while (tmp & 0x11111111);

#ifdef PRINT_DIV_VAL
	tmp = __raw_readl(EXYNOS5_CLKDIV_CPU0);
	pr_info("DIV_CPU0[0x%x]\n", tmp);
#endif

	/* Change Divider - CPU1 */
	tmp = exynos5422_clkdiv_table_CA15[div_index].clkdiv1;

	__raw_writel(tmp, EXYNOS5_CLKDIV_CPU1);

	do {
		cpu_relax();
		tmp = __raw_readl(EXYNOS5_CLKDIV_STATCPU1);
	} while (tmp & 0x111);

#ifdef PRINT_DIV_VAL
	tmp = __raw_readl(EXYNOS5_CLKDIV_CPU1);
	pr_info("DIV_CPU1[0x%x]\n", tmp);
#endif
}

#define CLK_ENA(a) clk_prepare_enable(a)
#define CLK_DIS(a) clk_disable_unprepare(a)
#define USING_CCF
static void exynos5422_set_egl_pll_CA15(unsigned int new_index, unsigned int old_index)
{
	unsigned int tmp, pdiv;

	CLK_ENA(fout_spll);
	/* 1. CLKMUX_CPU = MX_MSPLL_CPU, ARMCLK uses MX_MSPLL_CPU for lock time */
	if (clk_set_parent(mout_cpu, mx_mspll_cpu))
		pr_err(KERN_ERR "Unable to set parent %s of clock %s.\n",
			mx_mspll_cpu->name, mout_cpu->name);
	/* 1.1 CLKMUX_HPM = MX_MSPLL_CPU, CLKHPM uses MX_MSPLL_KFC for lock time */
	if (clk_set_parent(mout_hpm_cpu, mx_mspll_cpu))
		pr_err(KERN_ERR "Unable to set parent %s of clock %s.\n",
			mx_mspll_cpu->name, mout_hpm_cpu->name);

	do {
		cpu_relax();
		tmp = (__raw_readl(EXYNOS5_CLKMUX_STATCPU)
		>> EXYNOS5_CLKSRC_CPU_MUXCORE_SHIFT);
		tmp &= 0x7;
	} while (tmp != 0x2);
#ifndef USING_CCF
	/* 2. Set APLL Lock time */
	pdiv = ((exynos5422_apll_pms_table_CA15[new_index] >> 8) & 0x3f);

	__raw_writel((pdiv * 250), EXYNOS5_APLL_LOCK);

	/* 3. Change PLL PMS values */
	tmp = __raw_readl(EXYNOS5_APLL_CON0);
	tmp &= ~((0x3ff << 16) | (0x3f << 8) | (0x7 << 0));
	tmp |= exynos5422_apll_pms_table_CA15[new_index];
	__raw_writel(tmp, EXYNOS5_APLL_CON0);

	/* 4. wait_lock_time */
	do {
		cpu_relax();
		tmp = __raw_readl(EXYNOS5_APLL_CON0);
	} while (!(tmp & (0x1 << EXYNOS5_APLLCON0_LOCKED_SHIFT)));
#else
	pdiv = 0;
	clk_set_rate(fout_apll, exynos5422_freq_table_CA15[new_index].frequency*1000);
	pr_debug("apll set_rate:%ld\n", clk_get_rate(fout_apll));
#endif
	/* 5. MUX_CORE_SEL = APLL */
	if (clk_set_parent(mout_cpu, mout_apll))
		pr_err("Unable to set parent %s of clock %s.\n",
			mout_apll->name, mout_cpu->name);
	/* 5..1 CLKMUX_HPM = APLL */
	if (clk_set_parent(mout_hpm_cpu, mout_apll))
		pr_err("Unable to set parent %s of clock %s.\n",
			mout_apll->name, mout_hpm_cpu->name);

	do {
		cpu_relax();
		tmp = __raw_readl(EXYNOS5_CLKMUX_STATCPU);
		tmp &= EXYNOS5_CLKMUX_STATCPU_MUXCORE_MASK;
	} while (tmp != (0x1 << EXYNOS5_CLKSRC_CPU_MUXCORE_SHIFT));
	CLK_DIS(fout_spll);
}

static bool exynos5422_pms_change_CA15(unsigned int old_index,
				      unsigned int new_index)
{
	unsigned int old_pm = (exynos5422_apll_pms_table_CA15[old_index] >> 8);
	unsigned int new_pm = (exynos5422_apll_pms_table_CA15[new_index] >> 8);

	return (old_pm == new_pm) ? 0 : 1;
}

static void exynos5422_set_frequency_CA15(unsigned int old_index,
					 unsigned int new_index)
{
	unsigned int tmp;

	if (old_index > new_index) {
		if (!exynos5422_pms_change_CA15(old_index, new_index)) {
			/* 1. Change the system clock divider values */
			exynos5422_set_clkdiv_CA15(new_index);
			/* 2. Change just s value in apll m,p,s value */
			tmp = __raw_readl(EXYNOS5_APLL_CON0);
			tmp &= ~(0x7 << 0);
			tmp |= (exynos5422_apll_pms_table_CA15[new_index] & 0x7);
			__raw_writel(tmp, EXYNOS5_APLL_CON0);

		} else {
			/* Clock Configuration Procedure */
			/* 1. Change the system clock divider values */
			exynos5422_set_clkdiv_CA15(new_index);
			/* 2. Change the apll m,p,s value */
			exynos5422_set_egl_pll_CA15(new_index, old_index);
		}
	} else if (old_index < new_index) {
		if (!exynos5422_pms_change_CA15(old_index, new_index)) {
			/* 1. Change just s value in apll m,p,s value */
			tmp = __raw_readl(EXYNOS5_APLL_CON0);
			tmp &= ~(0x7 << 0);
			tmp |= (exynos5422_apll_pms_table_CA15[new_index] & 0x7);
			__raw_writel(tmp, EXYNOS5_APLL_CON0);
			/* 2. Change the system clock divider values */
			exynos5422_set_clkdiv_CA15(new_index);
		} else {
			/* Clock Configuration Procedure */
			/* 1. Change the apll m,p,s value */
			exynos5422_set_egl_pll_CA15(new_index, old_index);
			/* 2. Change the system clock divider values */
			exynos5422_set_clkdiv_CA15(new_index);
		}
	}

#if defined(CONFIG_ARM_EXYNOS5422_BUS_DEVFREQ)
	if (old_index >= new_index) {
		if (pm_qos_request_active(&exynos5_cpu_mif_qos))
			pm_qos_update_request(&exynos5_cpu_mif_qos,
				exynos5422_bus_table_CA15[new_index]);
	}
#endif

	pr_debug("post clk [%ld]\n", clk_get_rate(dout_cpu));

#if defined(CONFIG_ARM_EXYNOS5422_BUS_DEVFREQ)
	if (old_index < new_index) {
		if (pm_qos_request_active(&exynos5_cpu_mif_qos))
			pm_qos_update_request(&exynos5_cpu_mif_qos,
				exynos5422_bus_table_CA15[new_index]);
	}
#endif
}

static void __init set_volt_table_CA15(void)
{
	unsigned int i;
	unsigned int asv_volt __maybe_unused;

	for (i = 0; i < CPUFREQ_LEVEL_END_CA15; i++) {
		/* FIXME: need to update voltage table for REV1 */
		asv_volt = get_match_volt(ID_ARM, exynos5422_freq_table_CA15[i].frequency);

		if (!asv_volt)
			exynos5422_volt_table_CA15[i] = asv_voltage_5422_CA15[i];
		else
			exynos5422_volt_table_CA15[i] = asv_volt;

		pr_info("CPUFREQ of CA15 L%d : %d uV\n", i,
		exynos5422_volt_table_CA15[i]);
	}

#ifdef CONFIG_EXYNOS5_MAX_CPU_HOTPLUG
	max_support_idx_CA15 = L3;
#else
	max_support_idx_CA15 = L5;
#endif
	min_support_idx_CA15 = L16;
}

static bool exynos5422_is_alive_CA15(void)
{
	unsigned int tmp = true;
	tmp = __raw_readl(EXYNOS5_ARM_L2_SYS_PWR_REG) & L2_LOCAL_PWR_EN;

	return tmp ? true : false;
}

#ifdef CONFIG_PM
static int exynos_cpufreq_cpu_pm_notifier(struct notifier_block *notifier,
		unsigned long pm_event, void *v)
{
	switch (pm_event) {
	case PM_SUSPEND_PREPARE:
		break;
	case PM_POST_SUSPEND:
		pm_qos_update_request_timeout(&resume_max_cpu_qos, 1000 * 1000, 5000 * 1000);
		break;
	}
	return NOTIFY_OK;
}

static struct notifier_block exynos_cpufreq_cpu_nb = {
	.notifier_call = exynos_cpufreq_cpu_pm_notifier,
	.priority = 1,
};
#endif

int __init exynos5_cpufreq_CA15_init(struct exynos_dvfs_info *info)
{
	int i;
	unsigned int tmp;
	unsigned long rate;

	set_volt_table_CA15();

	dout_cpu = __clk_lookup("dout_arm2_lsb");
	if (IS_ERR(dout_cpu))
		goto err_get_clock;

	mout_cpu = __clk_lookup("mout_cpu");
	if (IS_ERR(mout_cpu))
		goto err_get_clock;

	mout_hpm_cpu = __clk_lookup("mout_hpm");
	if (IS_ERR(mout_hpm_cpu))
		goto err_get_clock;

	mx_mspll_cpu = __clk_lookup("mout_mx_mspll_cpu");
	if (IS_ERR(mx_mspll_cpu))
		goto err_get_clock;

	mout_apll = __clk_lookup("mout_apll_ctrl");
	if (IS_ERR(mout_apll))
		goto err_get_clock;

	fout_apll = __clk_lookup("fout_apll");
	if (IS_ERR(fout_apll))
		goto err_get_clock;

	fout_spll = __clk_lookup("fout_spll");
	if (IS_ERR(fout_spll))
		goto err_get_clock;

	clk_set_parent(mout_apll, fout_apll);
	clk_set_parent(mout_cpu, mout_apll);
	clk_set_parent(mout_hpm_cpu, mout_apll);

	rate = clk_get_rate(mx_mspll_cpu) / 1000;
	pr_info("eagle clock[%ld], mpll clock[%ld]\n", clk_get_rate(dout_cpu) / 1000, rate);

	for (i = L0; i < CPUFREQ_LEVEL_END_CA15; i++) {
		exynos5422_clkdiv_table_CA15[i].index = i;

		tmp = __raw_readl(EXYNOS5_CLKDIV_CPU0);

		tmp &= ~(EXYNOS5_CLKDIV_CPU0_CPUD_MASK |
		EXYNOS5_CLKDIV_CPU0_ATB_MASK |
		EXYNOS5_CLKDIV_CPU0_PCLKDBG_MASK |
		EXYNOS5_CLKDIV_CPU0_APLL_MASK |
		EXYNOS5_CLKDIV_CPU0_CORE2_MASK);

		tmp |= ((clkdiv_cpu0_5422_CA15[i][0] << EXYNOS5_CLKDIV_CPU0_CPUD_SHIFT) |
		(clkdiv_cpu0_5422_CA15[i][1] << EXYNOS5_CLKDIV_CPU0_ATB_SHIFT) |
		(clkdiv_cpu0_5422_CA15[i][2] << EXYNOS5_CLKDIV_CPU0_PCLKDBG_SHIFT) |
		(clkdiv_cpu0_5422_CA15[i][3] << EXYNOS5_CLKDIV_CPU0_APLL_SHIFT) |
		(clkdiv_cpu0_5422_CA15[i][4] << EXYNOS5_CLKDIV_CPU0_CORE2_SHIFT));

		exynos5422_clkdiv_table_CA15[i].clkdiv0 = tmp;

		tmp = __raw_readl(EXYNOS5_CLKDIV_CPU1);

		tmp &= ~(EXYNOS5_CLKDIV_CPU1_COPY_MASK |
		EXYNOS5_CLKDIV_CPU1_HPM_MASK);
		tmp |= ((clkdiv_cpu1_5422_CA15[i][0] << EXYNOS5_CLKDIV_CPU1_COPY_SHIFT) |
		(clkdiv_cpu1_5422_CA15[i][1] << EXYNOS5_CLKDIV_CPU1_HPM_SHIFT));

		exynos5422_clkdiv_table_CA15[i].clkdiv1 = tmp;
	}

	info->mpll_freq_khz = rate;
	/*info->pm_lock_idx = L0;*/
	info->pll_safe_idx = L12;
	info->max_support_idx = max_support_idx_CA15;
	info->min_support_idx = min_support_idx_CA15;
	info->cpu_clk = fout_apll;
	/* info->max_op_freqs = exynos5422_max_op_freq_b_evt0;*/

	info->volt_table = exynos5422_volt_table_CA15;
	info->freq_table = exynos5422_freq_table_CA15;
	info->set_freq = exynos5422_set_frequency_CA15;
	info->need_apll_change = exynos5422_pms_change_CA15;
	info->is_alive = exynos5422_is_alive_CA15;

#ifdef CONFIG_PM
	register_pm_notifier(&exynos_cpufreq_cpu_nb);

	pm_qos_add_request(&resume_max_cpu_qos, PM_QOS_CPU_FREQ_MAX,
			PM_QOS_CPU_FREQ_MAX_DEFAULT_VALUE);
#endif

#ifdef ENABLE_CLKOUT
	tmp = __raw_readl(EXYNOS5_CLKOUT_CMU_TOP);
	tmp &= ~0xffff;
	tmp |= 0x1904;
	__raw_writel(tmp, EXYNOS5_CLKOUT_CMU_TOP);
#endif

	return 0;

err_get_clock:

	pr_err("%s: failed initialization\n", __func__);
	return -EINVAL;
}
