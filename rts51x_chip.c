/* Driver for Realtek RTS51xx USB card reader
 *
 * Copyright(c) 2009 Realtek Semiconductor Corp. All rights reserved.  
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http:
 *
 * Author:
 *   wwang (wei_wang@realsil.com.cn)
 *   No. 450, Shenhu Road, Suzhou Industry Park, Suzhou, China
 */

#include <linux/blkdev.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/workqueue.h>

#include "debug.h"
#include "trace.h"
#include "rts51x.h"
#include "rts51x_chip.h"
#include "rts51x_card.h"
#include "rts51x_transport.h"
#include "rts51x_sys.h"
#include "xd.h"
#include "ms.h"
#include "sd.h"

static int check_sd_speed_prior(u32 sd_speed_prior)
{
	int i, fake_para = 0;
	
	for (i = 0; i < 4; i++) {
		u8 tmp = (u8)(sd_speed_prior >> (i*8));
		if ((tmp < 0x01) || (tmp > 0x04)) {
			fake_para = 1;
			break;
		}
	}
	
	return !fake_para;
}

int rts51x_reset_chip(struct rts51x_chip *chip)
{
	int retval;

	if(CHECK_PKG(chip, LQFP48)){ 
		RTS51X_WRITE_REG(chip, CARD_PWR_CTL, LDO3318_PWR_MASK, LDO_SUSPEND);
		RTS51X_WRITE_REG(chip, CARD_PWR_CTL, FORCE_LDO_POWERB, FORCE_LDO_POWERB);
		RTS51X_WRITE_REG(chip, CARD_PULL_CTL1, 0x30, 0x10);
		RTS51X_WRITE_REG(chip, CARD_PULL_CTL5, 0x03, 0x01);
		RTS51X_WRITE_REG(chip, CARD_PULL_CTL6, 0x0C, 0x04);
	}

	if (chip->asic_code) { 
		RTS51X_WRITE_REG(chip, SYS_DUMMY0, NYET_MSAK, NYET_EN);
		RTS51X_WRITE_REG(chip, CD_DEGLITCH_WIDTH, 0xFF, 0x08);
		rts51x_write_register(chip, CD_DEGLITCH_EN, XD_CD_DEGLITCH_EN, 0x00);
		rts51x_write_register(chip,SD30_DRIVE_SEL,SD30_DRIVE_MASK,chip->option.sd30_pad_drive);
		rts51x_write_register(chip,CARD_DRIVE_SEL,SD20_DRIVE_MASK,chip->option.sd20_pad_drive);

		if(!chip->option.ww_enable)
		{
			if(CHECK_PKG(chip, LQFP48))
			{
				rts51x_write_register(chip, CARD_PULL_CTL3, 0x80, 0x80);
				rts51x_write_register(chip, CARD_PULL_CTL6, 0xf0, 0xA0);
			}
			else
			{
				rts51x_write_register(chip, CARD_PULL_CTL1, 0x30, 0x20);
				rts51x_write_register(chip, CARD_PULL_CTL3, 0x80, 0x80);		
				rts51x_write_register(chip, CARD_PULL_CTL6, 0x0c, 0x08);	
			}
		}		
	}

	if (chip->option.sd_ctl & SUPPORT_UHS50_MMC44) 
	{
		SET_UHS50(chip);
		RTS51X_DEBUGP(("option enable UHS50&MMC44,sd_ctl:0x%x \n",chip->option.sd_ctl));
	}
	else
	{
		if(CHECK_PID(chip, 0x0139)&&CHECK_PKG(chip, LQFP48))
		{
			SET_UHS50(chip);
			RTS51X_DEBUGP(("PID enable UHS50&MMC44\n"));
		}
		else
		{
			CLEAR_UHS50(chip);
			RTS51X_DEBUGP(("PID disable UHS50&MMC44\n"));
		}
	}

	if(chip->option.ms_errreg_fix && (chip->ic_version>1)){
		rts51x_write_register(chip, 0xFD4D, 0x01, 0x01);
	}
	
	retval = rts51x_write_phy_register(chip, 0xC2, 0x7C);
	if (retval != STATUS_SUCCESS) {
		TRACE_RET(chip, retval);
	}
	
	rts51x_init_cmd(chip);
	
	rts51x_add_cmd(chip, WRITE_REG_CMD, CARD_GPIO, GPIO_OE, GPIO_OE);
#ifdef LED_AUTO_BLINK
	rts51x_add_cmd(chip, WRITE_REG_CMD, CARD_AUTO_BLINK, 
		       BLINK_ENABLE|BLINK_SPEED_MASK, 
		       BLINK_ENABLE|chip->option.led_blink_speed);
#endif
	rts51x_add_cmd(chip, WRITE_REG_CMD, CARD_DMA1_CTL, 
		       EXTEND_DMA1_ASYNC_SIGNAL, EXTEND_DMA1_ASYNC_SIGNAL);
	
	retval = rts51x_send_cmd(chip, MODE_C, 100);
	if (retval != STATUS_SUCCESS) {
		TRACE_RET(chip, retval);
	}

#ifdef SUPPORT_OCP
	if(chip->asic_code)
	{
		rts51x_write_register(chip, OCPCTL, MS_OCP_DETECT_EN, MS_OCP_DETECT_EN);
		RTS51X_DEBUGP(("Enable OCP detect!\n"));
	}
#endif

	if(chip->option.FT2_fast_mode)
	{
		card_power_on(chip, SD_CARD|MS_CARD|XD_CARD);
		wait_timeout(10);
	}

#ifndef USING_POLLING_CYCLE_DELINK
	rts51x_clear_start_time(chip);
#endif
	
	return STATUS_SUCCESS;
}

