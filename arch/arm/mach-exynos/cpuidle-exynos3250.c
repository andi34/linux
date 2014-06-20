/* linux/arch/arm/mach-exynos/cpuidle-exynos3250.c
 *
 * Copyright (c) 2014 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/cpuidle.h>
#include <linux/cpu_pm.h>
#include <linux/io.h>
#include <linux/suspend.h>
#include <linux/smp.h>

#include <asm/suspend.h>

#include <mach/regs-pmu.h>
#include <mach/regs-clock.h>
#include <mach/pmu.h>
#include <mach/smc.h>

#include <plat/pm.h>
#include <plat/cpu.h>

#ifdef CONFIG_ARM_TRUSTZONE
#define REG_DIRECTGO_ADDR	(S5P_VA_SYSRAM_NS + 0x24)
#define REG_DIRECTGO_FLAG	(S5P_VA_SYSRAM_NS + 0x20)
#else
#define REG_DIRECTGO_ADDR	EXYNOS_INFORM0
#define REG_DIRECTGO_FLAG	EXYNOS_INFORM1
#endif

#define EXYNOS_CHECK_DIRECTGO	0xFCBA0D10

static int exynos_enter_idle(struct cpuidle_device *dev,
			struct cpuidle_driver *drv,
			      int index);
static int exynos_enter_lowpower(struct cpuidle_device *dev,
				struct cpuidle_driver *drv,
				int index);

static struct cpuidle_state exynos_cpuidle_set[] __initdata = {
	[0] = {
		.enter			= exynos_enter_idle,
		.exit_latency		= 1,
		.target_residency	= 1000,
		.flags			= CPUIDLE_FLAG_TIME_VALID,
		.name			= "C1",
		.desc			= "ARM clock gating(WFI)",
	},
	[1] = {
		.enter                  = exynos_enter_lowpower,
		.exit_latency           = 300,
		.target_residency       = 5000,
		.flags                  = CPUIDLE_FLAG_TIME_VALID,
		.name                   = "C3",
		.desc                   = "ARM power down",
	},
};

static DEFINE_PER_CPU(struct cpuidle_device, exynos_cpuidle_device);

static struct cpuidle_driver exynos_idle_driver = {
	.name		= "exynos_idle",
	.owner		= THIS_MODULE,
};

#ifdef CONFIG_EXYNOS_IDLE_CLOCK_DOWN
static void exynos3250_enable_idle_clock_down(void)
{
	unsigned int tmp;

	tmp = __raw_readl(EXYNOS3_PWR_CTRL);
	tmp &= ~((0x7 << 28) | (0x7 << 16) | (1 << 9) | (1 << 8));
	tmp |= (0x7 << 28) | (0x7 << 16) | 0x3ff;
	__raw_writel(tmp, EXYNOS3_PWR_CTRL);

	tmp = __raw_readl(EXYNOS3_PWR_CTRL2);
	tmp &= ~((0x3 << 24) | (0xffff << 8) | (0x77));
	tmp |= (100 << 16) | (10 << 8) | (1 << 4) | (0 << 0);
	tmp |= (1 << 25) | (1 << 24);
	__raw_writel(tmp, EXYNOS3_PWR_CTRL2);
}

static void exynos3250_disable_idle_clock_down(void)
{
	unsigned int tmp;

	tmp = __raw_readl(EXYNOS3_PWR_CTRL);
	tmp &= ~(0x3 << 8);
	__raw_writel(tmp, EXYNOS3_PWR_CTRL);

	tmp = __raw_readl(EXYNOS3_PWR_CTRL2);
	tmp &= ~(0x3 << 24);
	__raw_writel(tmp, EXYNOS3_PWR_CTRL2);
}
#else
static void exynos3250_enable_idle_clock_down(void) { }
static void exynos3250_disable_idle_clock_down(void) { }
#endif

/* Ext-GIC nIRQ/nFIQ is the only wakeup source in AFTR */
static void exynos_set_wakeupmask(void)
{
	unsigned int origin = __raw_readl(EXYNOS_WAKEUP_MASK);

	origin = (origin & ~((0x1<<14)|(0x3<<9)|(0x1<<5)|(0x3<<1))) | (0x1 << 30);
	__raw_writel(origin, EXYNOS_WAKEUP_MASK);
	__raw_writel(0x0, EXYNOS_WAKEUP_MASK2);
}

#if !defined(CONFIG_ARM_TRUSTZONE)
static unsigned int g_pwr_ctrl, g_diag_reg;

