/* linux/drivers/video/decon_display/decon_mipi_dsi.c
 *
 * Samsung SoC MIPI-DSIM driver.
 *
 * Copyright (c) 2013 Samsung Electronics
 *
 * Haowei Li <haowei.li@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/clk.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/fb.h>
#include <linux/ctype.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/memory.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/kthread.h>
#include <linux/regulator/consumer.h>
#include <linux/notifier.h>
#include <linux/pm_runtime.h>
#include <linux/lcd.h>
#include <linux/gpio.h>

#include <video/mipi_display.h>

#include <plat/fb.h>
#include <plat/cpu.h>

#include <mach/map.h>

#include "decon_mipi_dsi_lowlevel.h"
#include "decon_mipi_dsi.h"
#include "regs-mipidsim.h"

static DEFINE_MUTEX(dsim_rd_wr_mutex);
static DECLARE_COMPLETION(dsim_wr_comp);
static DECLARE_COMPLETION(dsim_rd_comp);

static void mipi_cmu_set(void);

#define MIPI_WR_TIMEOUT msecs_to_jiffies(250)
#define MIPI_RD_TIMEOUT msecs_to_jiffies(250)

static unsigned int dpll_table[15] = {
	100, 120, 170, 220, 270,
	320, 390, 450, 510, 560,
	640, 690, 770, 870, 950 };

#ifdef CONFIG_OF
static const struct of_device_id exynos5_dsim[] = {
	{ .compatible = "samsung,exynos5-dsim" },
	{},
};
MODULE_DEVICE_TABLE(of, exynos5_dsim);
#endif

extern struct mipi_dsim_lcd_driver s6e3fa0_mipi_lcd_driver;
struct mipi_dsim_config g_dsim_config;

struct mipi_dsim_device *dsim_for_decon;
EXPORT_SYMBOL(dsim_for_decon);

int s5p_dsim_init_d_phy(struct mipi_dsim_device *dsim, unsigned int enable)
{
	void __iomem *reg;

	reg = ioremap(0x105C0710, 0x10);
	writel(0x1, reg);
	writel(0x1, reg + 0x4);
	writel(0x1, reg + 0x8);
	writel(0x1, reg + 0x10);
	iounmap(reg);

	reg = ioremap(0x13B80000, 0x4);
	writel(0x0, reg);
	mdelay(5);
	writel(0x1, reg);
	iounmap(reg);

	return 0;
}
static int mipi_lcd_power_control(struct mipi_dsim_device *dsim, unsigned int power);


static void s5p_mipi_dsi_set_packet_ctrl(struct mipi_dsim_device *dsim)
{
	writel(0xffff, dsim->reg_base + S5P_DSIM_MULTI_PKT);
}

static void s5p_mipi_dsi_long_data_wr(struct mipi_dsim_device *dsim, unsigned int data0, unsigned int data1)
{
	unsigned int data_cnt = 0, payload = 0;

	/* in case that data count is more then 4 */
	for (data_cnt = 0; data_cnt < data1; data_cnt += 4) {
		/*
		 * after sending 4bytes per one time,
		 * send remainder data less then 4.
		 */
		if ((data1 - data_cnt) < 4) {
			if ((data1 - data_cnt) == 3) {
				payload = *(u8 *)(data0 + data_cnt) |
				    (*(u8 *)(data0 + (data_cnt + 1))) << 8 |
					(*(u8 *)(data0 + (data_cnt + 2))) << 16;
			dev_dbg(dsim->dev, "count = 3 payload = %x, %x %x %x\n",
				payload, *(u8 *)(data0 + data_cnt),
				*(u8 *)(data0 + (data_cnt + 1)),
				*(u8 *)(data0 + (data_cnt + 2)));
			} else if ((data1 - data_cnt) == 2) {
				payload = *(u8 *)(data0 + data_cnt) |
					(*(u8 *)(data0 + (data_cnt + 1))) << 8;
			dev_dbg(dsim->dev,
				"count = 2 payload = %x, %x %x\n", payload,
				*(u8 *)(data0 + data_cnt),
				*(u8 *)(data0 + (data_cnt + 1)));
			} else if ((data1 - data_cnt) == 1) {
				payload = *(u8 *)(data0 + data_cnt);
			}

			s5p_mipi_dsi_wr_tx_data(dsim, payload);
		/* send 4bytes per one time. */
		} else {
			payload = *(u8 *)(data0 + data_cnt) |
				(*(u8 *)(data0 + (data_cnt + 1))) << 8 |
				(*(u8 *)(data0 + (data_cnt + 2))) << 16 |
				(*(u8 *)(data0 + (data_cnt + 3))) << 24;

			dev_dbg(dsim->dev,
				"count = 4 payload = %x, %x %x %x %x\n",
				payload, *(u8 *)(data0 + data_cnt),
				*(u8 *)(data0 + (data_cnt + 1)),
				*(u8 *)(data0 + (data_cnt + 2)),
				*(u8 *)(data0 + (data_cnt + 3)));

			s5p_mipi_dsi_wr_tx_data(dsim, payload);
		}
	}
}