int rts51x_init_chip(struct rts51x_chip *chip)
{
	int retval;
	u8 val;
	
	chip->max_lun = 0;
	chip->cur_clk = 0;
	chip->cur_card = 0;
	
	chip->card2lun[XD_CARD] = 0;
	chip->card2lun[SD_CARD] = 0;
	chip->card2lun[MS_CARD] = 0;
	chip->card_ejected = 0;

	chip->lun2card[0] = XD_CARD | SD_CARD | MS_CARD;

#ifdef CLOSE_SSC_POWER
	rts51x_write_register(chip, FPDCTL, SSC_POWER_MASK, SSC_POWER_ON);
	udelay(100);
	rts51x_write_register(chip, CLK_DIV, CLK_CHANGE, 0x00);
#endif	
	RTS51X_SET_STAT(chip, STAT_RUN);

	RTS51X_READ_REG(chip, HW_VERSION, &val);
	if((val&0x0f)>=2)
	{
		chip->option.rcc_bug_fix_en = 0;
	}
	RTS51X_DEBUGP(("rcc bug fix enable:%d\n",chip->option.rcc_bug_fix_en));
	RTS51X_DEBUGP(("HW_VERSION: 0x%x\n", val));
	if (val & FPGA_VER) {
		chip->asic_code = 0;
		RTS51X_DEBUGP(("FPGA!\n"));
	} else {
		chip->asic_code = 1;
		RTS51X_DEBUGP(("ASIC!\n"));
	}
	chip->ic_version = val & HW_VER_MASK;
	
	if (!check_sd_speed_prior(chip->option.sd_speed_prior)) {
		chip->option.sd_speed_prior = 0x01020403;
	}
	RTS51X_DEBUGP(("sd_speed_prior = 0x%08x\n", chip->option.sd_speed_prior));
	
	RTS51X_READ_REG(chip, CARD_SHARE_MODE, &val);
	if (val & CARD_SHARE_LQFP_SEL) {
		chip->package = LQFP48;
		RTS51X_DEBUGP(("Package: LQFP48\n"));
	} else {
		chip->package = QFN24;
		RTS51X_DEBUGP(("Package: QFN24\n"));
	}
	
	RTS51X_READ_REG(chip, HS_USB_STAT, &val);
	if (val & USB_HI_SPEED) {
		chip->usb_speed = USB_20;
		RTS51X_DEBUGP(("USB High Speed\n"));
	} else {
		chip->usb_speed = USB_11;
		RTS51X_DEBUGP(("USB Full Speed\n"));
	}
	
	retval = rts51x_reset_chip(chip);
	if (retval != STATUS_SUCCESS) {
		TRACE_RET(chip, STATUS_FAIL);
	}
	
	return STATUS_SUCCESS;
}

int rts51x_release_chip(struct rts51x_chip *chip)
{
	xd_free_l2p_tbl(chip);
	ms_free_l2p_tbl(chip);
	chip->card_ready = 0;
	return STATUS_SUCCESS;
}

#ifndef LED_AUTO_BLINK
static inline void rts51x_blink_led(struct rts51x_chip *chip)
{
	if (chip->card_ready) {
		if (chip->led_toggle_counter < chip->option.led_toggle_interval) {
			chip->led_toggle_counter ++;
		} else {
			chip->led_toggle_counter = 0;
			toggle_gpio(chip, LED_GPIO);
		}
	}
}
#endif