static void save_cpu_arch_register(void)
{
	/*read power control register*/
	asm("mrc p15, 0, %0, c15, c0, 0" : "=r"(g_pwr_ctrl) : : "cc");
	/*read diagnostic register*/
	asm("mrc p15, 0, %0, c15, c0, 1" : "=r"(g_diag_reg) : : "cc");
	return;
}

static void restore_cpu_arch_register(void)
{
	/*write power control register*/
	asm("mcr p15, 0, %0, c15, c0, 0" : : "r"(g_pwr_ctrl) : "cc");
	/*write diagnostic register*/
	asm("mcr p15, 0, %0, c15, c0, 1" : : "r"(g_diag_reg) : "cc");
	return;
}
#else
static void save_cpu_arch_register(void) { }
static void restore_cpu_arch_register(void) { }
#endif

static int idle_finisher(unsigned long flags)
{
#if defined(CONFIG_ARM_TRUSTZONE)
	exynos_smc(SMC_CMD_SAVE, OP_TYPE_CORE, SMC_POWERSTATE_IDLE, 0);
	exynos_smc(SMC_CMD_SHUTDOWN, OP_TYPE_CLUSTER, SMC_POWERSTATE_IDLE, 0);
#else
	cpu_do_idle();
#endif
	return 1;
}

static int exynos_enter_core0_aftr(struct cpuidle_device *dev,
				struct cpuidle_driver *drv,
				int index)
{
	unsigned long tmp;
	unsigned int ret = 0;
	unsigned int cpuid = smp_processor_id();

	exynos_set_wakeupmask();

	exynos3250_disable_idle_clock_down();

	/* Set value of power down register for aftr mode */
	exynos_sys_powerdown_conf(SYS_AFTR);

	__raw_writel(virt_to_phys(s3c_cpu_resume), REG_DIRECTGO_ADDR);
	__raw_writel(EXYNOS_CHECK_DIRECTGO, REG_DIRECTGO_FLAG);

	save_cpu_arch_register();

	/* Setting Central Sequence Register for power down mode */
	tmp = __raw_readl(EXYNOS_CENTRAL_SEQ_CONFIGURATION);
	tmp &= ~EXYNOS_CENTRAL_LOWPWR_CFG;
	__raw_writel(tmp, EXYNOS_CENTRAL_SEQ_CONFIGURATION);

	set_boot_flag(cpuid, C2_STATE);

	cpu_pm_enter();

	ret = cpu_suspend(0, idle_finisher);
	if (ret) {
		tmp = __raw_readl(EXYNOS_CENTRAL_SEQ_CONFIGURATION);
		tmp |= EXYNOS_CENTRAL_LOWPWR_CFG;
		__raw_writel(tmp, EXYNOS_CENTRAL_SEQ_CONFIGURATION);
	}

	clear_boot_flag(cpuid, C2_STATE);

	cpu_pm_exit();

	restore_cpu_arch_register();

	exynos3250_enable_idle_clock_down();

	/* Clear wakeup state register */
	__raw_writel(0x0, EXYNOS_WAKEUP_STAT);

	return index;
}

static int exynos_enter_idle(struct cpuidle_device *dev,
				struct cpuidle_driver *drv,
				int index)
{
	cpu_do_idle();
	return index;
}

static int exynos_enter_lowpower(struct cpuidle_device *dev,
				struct cpuidle_driver *drv,
				int index)
{
	int new_index = index;

	/* This mode only can be entered when other core's are offline */
	if (num_online_cpus() > 1)
		return exynos_enter_idle(dev, drv, 0);

	return exynos_enter_core0_aftr(dev, drv, new_index);
}

static int __init exynos_init_cpuidle(void)
{
	int i, max_cpuidle_state, cpu_id;
	struct cpuidle_device *device;
	struct cpuidle_driver *drv = &exynos_idle_driver;
	struct cpuidle_state *idle_set;

	exynos3250_enable_idle_clock_down();

	/* Setup cpuidle driver */
	idle_set = exynos_cpuidle_set;
	drv->state_count = ARRAY_SIZE(exynos_cpuidle_set);

	max_cpuidle_state = drv->state_count;
	for (i = 0; i < max_cpuidle_state; i++) {
		memcpy(&drv->states[i], &idle_set[i],
				sizeof(struct cpuidle_state));
	}
	drv->safe_state_index = 0;
	cpuidle_register_driver(&exynos_idle_driver);

	for_each_cpu(cpu_id, cpu_online_mask) {
		device = &per_cpu(exynos_cpuidle_device, cpu_id);
		device->cpu = cpu_id;

	device->state_count = max_cpuidle_state;

	if (cpuidle_register_device(device)) {
		printk(KERN_ERR "CPUidle register device failed\n,");
		return -EIO;
		}
	}

	return 0;
}
device_initcall(exynos_init_cpuidle);