int s5p_mipi_dsi_wr_data(struct mipi_dsim_device *dsim, unsigned int data_id,
	unsigned int data0, unsigned int data1)
{
	if (dsim->enabled == false || dsim->state != DSIM_STATE_HSCLKEN) {
		dev_dbg(dsim->dev, "MIPI DSIM is not ready.\n");
		return -EINVAL;
	}

	mutex_lock(&dsim_rd_wr_mutex);
	switch (data_id) {
	/* short packet types of packet types for command. */
	case MIPI_DSI_GENERIC_SHORT_WRITE_0_PARAM:
	case MIPI_DSI_GENERIC_SHORT_WRITE_1_PARAM:
	case MIPI_DSI_GENERIC_SHORT_WRITE_2_PARAM:
	case MIPI_DSI_DCS_SHORT_WRITE:
	case MIPI_DSI_DCS_SHORT_WRITE_PARAM:
	case MIPI_DSI_SET_MAXIMUM_RETURN_PACKET_SIZE:
		s5p_mipi_dsi_wr_tx_header(dsim, data_id, data0, data1);
		break;

	/* general command */
	case MIPI_DSI_COLOR_MODE_OFF:
	case MIPI_DSI_COLOR_MODE_ON:
	case MIPI_DSI_SHUTDOWN_PERIPHERAL:
	case MIPI_DSI_TURN_ON_PERIPHERAL:
		s5p_mipi_dsi_wr_tx_header(dsim, data_id, data0, data1);
		break;

	/* packet types for video data */
	case MIPI_DSI_V_SYNC_START:
	case MIPI_DSI_V_SYNC_END:
	case MIPI_DSI_H_SYNC_START:
	case MIPI_DSI_H_SYNC_END:
	case MIPI_DSI_END_OF_TRANSMISSION:
		break;

	/* short and response packet types for command */
	case MIPI_DSI_GENERIC_READ_REQUEST_0_PARAM:
	case MIPI_DSI_GENERIC_READ_REQUEST_1_PARAM:
	case MIPI_DSI_GENERIC_READ_REQUEST_2_PARAM:
	case MIPI_DSI_DCS_READ:
		s5p_mipi_dsi_clear_all_interrupt(dsim);
		s5p_mipi_dsi_wr_tx_header(dsim, data_id, data0, data1);
		/* process response func should be implemented. */
		break;

	/* long packet type and null packet */
	case MIPI_DSI_NULL_PACKET:
	case MIPI_DSI_BLANKING_PACKET:
		break;

	case MIPI_DSI_GENERIC_LONG_WRITE:
	case MIPI_DSI_DCS_LONG_WRITE:
	{
		unsigned int size, data_cnt = 0, payload = 0;

		size = data1 * 4;
		INIT_COMPLETION(dsim_wr_comp);
		/* if data count is less then 4, then send 3bytes data.  */
		if (data1 < 4) {
			payload = *(u8 *)(data0) |
				*(u8 *)(data0 + 1) << 8 |
				*(u8 *)(data0 + 2) << 16;

			s5p_mipi_dsi_wr_tx_data(dsim, payload);

			dev_dbg(dsim->dev, "count = %d payload = %x,%x %x %x\n",
				data1, payload,
				*(u8 *)(data0 + data_cnt),
				*(u8 *)(data0 + (data_cnt + 1)),
				*(u8 *)(data0 + (data_cnt + 2)));
		/* in case that data count is more then 4 */
		} else
			s5p_mipi_dsi_long_data_wr(dsim, data0, data1);

		/* put data into header fifo */
		s5p_mipi_dsi_wr_tx_header(dsim, data_id, data1 & 0xff,
			(data1 & 0xff00) >> 8);

		if (!wait_for_completion_interruptible_timeout(&dsim_wr_comp,
			MIPI_WR_TIMEOUT)) {
				dev_err(dsim->dev, "MIPI DSIM write Timeout!\n");
				mutex_unlock(&dsim_rd_wr_mutex);
				return -1;
		}
		break;
	}

	/* packet typo for video data */
	case MIPI_DSI_PACKED_PIXEL_STREAM_16:
	case MIPI_DSI_PACKED_PIXEL_STREAM_18:
	case MIPI_DSI_PIXEL_STREAM_3BYTE_18:
	case MIPI_DSI_PACKED_PIXEL_STREAM_24:
		break;
	default:
		dev_warn(dsim->dev,
			"data id %x is not supported current DSI spec.\n",
			data_id);

		mutex_unlock(&dsim_rd_wr_mutex);
		return -EINVAL;
	}
	mutex_unlock(&dsim_rd_wr_mutex);

	return 0;
}

static void s5p_mipi_dsi_rx_err_handler(struct mipi_dsim_device *dsim,
	u32 rx_fifo)
{
	/* Parse error report bit*/
	if (rx_fifo & (1 << 8))
		dev_err(dsim->dev, "SoT error!\n");
	if (rx_fifo & (1 << 9))
		dev_err(dsim->dev, "SoT sync error!\n");
	if (rx_fifo & (1 << 10))
		dev_err(dsim->dev, "EoT error!\n");
	if (rx_fifo & (1 << 11))
		dev_err(dsim->dev, "Escape mode entry command error!\n");
	if (rx_fifo & (1 << 12))
		dev_err(dsim->dev, "Low-power transmit sync error!\n");
	if (rx_fifo & (1 << 13))
		dev_err(dsim->dev, "HS receive timeout error!\n");
	if (rx_fifo & (1 << 14))
		dev_err(dsim->dev, "False control error!\n");
	/* Bit 15 is reserved*/
	if (rx_fifo & (1 << 16))
		dev_err(dsim->dev, "ECC error, single-bit(detected and corrected)!\n");
	if (rx_fifo & (1 << 17))
		dev_err(dsim->dev, "ECC error, multi-bit(detected, not corrected)!\n");
	if (rx_fifo & (1 << 18))
		dev_err(dsim->dev, "Checksum error(long packet only)!\n");
	if (rx_fifo & (1 << 19))
		dev_err(dsim->dev, "DSI data type not recognized!\n");
	if (rx_fifo & (1 << 20))
		dev_err(dsim->dev, "DSI VC ID invalid!\n");
	if (rx_fifo & (1 << 21))
		dev_err(dsim->dev, "Invalid transmission length!\n");
	/* Bit 22 is reserved */
	if (rx_fifo & (1 << 23))
		dev_err(dsim->dev, "DSI protocol violation!\n");
}