void rts51x_polling_func(struct rts51x_chip *chip)
{
#ifdef SUPPORT_SD_LOCK
	struct sd_info *sd_card = &(chip->sd_card);
	
	if (sd_card->sd_erase_status) {
		if (chip->card_exist & SD_CARD) {
			u8 val;
			rts51x_read_register(chip, SD_BUS_STAT, &val);
			if (val & SD_DAT0_STATUS) {
				sd_card->sd_erase_status = SD_NOT_ERASE;
				sd_card->sd_lock_notify = 1;
				
				sd_cleanup_work(chip);
				release_sd_card(chip);
				chip->card_ready &= ~SD_CARD;
				chip->card_exist &= ~SD_CARD;
				chip->rw_card[chip->card2lun[SD_CARD]] = NULL;
				clear_bit(chip->card2lun[SD_CARD], &(chip->lun_mc));
			}
		} else {
			sd_card->sd_erase_status = SD_NOT_ERASE;
		}
	}
#endif

	rts51x_init_cards(chip);

#ifdef SUPPORT_OCP
	if((chip->ocp_stat&(MS_OCP_NOW|MS_OCP_EVER))&&(chip->card_exist))
	{

		rts51x_prepare_run(chip);
	
		if(chip->card_exist&SD_CARD)
		{
			rts51x_write_register(chip, CARD_OE, SD_OUTPUT_EN, 0);
		}
		else if(chip->card_exist&MS_CARD)
		{
			rts51x_write_register(chip, CARD_OE, MS_OUTPUT_EN, 0);
		}
		else if(chip->card_exist&XD_CARD)
		{
			rts51x_write_register(chip, CARD_OE, XD_OUTPUT_EN, 0);	
		}
	}
#endif

	if (chip->idle_counter < IDLE_MAX_COUNT) {
		chip->idle_counter ++;
	} else {
		if (!RTS51X_CHK_STAT(chip, STAT_IDLE)) {
			RTS51X_DEBUGP(("Idle state!\n"));
			RTS51X_SET_STAT(chip, STAT_IDLE);

#ifndef LED_AUTO_BLINK
			chip->led_toggle_counter = 0;
#endif
			turn_off_led(chip, LED_GPIO);
#ifdef CLOSE_SSC_POWER
			if(!chip->card_ready)
			{
				rts51x_write_register(chip, CLK_DIV, CLK_CHANGE, CLK_CHANGE);
				rts51x_write_register(chip, FPDCTL, SSC_POWER_MASK, SSC_POWER_DOWN);
				RTS51X_DEBUGP(("Close SSC clock power!\n"));
			}
#endif
		}
	}
	
	switch (RTS51X_GET_STAT(chip)) {
	case STAT_RUN:
#ifndef LED_AUTO_BLINK
		rts51x_blink_led(chip);
#endif 
		do_remaining_work(chip);
		break;

	case STAT_IDLE:
		break;

	default:
		break;
	}

	if (chip->option.auto_delink_en && !chip->card_ready) {

#ifdef USING_POLLING_CYCLE_DELINK	
		if (chip->auto_delink_counter <= chip->option.delink_delay* 2) {
			if (chip->auto_delink_counter == chip->option.delink_delay) {

				clear_first_install_mark(chip);
					
				if (chip->card_exist) {
					if(!chip->card_ejected){
					RTS51X_DEBUGP(("False card inserted, do force delink\n"));
					rts51x_write_register(chip, AUTO_DELINK_EN, 
							      AUTO_DELINK | FORCE_DELINK, 
							      AUTO_DELINK | FORCE_DELINK);

					chip->auto_delink_counter = chip->option.delink_delay * 2 + 1;
					}
				} else {
					RTS51X_DEBUGP(("No card inserted, do delink\n"));
					rts51x_write_register(chip, AUTO_DELINK_EN, 
							      AUTO_DELINK, AUTO_DELINK);
				}
			}

			if (chip->auto_delink_counter == chip->option.delink_delay * 2) {
				RTS51X_DEBUGP(("Try to do force delink\n"));
				rts51x_write_register(chip, AUTO_DELINK_EN, 
						      AUTO_DELINK | FORCE_DELINK, 
						      AUTO_DELINK | FORCE_DELINK);
			}			
			
			chip->auto_delink_counter ++;
		}
#else  
		int retvalue;
		retvalue = rts51x_get_card_status(chip, &chip->card_status);
		if((retvalue==STATUS_SUCCESS)&&(!(chip->card_status&(SD_CD|MS_CD|XD_CD)))){
			if (rts51x_count_delink_time(chip)>= chip->option.delink_delay) {

					clear_first_install_mark(chip);
					
					RTS51X_DEBUGP(("No card inserted, do delink\n"));
					rts51x_write_register(chip, AUTO_DELINK_EN, 
							      AUTO_DELINK, AUTO_DELINK);
			}
		}
		if((retvalue==STATUS_SUCCESS)&&(chip->card_status&(SD_CD|MS_CD|XD_CD))){
			if(!chip->card_ejected)
			{
				if(chip->auto_delink_counter>1)
				{							
					if (rts51x_count_delink_time(chip) > chip->option.delink_delay*2) {
						RTS51X_DEBUGP(("Try to do force delink\n"));
						rts51x_write_register(chip, AUTO_DELINK_EN, 
							      AUTO_DELINK | FORCE_DELINK, 
							      AUTO_DELINK | FORCE_DELINK);
					}
				}		
			}
		}
		chip->auto_delink_counter++;
#endif
	} else {
		chip->auto_delink_counter = 0;
#ifndef USING_POLLING_CYCLE_DELINK		
		rts51x_clear_start_time(chip);
#endif
	}
}

void rts51x_add_cmd(struct rts51x_chip *chip, 
		    u8 cmd_type, 
		    u16 reg_addr, 
		    u8 mask, 
		    u8 data)
{
	int i;

	if (chip->cmd_idx < ((CMD_BUF_LEN - CMD_OFFSET) / 4)) {
		i = CMD_OFFSET + chip->cmd_idx * 4;

		chip->cmd_buf[i++] = ((cmd_type & 0x03) << 6) | (u8)((reg_addr >> 8) & 0x3F);
		chip->cmd_buf[i++] = (u8)reg_addr;
		chip->cmd_buf[i++] = mask;
		chip->cmd_buf[i++] = data;

		chip->cmd_idx ++;
	}
}

int rts51x_send_cmd(struct rts51x_chip *chip, u8 flag, int timeout)
{
	int result;
	
	chip->cmd_buf[CNT_H] = (u8)(chip->cmd_idx >> 8);
	chip->cmd_buf[CNT_L] = (u8)(chip->cmd_idx);
	chip->cmd_buf[STAGE_FLAG] = flag;
	
	result = rts51x_transfer_data_rcc(chip, SND_BULK_PIPE(chip), 
					  (void *)(chip->cmd_buf), 
					  chip->cmd_idx * 4 + CMD_OFFSET, 
					  0, NULL, timeout, MODE_C);
	if (result != STATUS_SUCCESS) {
		TRACE_RET(chip, result);
	}
	
	return STATUS_SUCCESS;
}

int rts51x_get_rsp(struct rts51x_chip *chip, int rsp_len, int timeout)
{
	int result;
	
	if (rsp_len <= 0) {
		TRACE_RET(chip, STATUS_ERROR);
	}
	
	if (rsp_len % 4) {
		rsp_len += (4 - rsp_len % 4);
	}

	result = rts51x_transfer_data_rcc(chip, RCV_BULK_PIPE(chip), 
					   (void *)chip->rsp_buf, rsp_len, 
					   0, NULL, timeout, STAGE_R);
	if (result != STATUS_SUCCESS) {
		TRACE_RET(chip, result);
	}
	
	return STATUS_SUCCESS;
}

int rts51x_get_card_status(struct rts51x_chip *chip, u16 *status)
{
	int retval;
	u16 val;
	
#ifdef GET_CARD_STATUS_USING_EPC
	retval = rts51x_get_epc_status(chip, &val);

	if(retval != STATUS_SUCCESS)
	{
		TRACE_RET(chip, retval);
	}
#else
	retval = rts51x_ctrl_transfer(chip, RCV_CTRL_PIPE(chip), 0x02, 0xC0, 
				      0, 0, &val, 2, 100);
	if (retval != STATUS_SUCCESS) {
		TRACE_RET(chip, retval);
	}
#endif
	
	if (status) {
		*status = val;
	}
	
	return STATUS_SUCCESS;
}

int rts51x_write_register(struct rts51x_chip *chip, u16 addr, u8 mask, u8 data)
{
	int retval;
	
	rts51x_init_cmd(chip);
	rts51x_add_cmd(chip, WRITE_REG_CMD, addr, mask, data);
	retval = rts51x_send_cmd(chip, MODE_C, 100);
	if (retval != STATUS_SUCCESS) {
		TRACE_RET(chip, STATUS_FAIL);
	}
	
	return STATUS_SUCCESS;
}

int rts51x_read_register(struct rts51x_chip *chip, u16 addr, u8 *data)
{
	int retval;
	
	if (data) {
		*data = 0;
	}
	rts51x_init_cmd(chip);
	rts51x_add_cmd(chip, READ_REG_CMD, addr, 0, 0);
	retval = rts51x_send_cmd(chip, MODE_CR, 100);
	if (retval != STATUS_SUCCESS) {
		TRACE_RET(chip, STATUS_FAIL);
	}
	
	retval = rts51x_get_rsp(chip, 1, 100);

#ifdef RCC_BUG_FIX_SP
	if((chip->option.rcc_fail_flag == 2)&&(chip->option.rcc_bug_fix_en==1))
	{
		rts51x_ep0_read_register(chip, addr, chip->rsp_buf);
		retval = STATUS_SUCCESS;
		rts51x_reset_pipe(chip,0);
		rts51x_ep0_write_register(chip, MC_FIFO_CTL, FIFO_FLUSH, FIFO_FLUSH);
		rts51x_ep0_write_register(chip, SFSM_ED, 0xf8, 0xf8);
	}
#endif
	
	if (retval != STATUS_SUCCESS) {
		TRACE_RET(chip, STATUS_FAIL);
	}
	
	if (data) {
		*data = chip->rsp_buf[0];
	}
	
	return STATUS_SUCCESS;
}

int rts51x_ep0_write_register(struct rts51x_chip *chip, u16 addr, u8 mask, u8 data)
{
	int retval;
	u16 value = 0, index = 0;

	value |= (u16)(3 & 0x03) << 14;
	value |= (u16)(addr & 0x3FFF);
	index |= (u16)mask << 8;
	index |= (u16)data;
	
	retval = rts51x_ctrl_transfer(chip, SND_CTRL_PIPE(chip), 0x00, 0x40, 
				      cpu_to_be16(value), cpu_to_be16(index), NULL, 0, 100);
	if (retval != STATUS_SUCCESS) {
		TRACE_RET(chip, retval);
	}
	
	return STATUS_SUCCESS;
}

int rts51x_ep0_read_register(struct rts51x_chip *chip, u16 addr, u8 *data)
{
	int retval;
	u16 value = 0;
	u8 val;
	
	if (data) {
		*data = 0;
	}
	
	value |= (u16)(2 & 0x03) << 14;
	value |= (u16)(addr & 0x3FFF);
	
	retval = rts51x_ctrl_transfer(chip, RCV_CTRL_PIPE(chip), 0x00, 0xC0, 
				      cpu_to_be16(value), 0, &val, 1, 100);
	if (retval != STATUS_SUCCESS) {
		TRACE_RET(chip, retval);
	}
	
	if (data) {
		*data = val;
	}
	
	return STATUS_SUCCESS;
}

int rts51x_seq_write_register(struct rts51x_chip *chip, u16 addr, u16 len, u8 *data)
{
	int result;
	u16 cmd_len = len + 12;
	
	if (!data) {
		TRACE_RET(chip, STATUS_ERROR);
	}
	
	cmd_len = (cmd_len <= CMD_BUF_LEN) ? cmd_len : CMD_BUF_LEN;
	
	if (cmd_len % 4) {
		cmd_len += (4 - cmd_len % 4);
	}
	
	chip->cmd_buf[0] = 'R';
	chip->cmd_buf[1] = 'T';
	chip->cmd_buf[2] = 'C';
	chip->cmd_buf[3] = 'R';
	chip->cmd_buf[PACKET_TYPE] = SEQ_WRITE;
	chip->cmd_buf[5] = (u8)(len >> 8);
	chip->cmd_buf[6] = (u8)len;
	chip->cmd_buf[STAGE_FLAG] = 0;
	chip->cmd_buf[8] = (u8)(addr >> 8);
	chip->cmd_buf[9] = (u8)addr;
	
	memcpy(chip->cmd_buf + 12, data, len);
	
	result = rts51x_transfer_data_rcc(chip, SND_BULK_PIPE(chip), 
				      (void *)(chip->cmd_buf), cmd_len, 0, NULL, 100, MODE_C);
	if (result != STATUS_SUCCESS) {
		TRACE_RET(chip, result);
	}
	
	return STATUS_SUCCESS;
}