int s5p_mipi_dsi_rd_data(struct mipi_dsim_device *dsim, u32 data_id,
	 u32 addr, u32 count, u8 *buf)
{
	u32 rx_fifo, txhd, rx_size;
	int i, j;

	if (dsim->enabled == false || dsim->state != DSIM_STATE_HSCLKEN) {
		dev_dbg(dsim->dev, "MIPI DSIM is not ready.\n");
		return -EINVAL;
	}

	mutex_lock(&dsim_rd_wr_mutex);
	INIT_COMPLETION(dsim_rd_comp);

	/* Set the maximum packet size returned */
	txhd = MIPI_DSI_SET_MAXIMUM_RETURN_PACKET_SIZE | count << 8;
	writel(txhd, dsim->reg_base + S5P_DSIM_PKTHDR);

	 /* Read request */
	txhd = data_id | addr << 8;
	writel(txhd, dsim->reg_base + S5P_DSIM_PKTHDR);

	if (!wait_for_completion_interruptible_timeout(&dsim_rd_comp,
		MIPI_RD_TIMEOUT)) {
		dev_err(dsim->dev, "MIPI DSIM read Timeout!\n");
		mutex_unlock(&dsim_rd_wr_mutex);
		return -ETIMEDOUT;
	}

	rx_fifo = readl(dsim->reg_base + S5P_DSIM_RXFIFO);

	/* Parse the RX packet data types */
	switch (rx_fifo & 0xff) {
	case MIPI_DSI_RX_ACKNOWLEDGE_AND_ERROR_REPORT:
		s5p_mipi_dsi_rx_err_handler(dsim, rx_fifo);
		goto rx_error;
	case MIPI_DSI_RX_END_OF_TRANSMISSION:
		dev_dbg(dsim->dev, "EoTp was received from LCD module.\n");
		break;
	case MIPI_DSI_RX_DCS_SHORT_READ_RESPONSE_1BYTE:
	case MIPI_DSI_RX_DCS_SHORT_READ_RESPONSE_2BYTE:
	case MIPI_DSI_RX_GENERIC_SHORT_READ_RESPONSE_1BYTE:
	case MIPI_DSI_RX_GENERIC_SHORT_READ_RESPONSE_2BYTE:
		dev_dbg(dsim->dev, "Short Packet was received from LCD module.\n");
		for (i = 0; i <= count; i++)
			buf[i] = (rx_fifo >> (8 + i * 8)) & 0xff;
		break;
	case MIPI_DSI_RX_DCS_LONG_READ_RESPONSE:
	case MIPI_DSI_RX_GENERIC_LONG_READ_RESPONSE:
		dev_dbg(dsim->dev, "Long Packet was received from LCD module.\n");
		rx_size = (rx_fifo & 0x00ffff00) >> 8;
		/* Read data from RX packet payload */
		for (i = 0; i < rx_size >> 2; i++) {
			rx_fifo = readl(dsim->reg_base + S5P_DSIM_RXFIFO);
			buf[0 + i] = (u8)(rx_fifo >> 0) & 0xff;
			buf[1 + i] = (u8)(rx_fifo >> 8) & 0xff;
			buf[2 + i] = (u8)(rx_fifo >> 16) & 0xff;
			buf[3 + i] = (u8)(rx_fifo >> 24) & 0xff;
		}
		if (rx_size % 4) {
			rx_fifo = readl(dsim->reg_base + S5P_DSIM_RXFIFO);
			for (j = 0; j < rx_size % 4; j++)
				buf[4 * i + j] =
					(u8)(rx_fifo >> (j * 8)) & 0xff;
		}
		break;
	default:
		dev_err(dsim->dev, "Packet format is invaild.\n");
		goto rx_error;
	}

	rx_fifo = readl(dsim->reg_base + S5P_DSIM_RXFIFO);
	if (rx_fifo != DSIM_RX_FIFO_READ_DONE) {
		dev_info(dsim->dev, "[DSIM:WARN]:%s Can't find RX FIFO READ DONE FLAG : %x\n",
			__func__, rx_fifo);
		goto clear_rx_fifo;
	}
	mutex_unlock(&dsim_rd_wr_mutex);
	return 0;

clear_rx_fifo:
	i = 0;
	while (1) {
		rx_fifo = readl(dsim->reg_base + S5P_DSIM_RXFIFO);
		if ((rx_fifo == DSIM_RX_FIFO_READ_DONE) ||
				(i > DSIM_MAX_RX_FIFO))
			break;
		dev_info(dsim->dev, "[DSIM:INFO] : %s clear rx fifo : %08x\n",
			__func__, rx_fifo);
		i++;
	}
	mutex_unlock(&dsim_rd_wr_mutex);
	return 0;

rx_error:
	s5p_mipi_dsi_force_dphy_stop_state(dsim, 1);
	usleep_range(3000, 4000);
	s5p_mipi_dsi_force_dphy_stop_state(dsim, 0);
	mutex_unlock(&dsim_rd_wr_mutex);
	return -1;
}

int s5p_mipi_dsi_pll_on(struct mipi_dsim_device *dsim, unsigned int enable)
{
	int sw_timeout;

	if (enable) {
		sw_timeout = 1000;

		s5p_mipi_dsi_clear_interrupt(dsim, INTSRC_PLL_STABLE);
		s5p_mipi_dsi_enable_pll(dsim, 1);
		while (1) {
			sw_timeout--;
			if (s5p_mipi_dsi_is_pll_stable(dsim))
				return 0;
			if (sw_timeout == 0)
				return -EINVAL;
		}
	} else
		s5p_mipi_dsi_enable_pll(dsim, 0);

	return 0;
}

unsigned long s5p_mipi_dsi_change_pll(struct mipi_dsim_device *dsim,
	unsigned int pre_divider, unsigned int main_divider,
	unsigned int scaler)
{
	unsigned long dfin_pll, dfvco, dpll_out;
	unsigned int i, freq_band = 0xf;

	dfin_pll = (FIN_HZ / pre_divider);

	if (soc_is_exynos5250()) {
		if (dfin_pll < DFIN_PLL_MIN_HZ || dfin_pll > DFIN_PLL_MAX_HZ) {
			dev_warn(dsim->dev, "fin_pll range should be 6MHz ~ 12MHz\n");
			s5p_mipi_dsi_enable_afc(dsim, 0, 0);
		} else {
			if (dfin_pll < 7 * MHZ)
				s5p_mipi_dsi_enable_afc(dsim, 1, 0x1);
			else if (dfin_pll < 8 * MHZ)
				s5p_mipi_dsi_enable_afc(dsim, 1, 0x0);
			else if (dfin_pll < 9 * MHZ)
				s5p_mipi_dsi_enable_afc(dsim, 1, 0x3);
			else if (dfin_pll < 10 * MHZ)
				s5p_mipi_dsi_enable_afc(dsim, 1, 0x2);
			else if (dfin_pll < 11 * MHZ)
				s5p_mipi_dsi_enable_afc(dsim, 1, 0x5);
			else
				s5p_mipi_dsi_enable_afc(dsim, 1, 0x4);
		}
	}
	dfvco = dfin_pll * main_divider;
	dev_dbg(dsim->dev, "dfvco = %lu, dfin_pll = %lu, main_divider = %d\n",
				dfvco, dfin_pll, main_divider);

	if (soc_is_exynos5250()) {
		if (dfvco < DFVCO_MIN_HZ || dfvco > DFVCO_MAX_HZ)
			dev_warn(dsim->dev, "fvco range should be 500MHz ~ 1000MHz\n");
	}

	dpll_out = dfvco / (1 << scaler);
	dev_dbg(dsim->dev, "dpll_out = %lu, dfvco = %lu, scaler = %d\n",
		dpll_out, dfvco, scaler);

	if (soc_is_exynos5250()) {
		for (i = 0; i < ARRAY_SIZE(dpll_table); i++) {
			if (dpll_out < dpll_table[i] * MHZ) {
				freq_band = i;
				break;
			}
		}
	}

	dev_dbg(dsim->dev, "freq_band = %d\n", freq_band);

	s5p_mipi_dsi_pll_freq(dsim, pre_divider, main_divider, scaler);

	s5p_mipi_dsi_hs_zero_ctrl(dsim, 0);
	s5p_mipi_dsi_prep_ctrl(dsim, 0);

	if (soc_is_exynos5250()) {
		/* Freq Band */
		s5p_mipi_dsi_pll_freq_band(dsim, freq_band);
	}
	/* Stable time */
	s5p_mipi_dsi_pll_stable_time(dsim, dsim->dsim_config->pll_stable_time);

	/* Enable PLL */
	dev_dbg(dsim->dev, "FOUT of mipi dphy pll is %luMHz\n",
		(dpll_out / MHZ));

	return dpll_out;
}

int s5p_mipi_dsi_set_clock(struct mipi_dsim_device *dsim,
	unsigned int byte_clk_sel, unsigned int enable)
{
	unsigned int esc_div;
	unsigned long esc_clk_error_rate;

	if (enable) {
		dsim->e_clk_src = byte_clk_sel;

		/* Escape mode clock and byte clock source */
		s5p_mipi_dsi_set_byte_clock_src(dsim, byte_clk_sel);

		/* DPHY, DSIM Link : D-PHY clock out */
		if (byte_clk_sel == DSIM_PLL_OUT_DIV8) {
			dsim->hs_clk = s5p_mipi_dsi_change_pll(dsim,
				dsim->dsim_config->p, dsim->dsim_config->m,
				dsim->dsim_config->s);
			if (dsim->hs_clk == 0) {
				dev_err(dsim->dev,
					"failed to get hs clock.\n");
				return -EINVAL;
			}
			if (!soc_is_exynos5250()) {
#if defined(CONFIG_DECON_LCD_S6E8AA0)
				s5p_mipi_dsi_set_b_dphyctrl(dsim, 0x0AF);
				s5p_mipi_dsi_set_timing_register0(dsim, 0x03, 0x06);
				s5p_mipi_dsi_set_timing_register1(dsim, 0x04, 0x15,
									0x09, 0x04);
				s5p_mipi_dsi_set_timing_register2(dsim, 0x05, 0x06,
									0x07);
#elif defined(CONFIG_DECON_LCD_S6E3FA0)
#ifdef CONFIG_FB_I80_COMMAND_MODE
				s5p_mipi_dsi_set_b_dphyctrl(dsim, 0x0AF);
				s5p_mipi_dsi_set_timing_register0(dsim, 0x08, 0x0d);
				s5p_mipi_dsi_set_timing_register1(dsim, 0x09, 0x30,
									0x0e, 0x0a);
				s5p_mipi_dsi_set_timing_register2(dsim, 0x0c, 0x11,
									0x0d);
#else
				s5p_mipi_dsi_set_b_dphyctrl(dsim, 0x0AF);
				s5p_mipi_dsi_set_timing_register0(dsim, 0x06, 0x0b);
				s5p_mipi_dsi_set_timing_register1(dsim, 0x07, 0x27,
									0x0d, 0x08);
				s5p_mipi_dsi_set_timing_register2(dsim, 0x09, 0x0d,
									0x0b);
#endif
#endif
			}
			dsim->byte_clk = dsim->hs_clk / 8;
			s5p_mipi_dsi_enable_pll_bypass(dsim, 0);
			s5p_mipi_dsi_pll_on(dsim, 1);
		/* DPHY : D-PHY clock out, DSIM link : external clock out */
		} else if (byte_clk_sel == DSIM_EXT_CLK_DIV8)
			dev_warn(dsim->dev,
				"this project is not support \
				external clock source for MIPI DSIM\n");
		else if (byte_clk_sel == DSIM_EXT_CLK_BYPASS)
			dev_warn(dsim->dev,
				"this project is not support \
				external clock source for MIPI DSIM\n");

		/* escape clock divider */
		esc_div = dsim->byte_clk / (dsim->dsim_config->esc_clk);
		dev_dbg(dsim->dev,
			"esc_div = %d, byte_clk = %lu, esc_clk = %lu\n",
			esc_div, dsim->byte_clk, dsim->dsim_config->esc_clk);

		if (soc_is_exynos5250()) {
			if ((dsim->byte_clk / esc_div) >= (20 * MHZ) ||
					(dsim->byte_clk / esc_div) >
						dsim->dsim_config->esc_clk)
				esc_div += 1;
		} else {
			if ((dsim->byte_clk / esc_div) >= (10 * MHZ) ||
				(dsim->byte_clk / esc_div) >
					dsim->dsim_config->esc_clk)
				esc_div += 1;
		}
		dsim->escape_clk = dsim->byte_clk / esc_div;
		dev_dbg(dsim->dev,
			"escape_clk = %lu, byte_clk = %lu, esc_div = %d\n",
			dsim->escape_clk, dsim->byte_clk, esc_div);

		/* enable byte clock. */
		s5p_mipi_dsi_enable_byte_clock(dsim, DSIM_ESCCLK_ON);

		/* enable escape clock */
		s5p_mipi_dsi_set_esc_clk_prs(dsim, 1, esc_div);
		/* escape clock on lane */
		s5p_mipi_dsi_enable_esc_clk_on_lane(dsim,
			(DSIM_LANE_CLOCK | dsim->data_lane), 1);

		dev_dbg(dsim->dev, "byte clock is %luMHz\n",
			(dsim->byte_clk / MHZ));
		dev_dbg(dsim->dev, "escape clock that user's need is %lu\n",
			(dsim->dsim_config->esc_clk / MHZ));
		dev_dbg(dsim->dev, "escape clock divider is %x\n", esc_div);
		dev_dbg(dsim->dev, "escape clock is %luMHz\n",
			((dsim->byte_clk / esc_div) / MHZ));

		if ((dsim->byte_clk / esc_div) > dsim->escape_clk) {
			esc_clk_error_rate = dsim->escape_clk /
				(dsim->byte_clk / esc_div);
			dev_warn(dsim->dev, "error rate is %lu over.\n",
				(esc_clk_error_rate / 100));
		} else if ((dsim->byte_clk / esc_div) < (dsim->escape_clk)) {
			esc_clk_error_rate = (dsim->byte_clk / esc_div) /
				dsim->escape_clk;
			dev_warn(dsim->dev, "error rate is %lu under.\n",
				(esc_clk_error_rate / 100));
		}
	} else {
		s5p_mipi_dsi_enable_esc_clk_on_lane(dsim,
			(DSIM_LANE_CLOCK | dsim->data_lane), 0);
		s5p_mipi_dsi_set_esc_clk_prs(dsim, 0, 0);

		/* disable escape clock. */
		s5p_mipi_dsi_enable_byte_clock(dsim, DSIM_ESCCLK_OFF);

		if (byte_clk_sel == DSIM_PLL_OUT_DIV8)
			s5p_mipi_dsi_pll_on(dsim, 0);
	}

	return 0;
}