int rts51x_seq_read_register(struct rts51x_chip *chip, u16 addr, u16 len, u8 *data)
{
	int result;
	u16 rsp_len;
	
	if (!data) {
		TRACE_RET(chip, STATUS_ERROR);
	}
	
	if (len % 4) {
		rsp_len = len + (4 - len % 4);
	} else {
		rsp_len = len;
	}
	
	chip->cmd_buf[0] = 'R';
	chip->cmd_buf[1] = 'T';
	chip->cmd_buf[2] = 'C';
	chip->cmd_buf[3] = 'R';
	chip->cmd_buf[PACKET_TYPE] = SEQ_READ;
	chip->cmd_buf[5] = (u8)(rsp_len >> 8);
	chip->cmd_buf[6] = (u8)rsp_len;
	chip->cmd_buf[STAGE_FLAG] = STAGE_R;
	chip->cmd_buf[8] = (u8)(addr >> 8);
	chip->cmd_buf[9] = (u8)addr;
	
	result = rts51x_transfer_data_rcc(chip, SND_BULK_PIPE(chip), 
				      (void *)(chip->cmd_buf), 12, 0, NULL, 100, MODE_C);
	if (result != STATUS_SUCCESS) {
		TRACE_RET(chip, result);
	}
	
	result = rts51x_transfer_data_rcc(chip, RCV_BULK_PIPE(chip), 
				       (void *)data, rsp_len, 0, NULL, 100, STAGE_DI);
	if (result != STATUS_SUCCESS) {
		TRACE_RET(chip, result);
	}
	
	return STATUS_SUCCESS;
}

int rts51x_read_ppbuf(struct rts51x_chip *chip, u8 *buf, int buf_len)
{
	int retval;
	
	if (!buf) {
		TRACE_RET(chip, STATUS_ERROR);
	}
	
	retval = rts51x_seq_read_register(chip, PPBUF_BASE2, (u16)buf_len, buf);
	if (retval != STATUS_SUCCESS) {
		TRACE_RET(chip, retval);
	}
	
	return STATUS_SUCCESS;
}

int rts51x_write_ppbuf(struct rts51x_chip *chip, u8 *buf, int buf_len)
{
	int retval;
	
	if (!buf) {
		TRACE_RET(chip, STATUS_ERROR);
	}
	
	retval = rts51x_seq_write_register(chip, PPBUF_BASE2, (u16)buf_len, buf);
	if (retval != STATUS_SUCCESS) {
		TRACE_RET(chip, retval);
	}
	
	return STATUS_SUCCESS;
}

int rts51x_write_phy_register(struct rts51x_chip *chip, u8 addr, u8 val)
{
	int retval;
	
	RTS51X_DEBUGP(("Write 0x%x to phy register 0x%x\n", val, addr));
	
	rts51x_init_cmd(chip);
	
	rts51x_add_cmd(chip, WRITE_REG_CMD, HS_VSTAIN, 0xFF, val);
	rts51x_add_cmd(chip, WRITE_REG_CMD, HS_VCONTROL, 0xFF, addr & 0x0F);
	rts51x_add_cmd(chip, WRITE_REG_CMD, HS_VLOADM, 0xFF, 0x00);
	rts51x_add_cmd(chip, WRITE_REG_CMD, HS_VLOADM, 0xFF, 0x00);
	rts51x_add_cmd(chip, WRITE_REG_CMD, HS_VLOADM, 0xFF, 0x01);
	rts51x_add_cmd(chip, WRITE_REG_CMD, HS_VCONTROL, 0xFF, (addr >> 4) & 0x0F);
	rts51x_add_cmd(chip, WRITE_REG_CMD, HS_VLOADM, 0xFF, 0x00);
	rts51x_add_cmd(chip, WRITE_REG_CMD, HS_VLOADM, 0xFF, 0x00);
	rts51x_add_cmd(chip, WRITE_REG_CMD, HS_VLOADM, 0xFF, 0x01);
	
	retval = rts51x_send_cmd(chip, MODE_C, 100);
	if (retval != STATUS_SUCCESS) {
		TRACE_RET(chip, retval);
	}
	
	return STATUS_SUCCESS;
}

int rts51x_read_phy_register(struct rts51x_chip *chip, u8 addr, u8 *val)
{
	int retval;
	
	RTS51X_DEBUGP(("Read from phy register 0x%x\n", addr));
	
	rts51x_init_cmd(chip);
	
	rts51x_add_cmd(chip, WRITE_REG_CMD, HS_VCONTROL, 0xFF, 0x07);
	rts51x_add_cmd(chip, WRITE_REG_CMD, HS_VLOADM, 0xFF, 0x00);
	rts51x_add_cmd(chip, WRITE_REG_CMD, HS_VLOADM, 0xFF, 0x00);
	rts51x_add_cmd(chip, WRITE_REG_CMD, HS_VLOADM, 0xFF, 0x01);
	rts51x_add_cmd(chip, WRITE_REG_CMD, HS_VCONTROL, 0xFF, (addr >> 4) & 0x0F);
	rts51x_add_cmd(chip, WRITE_REG_CMD, HS_VLOADM, 0xFF, 0x00);
	rts51x_add_cmd(chip, WRITE_REG_CMD, HS_VLOADM, 0xFF, 0x00);
	rts51x_add_cmd(chip, WRITE_REG_CMD, HS_VLOADM, 0xFF, 0x01);
	rts51x_add_cmd(chip, WRITE_REG_CMD, HS_VCONTROL, 0xFF, addr & 0x0F);
	rts51x_add_cmd(chip, WRITE_REG_CMD, HS_VLOADM, 0xFF, 0x00);
	rts51x_add_cmd(chip, WRITE_REG_CMD, HS_VLOADM, 0xFF, 0x00);
	rts51x_add_cmd(chip, WRITE_REG_CMD, HS_VLOADM, 0xFF, 0x01);
	rts51x_add_cmd(chip, READ_REG_CMD, HS_VSTAOUT, 0, 0);
	
	retval = rts51x_send_cmd(chip, MODE_CR, 100);
	if (retval != STATUS_SUCCESS) {
		TRACE_RET(chip, retval);
	}
	
	retval = rts51x_get_rsp(chip, 1, 100);

#ifdef RCC_BUG_FIX_SP
	if((chip->option.rcc_fail_flag == 2)&&(chip->option.rcc_bug_fix_en==1))
	{
		chip->rcc_read_response = 0;
		rts51x_ep0_read_register(chip, HS_VSTAOUT, chip->rsp_buf);
		retval = STATUS_SUCCESS;
		rts51x_reset_pipe(chip,0);
		rts51x_ep0_write_register(chip, MC_FIFO_CTL, FIFO_FLUSH, FIFO_FLUSH);
		rts51x_ep0_write_register(chip, SFSM_ED, 0xf8, 0xf8);
	}
#endif
	
	if (retval != STATUS_SUCCESS) {
		TRACE_RET(chip, retval);
	}
	
	if (val) {
		*val = chip->rsp_buf[0];
	}
	
	RTS51X_DEBUGP(("Return value: 0x%x\n", chip->rsp_buf[0]));
	
	return STATUS_SUCCESS;
}