void s5p_mipi_dsi_d_phy_onoff(struct mipi_dsim_device *dsim,
	unsigned int enable)
{
	/*
	if (dsim->pd->init_d_phy)
		dsim->pd->init_d_phy(dsim, enable);
	*/
	s5p_dsim_init_d_phy(dsim, enable);
}

int s5p_mipi_dsi_init_dsim(struct mipi_dsim_device *dsim)
{
	s5p_mipi_dsi_d_phy_onoff(dsim, 1);

	dsim->state = DSIM_STATE_INIT;

	switch (dsim->dsim_config->e_no_data_lane) {
	case DSIM_DATA_LANE_1:
		dsim->data_lane = DSIM_LANE_DATA0;
		break;
	case DSIM_DATA_LANE_2:
		dsim->data_lane = DSIM_LANE_DATA0 | DSIM_LANE_DATA1;
		break;
	case DSIM_DATA_LANE_3:
		dsim->data_lane = DSIM_LANE_DATA0 | DSIM_LANE_DATA1 |
			DSIM_LANE_DATA2;
		break;
	case DSIM_DATA_LANE_4:
		dsim->data_lane = DSIM_LANE_DATA0 | DSIM_LANE_DATA1 |
			DSIM_LANE_DATA2 | DSIM_LANE_DATA3;
		break;
	default:
		dev_info(dsim->dev, "data lane is invalid.\n");
		return -EINVAL;
	};

	s5p_mipi_dsi_sw_reset(dsim);
	s5p_mipi_dsi_dp_dn_swap(dsim, 0);

	return 0;
}

int s5p_mipi_dsi_enable_frame_done_int(struct mipi_dsim_device *dsim,
	unsigned int enable)
{
	/* enable only frame done interrupt */
	s5p_mipi_dsi_set_interrupt_mask(dsim, INTMSK_FRAME_DONE, enable);

	return 0;
}

#define DT_READ_U32(node, key, value) { \
	u32 temp; \
	if (of_property_read_u32((node), "samsung," key, &temp)) { \
		printk(KERN_ERR "Fail to get %s value.\n", key); \
		return -1; \
	} \
	(value) = temp; \
	}

int s5p_mipi_dsi_set_display_mode(struct mipi_dsim_device *dsim,
	struct mipi_dsim_config *dsim_config)
{
	struct s3c_fb_pd_win *pd;
	unsigned int width = 0, height = 0;
	u32 umg, lmg, rmg, vsync_len, hsync_len;
	pd = (struct s3c_fb_pd_win *)dsim->dsim_config->lcd_panel_info;

	DT_READ_U32(dsim->dev->of_node, "xres", width);
	DT_READ_U32(dsim->dev->of_node, "yres", height);
	DT_READ_U32(dsim->dev->of_node, "upper_margin", umg);
	DT_READ_U32(dsim->dev->of_node, "left_margin", lmg);
	DT_READ_U32(dsim->dev->of_node, "right_margin", rmg);
	DT_READ_U32(dsim->dev->of_node, "vsync_len", vsync_len);
	DT_READ_U32(dsim->dev->of_node, "hsync_len", hsync_len);

	/* in case of VIDEO MODE (RGB INTERFACE) */
	if (dsim->dsim_config->e_interface == (u32) DSIM_VIDEO) {
			s5p_mipi_dsi_set_main_disp_vporch(dsim,
				1, /* cmd allow */
				1, /* stable vfp */
				umg);
			s5p_mipi_dsi_set_main_disp_hporch(dsim,
				rmg, lmg);
			s5p_mipi_dsi_set_main_disp_sync_area(dsim,
				vsync_len,
				hsync_len);
	}

	s5p_mipi_dsi_set_main_disp_resol(dsim, height, width);
	s5p_mipi_dsi_display_config(dsim);
	return 0;
}

int s5p_mipi_dsi_init_link(struct mipi_dsim_device *dsim)
{
	unsigned int time_out = 100;
	unsigned int id;
	id = dsim->id;
	switch (dsim->state) {
	case DSIM_STATE_INIT:
		s5p_mipi_dsi_sw_reset(dsim);
		s5p_mipi_dsi_init_fifo_pointer(dsim, 0x1f);

		/* dsi configuration */
		s5p_mipi_dsi_init_config(dsim);
		s5p_mipi_dsi_enable_lane(dsim, DSIM_LANE_CLOCK, 1);
		s5p_mipi_dsi_enable_lane(dsim, dsim->data_lane, 1);

		/* set clock configuration */
		s5p_mipi_dsi_set_clock(dsim, dsim->dsim_config->e_byte_clk, 1);

		msleep(100);

		/* check clock and data lane state are stop state */
		while (!(s5p_mipi_dsi_is_lane_state(dsim))) {
			time_out--;
			if (time_out == 0) {
				dev_err(dsim->dev,
					"DSI Master is not stop state.\n");
				dev_err(dsim->dev,
					"Check initialization process\n");

				return -EINVAL;
			}
		}

		if (time_out != 0) {
			dev_info(dsim->dev,
				"DSI Master driver has been completed.\n");
			dev_info(dsim->dev, "DSI Master state is stop state\n");
		}

		dsim->state = DSIM_STATE_STOP;

		/* BTA sequence counters */
		s5p_mipi_dsi_set_stop_state_counter(dsim,
			dsim->dsim_config->stop_holding_cnt);
		s5p_mipi_dsi_set_bta_timeout(dsim,
			dsim->dsim_config->bta_timeout);
		s5p_mipi_dsi_set_lpdr_timeout(dsim,
			dsim->dsim_config->rx_timeout);
		s5p_mipi_dsi_set_packet_ctrl(dsim);
		return 0;
	default:
		dev_info(dsim->dev, "DSI Master is already init.\n");
		return 0;
	}

	return 0;
}

int s5p_mipi_dsi_set_hs_enable(struct mipi_dsim_device *dsim)
{
	if (dsim->state == DSIM_STATE_STOP) {
		if (dsim->e_clk_src != DSIM_EXT_CLK_BYPASS) {
			dsim->state = DSIM_STATE_HSCLKEN;

			 /* set LCDC and CPU transfer mode to HS. */
			s5p_mipi_dsi_set_lcdc_transfer_mode(dsim, 0);
			s5p_mipi_dsi_set_cpu_transfer_mode(dsim, 0);

			s5p_mipi_dsi_enable_hs_clock(dsim, 1);

			return 0;
		} else
			dev_warn(dsim->dev,
				"clock source is external bypass.\n");
	} else
		dev_warn(dsim->dev, "DSIM is not stop state.\n");

	return 0;
}

int s5p_mipi_dsi_set_data_transfer_mode(struct mipi_dsim_device *dsim,
		unsigned int mode)
{
	if (mode) {
		if (dsim->state != DSIM_STATE_HSCLKEN) {
			dev_err(dsim->dev, "HS Clock lane is not enabled.\n");
			return -EINVAL;
		}

		s5p_mipi_dsi_set_lcdc_transfer_mode(dsim, 0);
	} else {
		if (dsim->state == DSIM_STATE_INIT || dsim->state ==
			DSIM_STATE_ULPS) {
			dev_err(dsim->dev,
				"DSI Master is not STOP or HSDT state.\n");
			return -EINVAL;
		}

		s5p_mipi_dsi_set_cpu_transfer_mode(dsim, 0);
	}
	return 0;
}

int s5p_mipi_dsi_get_frame_done_status(struct mipi_dsim_device *dsim)
{
	return _s5p_mipi_dsi_get_frame_done_status(dsim);
}

int s5p_mipi_dsi_clear_frame_done(struct mipi_dsim_device *dsim)
{
	_s5p_mipi_dsi_clear_frame_done(dsim);

	return 0;
}

static irqreturn_t s5p_mipi_dsi_interrupt_handler(int irq, void *dev_id)
{
	unsigned int int_src;
	struct mipi_dsim_device *dsim = dev_id;

	s5p_mipi_dsi_set_interrupt_mask(dsim, 0xffffffff, 1);
	int_src = readl(dsim->reg_base + S5P_DSIM_INTSRC);

	/* Test bit */
	if (int_src & SFR_PL_FIFO_EMPTY)
		complete(&dsim_wr_comp);
	if (int_src & RX_DAT_DONE)
		complete(&dsim_rd_comp);
	if (int_src & ERR_RX_ECC)
		dev_err(dsim->dev, "RX ECC Multibit error was detected!\n");
	s5p_mipi_dsi_clear_interrupt(dsim, int_src);

	s5p_mipi_dsi_set_interrupt_mask(dsim, 0xffffffff, 0);
	return IRQ_HANDLED;
}

#ifdef CONFIG_PM
static int s5p_mipi_dsi_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct mipi_dsim_device *dsim = platform_get_drvdata(pdev);

	if (dsim->enabled == false)
		return 0;
	dsim->enabled = false;
	dsim->dsim_lcd_drv->suspend(dsim);
	dsim->state = DSIM_STATE_SUSPEND;
	mipi_lcd_power_control(dsim, 0);
	s5p_mipi_dsi_d_phy_onoff(dsim, 0);
	/*
	if (dsim->pd->mipi_power)
		dsim->pd->mipi_power(dsim, 0);
	*/
	pm_runtime_put_sync(dev);
	/*
	clk_disable(dsim->clock);
	*/
	return 0;
}

static int s5p_mipi_dsi_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct mipi_dsim_device *dsim = platform_get_drvdata(pdev);

	if (dsim->enabled == true)
		return 0;

	mipi_cmu_set();
	pm_runtime_get_sync(&pdev->dev);
	/*
	clk_enable(dsim->clock);
	*/

	/*
	if (dsim->pd->mipi_power)
		dsim->pd->mipi_power(dsim, 1);
	*/
	mipi_lcd_power_control(dsim, 1);

	if (dsim->dsim_lcd_drv->resume)
		dsim->dsim_lcd_drv->resume(dsim);
	s5p_mipi_dsi_init_dsim(dsim);
	s5p_mipi_dsi_init_link(dsim);
	dsim->enabled = true;
	s5p_mipi_dsi_set_data_transfer_mode(dsim, 0);
	s5p_mipi_dsi_set_display_mode(dsim, dsim->dsim_config);
	s5p_mipi_dsi_set_hs_enable(dsim);
	dsim->dsim_lcd_drv->displayon(dsim);

	return 0;
}

static int s5p_mipi_dsi_runtime_suspend(struct device *dev)
{
	return 0;
}

static int s5p_mipi_dsi_runtime_resume(struct device *dev)
{
	return 0;
}
#else
#define s5p_mipi_dsi_suspend NULL
#define s5p_mipi_dsi_resume NULL
#define s5p_mipi_dsi_runtime_suspend NULL
#define s5p_mipi_dsi_runtime_resume NULL
#endif

int s5p_mipi_dsi_enable(struct mipi_dsim_device *dsim)
{
	/*
	clk_enable(dsim->clock);
	*/
	if (dsim->enabled == true)
		return 0;

	pm_runtime_get_sync(dsim->dev);
	/*
	if (dsim->pd->mipi_power)
		dsim->pd->mipi_power(dsim, 1);
	*/
	mipi_cmu_set();
	mipi_lcd_power_control(dsim, 1);

	if (dsim->dsim_lcd_drv->resume)
		dsim->dsim_lcd_drv->resume(dsim);
	s5p_mipi_dsi_init_dsim(dsim);
	s5p_mipi_dsi_init_link(dsim);
	dsim->enabled = true;

	s5p_mipi_dsi_set_data_transfer_mode(dsim, 0);
	s5p_mipi_dsi_set_display_mode(dsim, dsim->dsim_config);
	s5p_mipi_dsi_set_hs_enable(dsim);
	dsim->dsim_lcd_drv->displayon(dsim);

	return 0;
}

int s5p_mipi_dsi_disable(struct mipi_dsim_device *dsim)
{
	if (dsim->enabled == false)
		return 0;

	dsim->enabled = false;
	dsim->dsim_lcd_drv->suspend(dsim);
	dsim->state = DSIM_STATE_SUSPEND;
	s5p_mipi_dsi_d_phy_onoff(dsim, 0);
	/*
	if (dsim->pd->mipi_power)
		dsim->pd->mipi_power(dsim, 0);
	*/
	mipi_lcd_power_control(dsim, 0);

	pm_runtime_put_sync(dsim->dev);
	/*
	clk_disable(dsim->clock);
	*/

	return 0;
}

static int s5p_mipi_dsi_set_power(struct lcd_device *lcd, int power)
{
	struct mipi_dsim_device *dsim = lcd_get_data(lcd);

	if (power == FB_BLANK_UNBLANK) {
		s5p_mipi_dsi_enable(dsim);
	} else {
		s5p_mipi_dsi_disable(dsim);
	}

	return 0;
}