void rts51x_do_before_power_down(struct rts51x_chip *chip)
{
	RTS51X_DEBUGP(("rts51x_do_before_power_down\n"));

	rts51x_prepare_run(chip);

	rts51x_release_cards(chip);
	turn_off_led(chip, LED_GPIO);
	
	chip->cur_clk = 0;
	chip->card_exist = 0;
	chip->cur_card = 0;

	if(chip->asic_code&&!chip->option.ww_enable)
	{
		if(CHECK_PKG(chip, LQFP48))
		{
			rts51x_write_register(chip, CARD_PULL_CTL3, 0x80, 0x00);
			rts51x_write_register(chip, CARD_PULL_CTL6, 0xf0, 0x50);
		}
		else
		{
			rts51x_write_register(chip, CARD_PULL_CTL1, 0x30, 0x10);
			rts51x_write_register(chip, CARD_PULL_CTL3, 0x80, 0x00);		
			rts51x_write_register(chip, CARD_PULL_CTL6, 0x0c, 0x04);	
		}
	}
		
	if(CHECK_PKG(chip, LQFP48)){ 
		rts51x_write_register(chip, CARD_PWR_CTL, LDO3318_PWR_MASK, LDO_OFF);
	}
}

void rts51x_clear_hw_error(struct rts51x_chip *chip)
{
	rts51x_ep0_write_register(chip, SFSM_ED, 0xf8, 0xf8);
}

void rts51x_prepare_run(struct rts51x_chip *chip)
{
#ifdef CLOSE_SSC_POWER
	if (RTS51X_CHK_STAT(chip, STAT_IDLE) && (!chip->card_ready)) {
		rts51x_write_register(chip, FPDCTL, SSC_POWER_MASK, SSC_POWER_ON);
		udelay(100);
		RTS51X_DEBUGP(("Open SSC clock power.\n"));
		
		rts51x_write_register(chip, CLK_DIV, CLK_CHANGE, 0x00);
	}
#endif


}

#ifdef _MSG_TRACE
void rts51x_trace_msg(struct rts51x_chip *chip, unsigned char *buf, int clear)
{
	unsigned char *ptr;
	int i, msg_cnt;
	
	if (!buf) {
		return;
	}
	
	ptr = buf;

	if (chip->trace_msg[chip->msg_idx].valid) {
		msg_cnt = TRACE_ITEM_CNT;
	} else {
		msg_cnt = chip->msg_idx;
	}
	*(ptr++) = (u8)(msg_cnt >> 24);
	*(ptr++) = (u8)(msg_cnt >> 16);
	*(ptr++) = (u8)(msg_cnt >> 8);
	*(ptr++) = (u8)msg_cnt;
	RTS51X_DEBUGP(("Trace message count is %d\n", msg_cnt));

	for (i = 1; i <= msg_cnt; i++) {
		int j, idx;

		idx = chip->msg_idx - i;
		if (idx < 0) {
			idx += TRACE_ITEM_CNT;
		}
				
		*(ptr++) = (u8)(chip->trace_msg[idx].line >> 8);
		*(ptr++) = (u8)(chip->trace_msg[idx].line);
		for (j = 0; j < MSG_FUNC_LEN; j++) {
			*(ptr++) = chip->trace_msg[idx].func[j];
		}
		for (j = 0; j < MSG_FILE_LEN; j++) {
			*(ptr++) = chip->trace_msg[idx].file[j];
		}
		for (j = 0; j < TIME_VAL_LEN; j++) {
			*(ptr++) = chip->trace_msg[idx].timeval_buf[j];
		}
	}

	if (clear) {
		chip->msg_idx = 0;
		for (i = 0; i < TRACE_ITEM_CNT; i++) {
			chip->trace_msg[i].valid = 0;
		}
	}
}
#endif