struct lcd_ops s5p_mipi_dsi_lcd_ops = {
	.set_power = s5p_mipi_dsi_set_power,
};

static int mipi_lcd_power_control(struct mipi_dsim_device *dsim,
			unsigned int power)
{
	void __iomem *regs = ioremap(0x14CC01E0, 0x8);
	u32 data;

	if (power) {
		data = readl(regs);
		data |= 0x1;
		writel(data, regs);

		data = readl(regs + 0x4);
		data |= 0x1;
		writel(data, regs + 0x4);
		usleep_range(5000, 6000);

		data = readl(regs + 0x4);
		data &= ~0x1;
		writel(data, regs + 0x4);
		usleep_range(5000, 6000);
		msleep(5);
		data = readl(regs + 0x4);
		data |= 0x1;
		writel(data, regs + 0x4);
		usleep_range(5000, 6000);
		iounmap(regs);

#ifdef CONFIG_FB_I80_COMMAND_MODE
#ifndef CONFIG_FB_I80_SW_TRIGGER
		regs = ioremap(0x14CC00C0, 0x4);
		data = readl(regs);
		data &= ~(0xf << 4);
		data |= (0x2 << 4);
		writel(data, regs);
		iounmap(regs);
#endif
#endif
	} else {
		data = readl(regs + 0x4);
		data &= ~0x1;
		writel(data, regs + 0x4);
		usleep_range(5000, 6000);

#ifdef CONFIG_FB_I80_COMMAND_MODE
#ifndef CONFIG_FB_I80_SW_TRIGGER
		regs = ioremap(0x14CC00C0, 0x4);
		data = readl(regs);
		data &= ~(0xf << 4);
		writel(data, regs);
		iounmap(regs);
#endif
#endif
	}

	return 1;
}

static void mipi_cmu_set(void)
{
	void __iomem *regs;
	u32 data;

	/* Set DISP_PLL = 136Mhz */
	regs = ioremap(0x13B90100, 0x4);
	writel(0xA0880303, regs);
	iounmap(regs);
	msleep(100);

	/* Set MFC_PLL = 1332Mhz */
	regs = ioremap(0x105B0120, 0x4);
	writel(0xA0DE0400, regs);
	iounmap(regs);
	msleep(100);

	/* Set MUX_DECON_ECLK_A: MOUT_BUS_PLL_SUB */
	regs = ioremap(0x105B0210, 0x4);
	data = readl(regs) | 0x1;
	writel(data, regs);
	iounmap(regs);

	/* Set ACLK_DISP_333_RATIO = 0x2 */
	regs = ioremap(0x105B060c, 0x4);
	data = readl(regs) & ~(0x7 << 4);
	data |= (0x2 << 4);
	writel(data, regs);

	/* Set SCLK_DECON_ECLK_RATIO = 0x2 */
	regs = ioremap(0x105B0610, 0x4);
	data = readl(regs);
	data &= ~0xf;
	data |= 0x2;
	writel(data, regs);
	iounmap(regs);

	/* SET MUX_DISP_PLL: FOUT_DISP_PLL */
	regs = ioremap(0x13B90200, 0x4);
	writel(0x1, regs);
	iounmap(regs);

	/* Set MUX_SCLK_DSD_USER: SCLK_DSD
	 * MUX_SCLK_DECON_VCLK_USER: SCLK_DECON_VCLK
	 * MUX_SCLK_DECON_ECLK_USER: SCLK_DECON_ECLK
	 * MUX_SCLK_ACLK_DISP_222_USER: ACLK_DISP_222
	 * MUX_SCLK_ACLK_DISP_333_USER: ACLK_DISP_333
	 */
	regs = ioremap(0x13B90204, 0x4);
	data = readl(regs + 0x4);
	data |= 0x11111;
	writel(data, regs);
	iounmap(regs);

	/* Set MUX_PHYCLK_MIPIDPHY_RXCLKESC0_USER:
	 *	MUX_PHYCLK_MIPIDPHY_RXCLKESC0
	 * Set MUX_PHYCLK_MIPIDPHY_BITCLKDIV8_USER:
	 *	MUX_PHYCLK_MIPIDPHY_BITCLKDIV8
	 */
	regs = ioremap(0x13B90208, 0x4);
	data = readl(regs);
	data |= (1 << 8) | (1 << 12);
	writel(data, regs);
	iounmap(regs);

	/* Set MUX_SCLK_DECON_ECLK: MOUT_SCLK_DECON_ECLK_USER */
	regs = ioremap(0x13B9020C, 0x4);
	writel(0x1, regs);
	iounmap(regs);

	/* Set: ignore CLK1 was toggling */
	regs = ioremap(0x13B90508, 0x4);
	writel(0, regs);
	iounmap(regs);
}

static int s5p_mipi_dsi_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct mipi_dsim_device *dsim = NULL;
	int ret = -1;

	mipi_cmu_set();
	if (!dsim)
		dsim = kzalloc(sizeof(struct mipi_dsim_device),
			GFP_KERNEL);
	if (!dsim) {
		dev_err(&pdev->dev, "failed to allocate dsim object.\n");
		return -EFAULT;
	}

	dsim->dev = &pdev->dev;
	dsim->id = pdev->id;


	pm_runtime_enable(&pdev->dev);

	dsim->dsim_config = &g_dsim_config;
	/*
	dsim->clock = clk_get(&pdev->dev, dsim->pd->clk_name);
	if (IS_ERR(dsim->clock)) {
		dev_err(&pdev->dev, "failed to get dsim clock source\n");
		goto err_clock_get;
	}
	clk_enable(dsim->clock);
	*/
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "failed to get io memory region\n");
		ret = -EINVAL;
		goto err_platform_get;
	}
	res = request_mem_region(res->start, resource_size(res),
					dev_name(&pdev->dev));
	if (!res) {
		dev_err(&pdev->dev, "failed to request io memory region\n");
		ret = -EINVAL;
		goto err_mem_region;
	}

	dsim->res = res;
	dsim->reg_base = ioremap(res->start, resource_size(res));
	if (!dsim->reg_base) {
		dev_err(&pdev->dev, "failed to remap io region\n");
		ret = -EINVAL;
		goto err_mem_region;
	}

	/*
	 * it uses frame done interrupt handler
	 * only in case of MIPI Video mode.
	 */
	DT_READ_U32(pdev->dev.of_node, "irq_no", dsim->irq);
	if (request_irq(dsim->irq, s5p_mipi_dsi_interrupt_handler,
			IRQF_DISABLED, "mipi-dsi", dsim)) {
		dev_err(&pdev->dev, "request_irq failed.\n");
		goto err_irq;
	}

	dsim->dsim_lcd_drv = dsim->dsim_config->dsim_ddi_pd;

	if (dsim->dsim_config == NULL) {
		dev_err(&pdev->dev, "dsim_config is NULL.\n");
		goto err_dsim_config;
	}
	platform_set_drvdata(pdev, dsim);
	pm_runtime_get_sync(&pdev->dev);

	s5p_mipi_dsi_init_dsim(dsim);
	s5p_mipi_dsi_init_link(dsim);
	dsim->dsim_lcd_drv->probe(dsim);

	/*
	if (dsim->pd->mipi_power)
		dsim->pd->mipi_power(dsim, 1);
	*/
	mipi_lcd_power_control(dsim, 1);

	dsim->enabled = true;
	s5p_mipi_dsi_set_data_transfer_mode(dsim, 0);
	s5p_mipi_dsi_set_display_mode(dsim, dsim->dsim_config);
	s5p_mipi_dsi_set_hs_enable(dsim);
	dsim->dsim_lcd_drv->displayon(dsim);
	dsim_for_decon = dsim;
	dev_info(&pdev->dev, "mipi-dsi driver(%s mode) has been probed.\n",
		(g_dsim_config.e_interface == DSIM_COMMAND) ?
			"CPU" : "RGB");

	mutex_init(&dsim_rd_wr_mutex);
	return 0;

err_dsim_config:
err_irq:
	release_resource(dsim->res);
	kfree(dsim->res);

	iounmap((void __iomem *) dsim->reg_base);

err_mem_region:
err_platform_get:
	clk_disable(dsim->clock);
	clk_put(dsim->clock);

/*err_clock_get:*/
	kfree(dsim);
	pm_runtime_put_sync(&pdev->dev);
	return ret;

}

static int s5p_mipi_dsi_remove(struct platform_device *pdev)
{
	struct mipi_dsim_device *dsim = platform_get_drvdata(pdev);

	if (dsim->dsim_config->e_interface == DSIM_VIDEO)
		free_irq(dsim->irq, dsim);

	iounmap(dsim->reg_base);

	clk_disable(dsim->clock);
	clk_put(dsim->clock);

	release_resource(dsim->res);
	kfree(dsim->res);

	kfree(dsim);

	return 0;
}

static void s5p_mipi_dsi_shutdown(struct platform_device *pdev)
{
	struct mipi_dsim_device *dsim = platform_get_drvdata(pdev);

	if (dsim->enabled != false) {
		dsim->enabled = false;
		dsim->dsim_lcd_drv->suspend(dsim);
		dsim->state = DSIM_STATE_SUSPEND;

		s5p_mipi_dsi_set_interrupt_mask(dsim, 0xffffffff, 1);
		s5p_mipi_dsi_clear_interrupt(dsim, 0xffffffff);

		s5p_mipi_dsi_d_phy_onoff(dsim, 0);

		/*
		if (dsim->pd->mipi_power)
			dsim->pd->mipi_power(dsim, 0);
		*/
		mipi_lcd_power_control(dsim, 0);

		pm_runtime_put_sync(&pdev->dev);
		/*
		clk_disable(dsim->clock);
		*/
	}
}

static const struct dev_pm_ops mipi_dsi_pm_ops = {
#ifndef CONFIG_HAS_EARLYSUSPEND
	.suspend = s5p_mipi_dsi_suspend,
	.resume = s5p_mipi_dsi_resume,
#endif
	.runtime_suspend	= s5p_mipi_dsi_runtime_suspend,
	.runtime_resume		= s5p_mipi_dsi_runtime_resume,
};


struct mipi_dsim_config g_dsim_config = {
#ifdef CONFIG_FB_I80_COMMAND_MODE
	.e_interface	= DSIM_COMMAND,
#else
	.e_interface	= DSIM_VIDEO,
#endif
	.e_pixel_format = DSIM_24BPP_888,
	/* main frame fifo auto flush at VSYNC pulse */
	.auto_flush	= false,
	.eot_disable	= true,
	.auto_vertical_cnt = false,
	.hse = false,
	.hfp = false,
	.hbp = false,
	.hsa = false,

	.e_no_data_lane = DSIM_DATA_LANE_4,
	.e_byte_clk	= DSIM_PLL_OUT_DIV8,
	.e_burst_mode	= DSIM_BURST,

#if defined(CONFIG_DECON_LCD_S6E8AA0)
	.p = 4,
	.m = 80,
	.s = 2,
#elif defined(CONFIG_DECON_LCD_S6E3FA0)
#ifdef CONFIG_FB_I80_COMMAND_MODE
	.p = 2,
	.m = 46,
	.s = 1,
#else
	.p = 4,
	.m = 75,
	.s = 1,
#endif
#endif
	/* D-PHY PLL stable time spec :min = 200usec ~ max 400usec */
	.pll_stable_time = 500,

	.esc_clk = 7 * 1000000, /* escape clk : 7MHz */

	/* stop state holding counter after bta change count 0 ~ 0xfff */
	.stop_holding_cnt = 0x0fff,
	.bta_timeout = 0xff,		/* bta timeout 0 ~ 0xff */
	.rx_timeout = 0xffff,		/* lp rx timeout 0 ~ 0xffff */

#if defined(CONFIG_DECON_LCD_S6E8AA0)
	.dsim_ddi_pd = &s6e8aa0_mipi_lcd_driver,
#elif defined(CONFIG_DECON_LCD_S6E3FA0)
	.dsim_ddi_pd = &s6e3fa0_mipi_lcd_driver,
#endif
};

static struct platform_driver s5p_mipi_dsi_driver = {
	.probe = s5p_mipi_dsi_probe,
	.remove = s5p_mipi_dsi_remove,
	.shutdown = s5p_mipi_dsi_shutdown,
	.driver = {
		.name = "s5p-mipi-dsim",
		.owner = THIS_MODULE,
		.pm = &mipi_dsi_pm_ops,
		.of_match_table = of_match_ptr(exynos5_dsim),
	},
};

static int s5p_mipi_dsi_register(void)
{
	platform_driver_register(&s5p_mipi_dsi_driver);

	return 0;
}

static void s5p_mipi_dsi_unregister(void)
{
	platform_driver_unregister(&s5p_mipi_dsi_driver);
}
late_initcall(s5p_mipi_dsi_register);
module_exit(s5p_mipi_dsi_unregister);

MODULE_AUTHOR("Haowei li <haowei.li@samsung.com>");
MODULE_DESCRIPTION("Samusung MIPI-DSI driver");
MODULE_LICENSE("GPL");