void rts51x_pp_status(struct rts51x_chip *chip, unsigned int lun, u8 *status, u8 status_len)
{
	struct sd_info *sd_card = &(chip->sd_card);
	struct ms_info *ms_card = &(chip->ms_card);
	u8 card = get_lun_card(chip, lun);
#ifdef SUPPORT_OC
	u8 oc_now_mask = 0, oc_ever_mask = 0;
#endif
	
	if (!status || (status_len < 32)) {
		return;
	}
	
	status[0] = (u8)RTS51X_GET_PID(chip);
	status[1] = (u8)(chip->ic_version);

	if (chip->option.auto_delink_en) {
		status[2] = 0x10;
	} else {
		status[2] = 0x00;
	}
	
	status[3] = 20;
	status[4] = 10;
	status[5] = 05;
	status[6] = 21;

	if (chip->card_wp) {
		status[7] = 0x20;
	} else {
		status[7] = 0x00;
	}

#ifdef SUPPORT_OC
	status[8] = 0;
	oc_now_mask = MS_OCP_NOW;
	oc_ever_mask = MS_OCP_EVER;
	
	if (chip->ocp_stat & oc_now_mask) {
		status[8] |= 0x02;
	}
	if (chip->ocp_stat & oc_ever_mask) {
		status[8] |= 0x01;
	}
#endif

	if (card == SD_CARD) {
		if (CHK_SD(sd_card)) {
			if (CHK_SD_HCXC(sd_card)) {
				if (sd_card->capacity > 0x4000000) {
					status[0x0E] = 0x02;  
				} else {
					status[0x0E] = 0x01;  
				}
			} else {
				status[0x0E] = 0x00;  
			}
			
			if (CHK_SD_SDR104(sd_card)) {
				status[0x0F] = 0x03;
			} else if (CHK_SD_DDR50(sd_card)) {
				status[0x0F] = 0x04;
			} else if (CHK_SD_SDR50(sd_card)) {
				status[0x0F] = 0x02;
			} else if (CHK_SD_HS(sd_card)) {
				status[0x0F] = 0x01;
			} else {
				status[0x0F] = 0x00;  
			}
		} else {
			if (CHK_MMC_SECTOR_MODE(sd_card)) {
				status[0x0E] = 0x01;  
			} else {
				status[0x0E] = 0x00;  
			}
			
			if (CHK_MMC_DDR52(sd_card)) {
				status[0x0F] = 0x03;  
			} else if (CHK_MMC_52M(sd_card)) {
				status[0x0F] = 0x02;  
			} else if (CHK_MMC_26M(sd_card)) {
				status[0x0F] = 0x01;  
			} else {
				status[0x0F] = 0x00;  
			}
		}
	} else if (card == MS_CARD) {
		if (CHK_MSPRO(ms_card)) {
			if (CHK_MSXC(ms_card)) {
				status[0x0E] = 0x01;  
			} else {
				status[0x0E] = 0x00;
			}
			
			if (CHK_HG8BIT(ms_card)) {
				status[0x0F] = 0x01;
			} else {
				status[0x0F] = 0x00;
			}
		}
	}

#ifdef SUPPORT_SD_LOCK
	if (card == SD_CARD) {
		status[0x17] = 0x80;
		if (sd_card->sd_erase_status) {
			status[0x17] |= 0x01;	
		}
		if (sd_card->sd_lock_status & SD_LOCKED) {
			status[0x17] |= 0x02;	
			status[0x07] |= 0x40;	
		}
		if (sd_card->sd_lock_status & SD_PWD_EXIST) {
			status[0x17] |= 0x04;	
		}
	} else {
		status[0x17] = 0x00;
	}
	
	RTS51X_DEBUGP(("status[0x17] = 0x%x\n", status[0x17]));
#endif

	status[0x18] = 0x8A;

	status[0x1A] = 0x28;
	
#ifdef SUPPORT_SD_LOCK
	status[0x1F] = 0x01;
#endif

	status[0x1A] = 0x28;
}

void rts51x_read_status(struct rts51x_chip *chip, unsigned int lun, u8 *rts51x_status, u8 status_len)
{
	if (!rts51x_status || (status_len < 16)) {
		return;
	}
	
	rts51x_status[0] = (u8)(RTS51X_GET_VID(chip) >> 8);
	rts51x_status[1] = (u8)RTS51X_GET_VID(chip);

	rts51x_status[2] = (u8)(RTS51X_GET_PID(chip) >> 8);
	rts51x_status[3] = (u8)RTS51X_GET_PID(chip);

	rts51x_status[4] = (u8)lun;

	if (chip->card_exist) {
		if (chip->card_exist & XD_CARD) {
			rts51x_status[5] = 4;		
		} else if (chip->card_exist & SD_CARD) {
			rts51x_status[5] = 2;		
		} else if (chip->card_exist & MS_CARD) {
			rts51x_status[5] = 3;		
		} else {
			rts51x_status[5] = 7;		
		}
	} else {
		rts51x_status[5] = 7;			
	}

	rts51x_status[6] = 1;

	rts51x_status[7] = (u8)RTS51X_GET_PID(chip);;
	rts51x_status[8] = chip->ic_version;

	if (check_card_exist(chip, lun)) {
		rts51x_status[9] = 1;
	} else {
		rts51x_status[9] = 0;
	}

	rts51x_status[10] = 1;

	rts51x_status[11] = XD_CARD | SD_CARD | MS_CARD;

	if (check_card_ready(chip, lun)) {
		rts51x_status[12] = 1;
	} else {
		rts51x_status[12] = 0;
	}

	if (get_lun_card(chip, lun) == XD_CARD) {
		rts51x_status[13] = 0x40;
	} else if (get_lun_card(chip, lun) == SD_CARD) {
		struct sd_info *sd_card = &(chip->sd_card);
		
		rts51x_status[13] = 0x20;
		if (CHK_SD(sd_card)) {
			if (CHK_SD_HCXC(sd_card)) {
				rts51x_status[13] |= 0x04;	
			} 
			if (CHK_SD_HS(sd_card)) {
				rts51x_status[13] |= 0x02;	
			}
		} else {
			rts51x_status[13] |= 0x08;		
			if (CHK_MMC_52M(sd_card)) {
				rts51x_status[13] |= 0x02;	
			}
			if (CHK_MMC_SECTOR_MODE(sd_card)) {
				rts51x_status[13] |= 0x04;	
			}
		}
	} else if (get_lun_card(chip, lun) == MS_CARD) {
		struct ms_info *ms_card = &(chip->ms_card);

		if (CHK_MSPRO(ms_card)) {
			rts51x_status[13] = 0x38;		
			if (CHK_HG8BIT(ms_card)) {
				rts51x_status[13] |= 0x04;	
			} 
#ifdef SUPPORT_MSXC
			if (CHK_MSXC(ms_card)) {
				rts51x_status[13] |= 0x01;	
			}
#endif
		} else {
			rts51x_status[13] = 0x30;
		}
	} else {
			rts51x_status[13] = 0x70;
	}

	rts51x_status[14] = 0x78;

	rts51x_status[15] = 0x82;
}


int rts51x_transfer_data_rcc(struct rts51x_chip *chip, unsigned int pipe, 
			      void *buf, unsigned int len, int use_sg, 
			      unsigned int *act_len, int timeout, u8 stage_flag)
{
	int retval;

	retval=rts51x_transfer_data(chip, pipe, buf, len, use_sg, act_len, timeout);

#ifdef RCC_BUG_FIX_SP
	if(retval!=STATUS_SUCCESS)
	{
		u8 tmpvalue,tmpvalue1;
		u16 i, readpointer;
		u32 tcvalue;
		u16 bcvalue,oldbcvalue;
		u8 pollingnum;	
	
		if((chip->option.rcc_fail_flag == 2)&&(chip->option.rcc_bug_fix_en==1))
		{
			if(stage_flag&STAGE_DI)
			{
				rts51x_ep0_read_register(chip, SFSM_ED, &tmpvalue);
				if(!(tmpvalue&CARD_ERR))	
				{

					pollingnum = 0;
					if(CHECK_USB(chip, USB_20))
					{
						do
						{
							rts51x_ep0_read_register(chip, MC_FIFO_STAT, &tmpvalue1);
							if(tmpvalue1&FIFO_FULL)
							{
								break;
							}
							rts51x_ep0_read_register(chip, MC_DMA_CTL, &tmpvalue1);
							if(tmpvalue1&DMA_TC_EQ_0)
							{
								break;
							}
							udelay(100);
							pollingnum++;
						}while(pollingnum<10);
					}
					else 
					{
						bcvalue =0x5555; 
						do{
							rts51x_ep0_read_register(chip, MC_DMA_CTL, &tmpvalue1);
							if(tmpvalue1&DMA_TC_EQ_0)
							{
								break;
							}

							oldbcvalue = bcvalue;
							rts51x_ep0_read_register(chip, MC_FIFO_BC1, &tmpvalue1);
							bcvalue = tmpvalue1;
							bcvalue<<=8;
							rts51x_ep0_read_register(chip, MC_FIFO_BC0, &tmpvalue1);
							bcvalue += tmpvalue1;
							RTS51X_DEBUGP(("FS read install,bcvalue:%d\n",bcvalue));

							if(!(pollingnum%3)&&(bcvalue==oldbcvalue))
							{
								break;
							}
							udelay(100);
							pollingnum ++;
						}while(pollingnum<10);
					}

					rts51x_ep0_read_register(chip, MC_FIFO_BC1, &tmpvalue1);
					bcvalue = tmpvalue1;
					bcvalue<<=8;
					rts51x_ep0_read_register(chip, MC_FIFO_BC0, &tmpvalue1);
					bcvalue += tmpvalue1;
					RTS51X_DEBUGP(("read install,bcvalue:%d\n",bcvalue));

					if(bcvalue)
					{
						rts51x_ep0_read_register(chip, MC_FIFO_RD_PTR1, &tmpvalue1);
						readpointer = tmpvalue1;
						readpointer<<=8;
						rts51x_ep0_read_register(chip, MC_FIFO_RD_PTR0, &tmpvalue1);
						readpointer+=tmpvalue1;
						RTS51X_DEBUGP(("read install,buff have data, read pointer:%d\n",readpointer));

						rts51x_ep0_read_register(chip, MC_DMA_TC3, &tmpvalue1);
						tcvalue = tmpvalue1;
						tcvalue<<=8;
						rts51x_ep0_read_register(chip, MC_DMA_TC2, &tmpvalue1);
						tcvalue+=tmpvalue1;
						tcvalue<<=8;
						rts51x_ep0_read_register(chip, MC_DMA_TC1, &tmpvalue1);
						tcvalue+=tmpvalue1;
						tcvalue<<=8;
						rts51x_ep0_read_register(chip, MC_DMA_TC0, &tmpvalue1);
						tcvalue+=tmpvalue1;
						RTS51X_DEBUGP(("read intall,DMA tc value:%d\n",tcvalue));
						
							for(i=0; i<bcvalue;i++)
							{
								rts51x_ep0_read_register(chip, (RBUF_BASE+(readpointer&RBUF_SIZE_MASK)+i)&RBUF_SIZE_MASK, &tmpvalue1);
								*((unsigned char*)buf+len-tcvalue-bcvalue+i) =tmpvalue1;
							}
/*						else
						{
							for(i=0; i<bcvalue; i++)
							{
								rts51x_ep0_read_register(chip, (RBUF_BASE+(readpointer&RBUF_SIZE_MASK)+i)&RBUF_SIZE_MASK, &tmpvalue1);
								*((unsigned char*)buf+len-bcvalue+i) =tmpvalue1;
							}
						}*/

						rts51x_ep0_write_register(chip, RCCTL, U_HW_CMD_EN_MASK, U_HW_CMD_DIS);
						rts51x_reset_pipe(chip,0);
						rts51x_ep0_write_register(chip, SFSM_ED, HW_CMD_STOP, HW_CMD_STOP);		
						if(tcvalue)
						{
							retval=rts51x_transfer_data(chip, pipe, ((unsigned char*)buf+len-tcvalue), tcvalue, use_sg, act_len, timeout);	
						}
						else
						{
							rts51x_ep0_write_register(chip, MC_FIFO_CTL, FIFO_FLUSH, FIFO_FLUSH);
							retval = STATUS_SUCCESS;
						}
						rts51x_ep0_write_register(chip, RCCTL, U_HW_CMD_EN_MASK, U_HW_CMD_EN);
					}
					chip->rcc_read_response = 1;
				}
			}
		}
		
	}
#endif

	return retval;
	
}
