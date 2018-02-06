/*****************************************************************************
* Copyright(c) O2Micro, 2013. All rights reserved.
*	
* O2Micro OZ8806 battery gauge driver
* File: seaelf_charger.c

* Author: Eason.yuan
* $Source: /data/code/CVS
* $Revision:  $
*
* This program is free software and can be edistributed and/or modify
* it under the terms of the GNU General Public License version 2 as
* published by the Free Software Foundation.
*	
* This Source Code Reference Design for O2MICRO OZ8806 access (\u201cReference Design\u201d) 
* is sole for the use of PRODUCT INTEGRATION REFERENCE ONLY, and contains confidential 
* and privileged information of O2Micro International Limited. O2Micro shall have no 
* liability to any PARTY FOR THE RELIABILITY, SERVICEABILITY FOR THE RESULT OF PRODUCT 
* INTEGRATION, or results from: (i) any modification or attempted modification of the 
* Reference Design by any party, or (ii) the combination, operation or use of the 
* Reference Design with non-O2Micro Reference Design.
*****************************************************************************/

//#define TEST_DEBUG		1	//test debug, open sysfs port and add polling timer
//#define DEBUG_ILMT		1	//detect ILMT from high to low, test only 


#include <linux/device.h>
#include <linux/jiffies.h>
#include <linux/delay.h>
#include <mach/gpio.h>
#include <linux/interrupt.h>
#include <linux/pm.h>
#include <linux/platform_device.h>

#include "seaelf_charger.h"
#include "parameter.h"

#include <asm/gpio.h>
#include <asm/irq.h>
#include <asm/io.h>

#define VERSION		   "2013.12.02/4.00.03"	

#define DEBUG_LOG   		1
#define IR_COMPENSATION		1

#define	RETRY_CNT	5

static struct seaelf_platform_data  seaelf_pdata_default = {
        .max_charger_currentmA = 1500,
        .max_charger_voltagemV = 4350,
        .termination_currentmA = 160,
		.T34_charger_voltagemV = 4250,    //dangous
		.T45_charger_voltagemV = 4200,    //dangous
		.wakeup_voltagemV = 2500,
		.wakeup_currentmA = 250,
		.recharge_voltagemV = 100,
		.min_vsys_voltagemV = 3000,
		.vbus_limit_currentmA = 2000,
		.max_cc_charge_time = 0,
		.max_wakeup_charge_time = 30,
		.rthm_10k = 0,			//1 for 10K, 0 for 100K thermal resistor
		.inpedance = 60,
};

static enum power_supply_property seaelf_charger_props[] = {
        POWER_SUPPLY_PROP_STATUS,
        POWER_SUPPLY_PROP_HEALTH,
        POWER_SUPPLY_PROP_ONLINE,
        POWER_SUPPLY_PROP_CURRENT_MAX,
};

struct seaelf_charger_info *charger_info;

static int seaelf_init(void);
static int seaelf_set_vubs_current(int ilmt_ma);


/*****************************************************************************
 * Description:
 *		seaelf_read_byte 
 * Parameters:
 *		index:	register index to be read
 *		*dat:	buffer to store data read back
 * Return:
 *		I2C_STATUS_OK if read success, else inidcate error
 *****************************************************************************/
 int32_t seaelf_read_byte(uint8_t index, uint8_t *dat)
{
	int32_t ret;
	uint8_t i;

	for(i = 0; i < RETRY_CNT; i++){
		ret = i2c_smbus_read_byte_data(charger_info->client, index);
		if(ret >= 0) break;
	}
	if(i >= RETRY_CNT)
	{
		return ret;
	} 
	*dat = (uint8_t)ret;

	return ret;
}


/*****************************************************************************
 * Description:
 *		seaelf_write_byte 
 * Parameters:
 *		index:	register index to be write
 *		dat:		write data
 * Return:
 *		see I2C_STATUS_XXX define
 *****************************************************************************/
 int32_t seaelf_write_byte(uint8_t index, uint8_t dat)
{
	int32_t ret;
	uint8_t i;
	
	for(i = 0; i < RETRY_CNT; i++){
		ret = i2c_smbus_write_byte_data(charger_info->client, index,dat);
		if(ret >= 0) break;
	}
	if(i >= RETRY_CNT)
	{
		return ret;
	}

	return ret;
}





/*****************************************************************************
 * Description:
 *		seaelf_set_min_vsys 
 * Parameters:
 *		min_vsys_mv:	min sys voltage to be written
 * Return:
 *		see I2C_STATUS_XXX define
 *****************************************************************************/
static int seaelf_set_min_vsys(int min_vsys_mv)
{
	int ret = 0;
	u8 data = 0;
	u8 vsys_val = 0; 
	if (min_vsys_mv < 1800)
		min_vsys_mv = 1800;		//limit to 1.8V
	if (min_vsys_mv > 3600)
		min_vsys_mv = 3600;		//limit to 3.6V
	vsys_val = min_vsys_mv / VSYS_VOLT_STEP;	//step is 200mV

	ret = seaelf_read_byte(REG_MIN_VSYS_VOLTAGE,&data);
	if (ret < 0)
		return ret;
	if (data != vsys_val) 
		ret = seaelf_write_byte(REG_MIN_VSYS_VOLTAGE,vsys_val);
	return ret;
	
}
/*****************************************************************************
 * Description:
 *		seaelf_set_chg_volt 
 * Parameters:
 *		chgvolt_mv:	charge voltage to be written
 * Return:
 *		see I2C_STATUS_XXX define
 *****************************************************************************/
static int seaelf_set_chg_volt(int chgvolt_mv)
{
	int ret = 0;
	u8 data = 0;
	u8 chg_volt = 0; 
	if (chgvolt_mv < 4000)
		chgvolt_mv = 4000;		//limit to 4.0V
	if (chgvolt_mv > 4400)
		chgvolt_mv = 4400;		//limit to 4.4V
	chg_volt = chgvolt_mv / CHG_VOLT_STEP;	//step is 25mV

	ret = seaelf_read_byte(REG_CHARGER_VOLTAGE, &data);
	if (ret < 0)
		return ret;
	if (data != chg_volt) 
		ret = seaelf_write_byte(REG_CHARGER_VOLTAGE, chg_volt);	
	return ret;
	
}

/*****************************************************************************
 * Description:
 *		seaelf_set_t34_cv
 * Parameters:
 *		chgvolt_mv:	charge voltage to be written at t34
 * Return:
 *		see I2C_STATUS_XXX define
 *****************************************************************************/
static int seaelf_set_t34_cv(int chgvolt_mv)
{
	int ret = 0;
	u8 data = 0;
	u8 chg_volt = 0; 
	if (chgvolt_mv < 4000)
		chgvolt_mv = 4000;		//limit to 4.0V
	if (chgvolt_mv > 4400)
		chgvolt_mv = 4400;		//limit to 4.4V
	chg_volt = chgvolt_mv / CHG_VOLT_STEP;	//step is 25mV

	ret = seaelf_read_byte(REG_T34_CHG_VOLTAGE, &data);
	if (ret < 0)
		return ret;
	if (data != chg_volt) 
		ret = seaelf_write_byte(REG_T34_CHG_VOLTAGE, chg_volt);
		
	return ret;
	 
	
}

/*****************************************************************************
 * Description:
 *		seaelf_set_t45_cv 
 * Parameters:
 *		chgvolt_mv:	charge voltage to be written at t45
 * Return:
 *		see I2C_STATUS_XXX define
 *****************************************************************************/
static int seaelf_set_t45_cv(int chgvolt_mv)
{
	int ret = 0;
	u8 data = 0;
	u8 chg_volt = 0; 
	if (chgvolt_mv < 4000)
		chgvolt_mv = 4000;		//limit to 4.0V
	if (chgvolt_mv > 4400)
		chgvolt_mv = 4400;		//limit to 4.4V
	chg_volt = chgvolt_mv / CHG_VOLT_STEP;	//step is 25mV

	ret = seaelf_read_byte(REG_T45_CHG_VOLTAGE, &data);
	if (ret < 0)
		return ret;
	if (data != chg_volt) 
		ret = seaelf_write_byte(REG_T45_CHG_VOLTAGE, chg_volt);
	return ret;
}

/*****************************************************************************
 * Description:
 *		seaelf_set_wakeup_volt 
 * Parameters:
 *		wakeup_mv:	set wake up voltage
 * Return:
 *		see I2C_STATUS_XXX define
 *****************************************************************************/
static int seaelf_set_wakeup_volt(int wakeup_mv)
{
	int ret = 0;
	u8 data = 0;
	u8 wak_volt = 0; 
	if (wakeup_mv < 1500)
		wakeup_mv = 1500;		//limit to 1.5V
	if (wakeup_mv > 3000)
		wakeup_mv = 3000;		//limit to 3.0V
	wak_volt = wakeup_mv / WK_VOLT_STEP;	//step is 100mV

	ret = seaelf_read_byte(REG_WAKEUP_VOLTAGE, &data);
	if (ret < 0)
		return ret;
	if (data != wak_volt) 
		ret = seaelf_write_byte(REG_WAKEUP_VOLTAGE, wak_volt);	
	return ret;
}

/*****************************************************************************
 * Description:
 *		seaelf_set_eoc_current 
 * Parameters:
 *		eoc_ma:	set end of charge current
 * Return:
 *		see I2C_STATUS_XXX define
 *****************************************************************************/
static int seaelf_set_eoc_current(int eoc_ma)
{
	int ret = 0;
	u8 data = 0;
	u8 eoc_curr = 0; 
	if (eoc_ma <= 0)
		eoc_curr = 0;		//min value is 0mA
	//else if (eoc_ma > 160) {
		//eoc_ma = 160;		//limit to 160mA
		
	//}

	eoc_curr = eoc_ma / EOC_CURRT_STEP;	//step is 10mA

	ret = seaelf_read_byte(REG_END_CHARGE_CURRENT, &data);
	if (ret < 0)
		return ret;
	if (data != eoc_curr) 
		ret = seaelf_write_byte(REG_END_CHARGE_CURRENT, eoc_curr);	
	return ret;
}

/*****************************************************************************
 * Description:
 *		seaelf_set_input_current_limit 
 * Parameters:
 *		ilmt_ma:	set input current limit
 * Return:
 *		see I2C_STATUS_XXX define
 *****************************************************************************/
static int seaelf_set_vubs_current(int ilmt_ma)
{
	int ret = 0;
	u8 data = 0;
	u8 input_curr = 0;

	/*
	if (ilmt_ma < 100)
		ilmt_ma = 100;		//limit to 100mA
	if (ilmt_ma > 2000)
		ilmt_ma = 2000;		//limit to 2000mA
		*/
	input_curr = ilmt_ma / VBUS_ILMT_STEP;	//step is 100mA

	ret = seaelf_read_byte(REG_VBUS_LIMIT_CURRENT, &data);
	if (ret < 0)
		return ret;
	if (data != input_curr) 
		ret = seaelf_write_byte(REG_VBUS_LIMIT_CURRENT, input_curr);
	return ret;
}

/*****************************************************************************
 * Description:
 *		seaelf_set_rechg_hystersis
 * Parameters:
 *		hyst_mv:	set Recharge hysteresis Register
 * Return:
 *		see I2C_STATUS_XXX define
 *****************************************************************************/
static int seaelf_set_rechg_hystersis(int hyst_mv)
{
	int ret = 0;
	u8 data = 0;
	u8 rechg = 0; 
	if (hyst_mv == 0)
		rechg = 0;				//set 0 to disable recharge hysteresis
	else if (hyst_mv > 200) {
		hyst_mv = 200;			//limit to 200mV
		rechg = hyst_mv / RECHG_VOLT_STEP;	//step is 50mV
	}

	ret = seaelf_read_byte(REG_RECHARGE_HYSTERESIS, &data);
	if (ret < 0)
		return ret;
	if (data != rechg) 
		ret = seaelf_write_byte(REG_RECHARGE_HYSTERESIS, rechg);	
	return ret;
}

/*****************************************************************************
 * Description:
 *		seaelf_set_charger_current
 * Parameters:
 *		chg_ma:	set charger current
 * Return:
 *		see I2C_STATUS_XXX define
 *****************************************************************************/
static int seaelf_set_charger_current(int chg_ma)
{
	int ret = 0;
	u8 data = 0;
	u8 chg_curr = 0; 

	/*
	if (chg_ma < 600)
		chg_ma = 600;		//limit to 0.6A
	if (chg_ma > 2000)
		chg_ma = 2000;		//limit to 2A
		*/
	chg_curr = chg_ma / CHG_CURRT_STEP;	//step is 100mA

	ret = seaelf_read_byte(REG_CHARGE_CURRENT, &data);
	if (ret < 0)
		return ret;
	if (data != chg_curr) 
		ret = seaelf_write_byte(REG_CHARGE_CURRENT, chg_curr);	
	return ret;
}


/*****************************************************************************
 * Description:
 *		seaelf_set_wakeup_current 
 * Parameters:
 *		wak_ma:	set wakeup current
 * Return:
 *		see I2C_STATUS_XXX define
 *****************************************************************************/
static int seaelf_set_wakeup_current(int wak_ma)
{
	int ret = 0;
	u8 data = 0;
	u8 wak_curr = 0; 
	if (wak_ma < 50)
		wak_ma = 50;		//limit to 50mA
	if (wak_ma > 200)
		wak_ma = 200;		//limit to 200mA
	wak_curr = wak_ma / WK_CURRT_STEP;	//step is 10mA

	ret = seaelf_read_byte(REG_WAKEUP_CURRENT, &data);
	if (ret < 0)
		return ret;
	if (data != wak_curr) 
		ret = seaelf_write_byte(REG_WAKEUP_CURRENT, wak_curr);
	return ret;
}

/*****************************************************************************
 * Description:
 *		seaelf_set_safety_cc_timer
 * Parameters:
 *		tmin:	set safety cc charge time
 * Return:
 *		see I2C_STATUS_XXX define
 *****************************************************************************/
static int seaelf_set_safety_cc_timer(int tmin)
{
	int ret = 0;
	u8 data = 0;
	u8 tval = 0; 

	if (tmin == 0) {	//disable
		tval = 0;
	}
	else if (tmin == 120) {	//120min
		tval = CC_TIMER_120MIN;
	}
	else if (tmin == 180) {	//180min
		tval = CC_TIMER_180MIN;
	}
	else if (tmin == 240) {	//240min
		tval = CC_TIMER_240MIN;
	}
	else if (tmin == 300) {	//300min
		tval = CC_TIMER_300MIN;
	}
	else {				//invalid values
		tval = CC_TIMER_180MIN;	//reset value
	}

	ret = seaelf_read_byte(REG_SAFETY_TIMER, &data);
	if (ret < 0)
		return ret;

	if ((data & CC_TIMER_MASK) != tval) 
	{
		data &= WAKEUP_TIMER_MASK;	//keep wk timer
		data |= tval;				//update new cc timer
		ret = seaelf_write_byte(REG_SAFETY_TIMER, data);	
	}
	return ret;
}

/*****************************************************************************
 * Description:
 *		seaelf_set_safety_wk_timer 
 * Parameters:
 *		tmin:	set safety wakeup time
 * Return:
 *		see I2C_STATUS_XXX define
 *****************************************************************************/
static int seaelf_set_safety_wk_timer( int tmin)
{
	int ret = 0;
	u8 data = 0;
	u8 tval = 0; 

	if (tmin == 0) {	//disable
		tval = 0;
	}
	else if (tmin == 15) {	//15min
		tval = WK_TIMER_15MIN;
	}
	else if (tmin == 30) {	//30min
		tval = WK_TIMER_30MIN;
	}
	else if (tmin == 45) {	//45min
		tval = WK_TIMER_45MIN;
	}
	else if (tmin == 60) {	//60min
		tval = WK_TIMER_60MIN;
	}
	else {				//invalid values
		tval = WK_TIMER_30MIN;	//reset value
	}

	ret = seaelf_read_byte(REG_SAFETY_TIMER, &data);
	if (ret < 0)
		return ret;

	if ((data & WAKEUP_TIMER_MASK) != tval) 
	{
		data &= CC_TIMER_MASK;		//keep cc timer
		data |= tval;				//update new wk timer
		ret = seaelf_write_byte(REG_SAFETY_TIMER, data);		
	} 
	return ret;
}

/*****************************************************************************
 * Description:
 *		seaelf_get_charging_status 
 * Parameters:
 *		charger:	charger data
 * Return:
 *		see I2C_STATUS_XXX define
 *****************************************************************************/
static int seaelf_get_charging_status(struct seaelf_charger_info *charger)
{
	int ret = 0;
	u8 data_status = 0;
	u8 data_thm = 0;
	u8 i = 0;

	if (!charger)
		return -EINVAL;

	//mutex_lock(&charger->msg_lock);

	/* Check VBUS and VSYS */
	ret = seaelf_read_byte(REG_VBUS_STATUS, &data_status);

	
	if (ret < 0) {
		charger->vbus_ok = 0;
		printk("fail to get Vbus status\n");
		//mutex_unlock(&charger->msg_lock);
		return ret;
	}
	charger->vsys_ovp = (data_status & VSYS_OVP_FLAG) ? 1 : 0;
	charger->vbus_ovp = (data_status & VBUS_OVP_FLAG) ? 1 : 0;
	charger->vbus_uvp = (data_status & VBUS_UVP_FLAG) ? 1 : 0;
	if (((!charger->vbus_ok) && (data_status & VBUS_OK_FLAG))
		|| ((charger->vbus_ok) && (!(data_status & VBUS_OK_FLAG))))
		charger->charger_changed = 1;
	charger->vbus_ok = (data_status & VBUS_OK_FLAG) ? 1 : 0;

	
	if((charger->vbus_ok) && (charger->charger_changed))
		seaelf_init();
		

	/* Check charger status */
	ret = seaelf_read_byte(REG_CHARGER_STATUS, &data_status);
	if (ret < 0) {
		printk("%s: fail to get Charger status\n", __func__);
		//mutex_unlock(&charger->msg_lock);
		return ret; 
	}
	charger->initial_state = (data_status & CHARGER_INIT) ? 1 : 0;
	charger->in_wakeup_state = (data_status & IN_WAKEUP_STATE) ? 1 : 0;
	charger->in_cc_state = (data_status & IN_CC_STATE) ? 1 : 0;
	charger->in_cv_state = (data_status & IN_CV_STATE) ? 1 : 0;
	charger->chg_full = (data_status & CHARGE_FULL_STATE) ? 1 : 0;
	charger->chg_timeout = (data_status & CC_TIMER_FLAG) ? 1 : 0;
	charger->wak_timeout = (data_status & WK_TIMER_FLAG) ? 1 : 0;
 
	/* Check thermal status*/
	ret = seaelf_read_byte(REG_THM_STATUS, &data_thm);
	if (ret < 0) {
		printk("%s: fail to get Thermal status\n", __func__);
		//mutex_unlock(&charger->msg_lock);
		return ret; 
	}
	if (!data_thm)
		charger->thermal_status = THM_DISABLE;
	else {
		for (i = 0; i < 7; i ++) {
			if (data_thm & (1 << i))
				charger->thermal_status = i + 1;
		}
	}
	//mutex_unlock(&charger->msg_lock);

	return ret;
}

/*****************************************************************************
 * Description:
 *		seaelf_get_charging_status 
 * Parameters:
 *		charger:	charger data
 * Return:
 *		see I2C_STATUS_XXX define
 *****************************************************************************/
static void seaelf_get_setting_data(void)
{
	int ret = 0;
	uint8_t data;
	
	if (!charger_info->client)
		return;

	//mutex_lock(&charger_info->msg_lock);

	ret = seaelf_read_byte(REG_CHARGER_VOLTAGE, &data);
	charger_info->chg_volt = data * CHG_VOLT_STEP;	//step is 25mV

	ret = seaelf_read_byte(REG_VBUS_LIMIT_CURRENT, &data);
	charger_info->vbus_current = data * VBUS_ILMT_STEP;	//step is 100mA

	ret = seaelf_read_byte(REG_CHARGE_CURRENT, &data);
	charger_info->charger_current = data * CHG_CURRT_STEP;	//step is 100mA

	ret = seaelf_read_byte(REG_END_CHARGE_CURRENT, &data);
	charger_info->eoc_current = data * EOC_CURRT_STEP;	//step is 10mA

	#if DEBUG_LOG
	{
		printk("----------------------------------------------------\n");
		printk("BBBB chg_volt  is %d\n",charger_info->chg_volt);
		printk("BBBB vbus_current  is %d\n",charger_info->vbus_current);
		printk("BBBB charge_current  is %d\n",charger_info->charger_current);
		printk("BBBB eoc_current  is %d\n",charger_info->eoc_current);
		printk("BBBB vbus_ovp  is %d\n",charger_info->vbus_ovp);
		printk("BBBB vbus_uvp  is %d\n",charger_info->vbus_uvp);
		printk("BBBB vsys_ovp  is %d\n",charger_info->vsys_ovp);
		printk("BBBB chg_full  is %d\n",charger_info->chg_full);
		printk("BBBB in_cc_state  is %d\n",charger_info->in_cc_state);
		printk("BBBB in_cv_state  is %d\n",charger_info->in_cv_state);
		printk("BBBB vbus_ovp  is %d\n",charger_info->vbus_ovp);
		printk("BBBB vbus_uvp  is %d\n",charger_info->vbus_uvp);  
		printk("----------------------------------------------------\n");
	}
	#endif
}

/*****************************************************************************
 * Description:
 *		seaelf_init
 * Parameters:
 *		None
 * Return:
 *		see I2C_STATUS_XXX define
 *****************************************************************************/
static int seaelf_init(void)
{
	struct seaelf_platform_data *pdata = NULL;
	
	u8 data = 0;
	int ret = 0;

  	if (!charger_info->client)
		return -EINVAL;

	/*
	if(!gpio_get_value (data->dc_det_pin))
		return -EINVAL;
	*/
	
	//ret = seaelf_read_byte(REG_CHARGER_VOLTAGE, &data);
	//if (data == 0xA8)
		//printk("Read data 0x:%x\n", data);
	//else
		//return -EINVAL;
	
	pdata = &seaelf_pdata_default;
	printk("O2Micro SeaElf Driver seaelf_init\n");

	//*************************************************************************************
	//note: you must test usb type and set vbus limit current.
	//for wall charger you can set current more than 500mA
	// but for pc charger you may set current not more than 500 mA for protect pc usb port
	//*************************************************************************************
	//mutex_lock(&charger_info->msg_lock);
	printk("Platform data ILMT value: %d\n", pdata->vbus_limit_currentmA);
	/* Min VSYS:3.0V */
    ret = seaelf_set_min_vsys(pdata->min_vsys_voltagemV);
	if(ret < 0)
		return -EINVAL;
	
	//voltage
	ret = seaelf_set_chg_volt(pdata->max_charger_voltagemV);
	if(ret < 0)
		return -EINVAL;

	
	/* ILMT: 1000mA */
	ret  = seaelf_set_vubs_current(pdata->vbus_limit_currentmA);
	if(ret < 0)
		return -EINVAL;
	printk("Platform data ILMT value: %d\n", pdata->vbus_limit_currentmA);
	/* CHARGER CURRENT: 1A */
	ret = seaelf_set_charger_current(pdata->max_charger_currentmA);
	if(ret < 0)
		return -EINVAL;

	printk("max_charger_currentmA : %d\n", pdata->max_charger_currentmA);

	/* CHARGER CURRENT: 1A */
	ret = seaelf_set_eoc_current(pdata->max_charger_currentmA);
	if(ret < 0)
		return -EINVAL;



	seaelf_set_safety_cc_timer(pdata->max_cc_charge_time);


	/* Max CHARGE VOLTAGE:4.2V */
	/* T34 CHARGE VOLTAGE:4.15V */
	/* T45 CHARGE VOLTAGE:4.1V */
	/* WAKEUP VOLTAGE:2.5V */
	/* WAKEUP CURRENT:0.1A */
	/* Max CHARGE VOLTAGE:4.2V */
	/* EOC CHARGE:0.05A */
	/* RECHG HYSTERESIS:0.1V */
	/* MAX CC CHARGE TIME:180min */
	/* MAX WAKEUP CHARGE TIME:30min */
	/* RTHM 10K/100K:0 (100K) */
	//seaelf_get_charging_status(charger_info);
	//mutex_unlock(&charger_info->msg_lock);
	
	return ret;
}




/*****************************************************************************
 * Description:
 *		seaelf_charger_work
 * Parameters:
 *		work
 * Return:
 *		see I2C_STATUS_XXX define
 *****************************************************************************/
static void seaelf_charger_work(struct work_struct *work)
{

	struct seaelf_charger_info *charger = 
			container_of(work, struct seaelf_charger_info, delay_work.work);
	
	struct seaelf_platform_data *pdata = NULL;
	uint8_t data;
	int32_t charge_volt = seaelf_pdata_default.max_charger_voltagemV;
	int32_t ir;
	
	int		i, ret = 0;
	pdata = &charger->pdata;

	if (!charger->client)
		return -EINVAL;
	
	//lock power supply
	//
	//mutex_lock(&charger->msg_lock);

	/*
	if(gpio_get_value(data->usb_det_pin))
	{
		charger_info->vbus_ok = 0; //0;	//discharging
		return;
	}
    */

	seaelf_get_charging_status(charger_info);
	printk(" yyyy get Vbus status is %d\n", charger_info->vbus_ok);
	gas_gauge->vbus_ok = charger_info->vbus_ok;
	gas_gauge->charge_full= charger_info->chg_full;
	
	
	if (charger->charger_changed) {
		power_supply_changed(&charger->usb);	//trigger PM
		charger->charger_changed = 0;
	}
	
	if(charger->vbus_ok)
	{
		seaelf_get_setting_data();

		/*
		//if((charger->in_cc_state) && (charger->chg_volt <= 4200))
		if((batt_info->fCurr > 500) && (charger->chg_volt <= 4200))
		{
			seaelf_set_chg_volt(4280);
			printk("set voltage 4280");
		}
		//else if((charger->in_cv_state) && (charger->chg_volt >= 4200))
		else if (charger->chg_volt > 4200)
		{
			seaelf_set_chg_volt(4200);
			printk("set voltage 4200");
		}
		*/

		

		//charge_volt = 4200 + batt_info->fCurr * 100 /1000;
		/*
		ir = batt_info->fCurr * 100 /1000;
		if(ir % 25)
			charge_volt = 4200 + (ir / 25 + 1) * 25;
		else
			charge_volt = 4200 + ir / 25 * 25;
			
		*/
		
		//if((ir>=25)&&(ir<=50))
			//charge_volt = seaelf_pdata_default.max_charger_voltagemV + ir / 25  * 25;
		//else
		
		//charge_volt = seaelf_pdata_default.max_charger_voltagemV + (ir / 25 - 1) * 25;
		//charge_volt = seaelf_pdata_default.max_charger_voltagemV + (ir / 25 - 1) * 25;

			/*
		if(batt_info->fVolt > 4210)
		{
			charge_volt = 4200;
			printk(" sssssss cell volt is too high and make it 4200\n");

		}
		*/

		#if IR_COMPENSATION
			ir = batt_info->fCurr * seaelf_pdata_default.inpedance /1000;

			
			charge_volt = seaelf_pdata_default.max_charger_voltagemV + ir;

			charge_volt = charge_volt / 25 * 25;

			printk(" calculate IR volt  is %d\n", charge_volt);
			
			if(charge_volt < 4200)
				charge_volt = 4200;

			if(charger->chg_volt != charge_volt)
				seaelf_set_chg_volt(charge_volt);
		#endif

		
	}


	//mutex_unlock(&charger->msg_lock);
	/* reschedule for the next time */
	schedule_delayed_work(&charger->delay_work, charger->polling_invl);

}


/*****************************************************************************
 * Description:
 *		seaelf_charger_get_property
 * Parameters:
 *		
 * Return:
 *		see I2C_STATUS_XXX define
 *****************************************************************************/
static int seaelf_charger_get_property(struct power_supply *psy,
                enum power_supply_property psp,
                union power_supply_propval *val)
{
	struct seaelf_charger_info *charger = 
			container_of(psy, struct seaelf_charger_info, usb);
	struct seaelf_platform_data *pdata = NULL;
	int ret = 0;
	pdata = &charger->pdata;

	//printk("%s: O2Micro SeaElf Driver Get Property, %d\n", __func__, psp);

	switch (psp) {
		case POWER_SUPPLY_PROP_ONLINE:
        		//ret = seaelf_get_charging_status(charger);
        		if (!ret) {
					val->intval = charger->vbus_ok ? 1 : 0;
					//dev_dbg(charger->dev, "%s: ONLINE, %d\n",
	                      // __func__, val->intval); 
                }
                else
					//return -EINVAL;
					return 0;
                break;
        case POWER_SUPPLY_PROP_STATUS:
        		//ret = seaelf_get_charging_status(charger);
        		if (!ret) {
        			if (charger->chg_full) {
        				val->intval = POWER_SUPPLY_STATUS_FULL;
        			}
        			else if ((charger->in_cc_state) || (charger->in_cv_state)
        					|| (charger->in_wakeup_state)) {
        				val->intval = POWER_SUPPLY_STATUS_CHARGING; 
					}
					else
						val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
					dev_dbg(charger->dev, "%s: STATUS, %d\n",
                        __func__, val->intval); 
				}
                else
					return -EINVAL;
        		break;
        case POWER_SUPPLY_PROP_HEALTH:
        		//ret = seaelf_get_charging_status(charger);
        		if (!ret) {
        			if ((charger->vbus_ovp) || (charger->vsys_ovp))	//ov check
        				val->intval = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
        			else if (charger->thermal_status == 0)			//thm disable
        				val->intval = POWER_SUPPLY_HEALTH_UNKNOWN;
        			else if (charger->thermal_status == THM_UNDER_T1)	//cold
        				val->intval = POWER_SUPPLY_HEALTH_COLD;
        			else if ((charger->thermal_status == THM_OVER_T5)	//heat
        					|| (charger->thermal_status == THM_ITOT))
        				val->intval = POWER_SUPPLY_HEALTH_OVERHEAT;
        			else
        				val->intval = POWER_SUPPLY_HEALTH_GOOD;
					dev_dbg(charger->dev, "%s: HEALTH, %d\n",
                        __func__, val->intval); 
        		}
                else
					return -EINVAL;
        		break;
        case POWER_SUPPLY_PROP_CURRENT_MAX:
                val->intval = pdata->max_charger_currentmA;
				dev_dbg(charger->dev, "%s: CURRENT_MAX, %d\n",
					__func__, val->intval); 
                break;
        default:
                return -EINVAL;
	}

	return ret;
}


/*****************************************************************************
 * Description:
 *		seaelf_interrupt
 * Parameters:
 *		
 * Return:
 *		see I2C_STATUS_XXX define
 *****************************************************************************/
static irqreturn_t seaelf_interrupt(int irq, void *dev)
{
	struct seaelf_charger_info * const charger = dev_get_drvdata(dev);
/*
	if(gpio_get_value(data->dc_det_pin))
			charger->vbus_ok = 0; //0;	discharging
		else
			seaelf_init();	//charging
	
*/
	return IRQ_HANDLED;
}

/*****************************************************************************
 * Description:
 *		usbdet_work
 * Parameters:
 *		
 * Return:
 *		see I2C_STATUS_XXX define
 *****************************************************************************/
static void usbdet_work(struct work_struct *work)
{
    int ret;

	struct seaelf_charger_info *charger = 
			container_of(work, struct seaelf_charger_info, usbdet_work);    

	int irq      = gpio_to_irq(charger->usb_det_pin);
    int irq_flag = gpio_get_value (charger->usb_det_pin) ? IRQF_TRIGGER_FALLING : IRQF_TRIGGER_RISING;
    
    //rk28_send_wakeup_key();// should add this for your project
    
    free_irq(irq, NULL);
    ret = request_irq(irq, seaelf_interrupt, irq_flag, "usb_det", NULL);
	if (ret) {
		free_irq(irq, NULL);
	}
	
	power_supply_changed(&charger->usb);
}

/*****************************************************************************
 * Description:
 *		seaelf_charger_probe
 * Parameters:
 *		
 * Return:
 *		see I2C_STATUS_XXX define
 *****************************************************************************/
static int __devinit seaelf_charger_probe(struct i2c_client *client,
									const struct i2c_device_id *id)
{
	struct seaelf_charger_info *charger;
	//struct seaelf_platform_data *pdata = NULL;
	int ret = 0;

	printk("O2Micro SeaElf Driver Loading\n");

	charger = kzalloc(sizeof(*charger), GFP_KERNEL);
	if (!charger) {
		dev_err(&client->dev, "Can't alloc charger struct\n");
		return -ENOMEM;
	}
	//for public use
	charger_info = charger;
	
	charger->pdata = seaelf_pdata_default;
	charger->client = client;	
	charger->dev = &client->dev;

	//charger->usb_det_pin = ;

	/*
	ret = request_irq(OMAP_GPIO_IRQ(pdata->gpio_stat),
						seaelf_interrupt,
						IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING,
						"seaelf_stat",
						charger->dev);
	*/

	#if 0
	if (charger->usb_det_pin != INVALID_GPIO)
	{
		ret = gpio_request(charger->usb_det_pin, "usb_det");
		if (ret != 0) {
			gpio_free(charger->usb_det_pin);
			printk("fail to request charger->usb_det_pin\n");
			return -EIO;
		}

		INIT_WORK(&data->usbdet_work, usbdet_work);
		irq = gpio_to_irq(charger->usb_det_pin);
	        
		irq_flag = gpio_get_value (charger->usb_det_pin) ? IRQF_TRIGGER_FALLING : IRQF_TRIGGER_RISING;
	    	ret = request_irq(irq, seaelf_interrupt, irq_flag, "dc_det", NULL);
	    	if (ret) {
	    		printk("failed to request usb det irq\n");
				return -EIO;
	    	}
	    	enable_irq_wake(irq);	
	}
	#endif
	
	

	charger->usb.name = "seaelf-charger";
	charger->usb.type = POWER_SUPPLY_TYPE_USB;
	charger->usb.properties = seaelf_charger_props;
	charger->usb.num_properties = ARRAY_SIZE(seaelf_charger_props);
	charger->usb.get_property = seaelf_charger_get_property;

	charger->polling_invl = msecs_to_jiffies(2 * 1000);

	i2c_set_clientdata(client, charger);

	mutex_init(&charger->msg_lock);

	seaelf_get_charging_status(charger->client);


	ret = power_supply_register(charger->dev, &charger->usb);
	if (ret < 0) {
		goto err_free;
	}
	INIT_DELAYED_WORK(&charger->delay_work, seaelf_charger_work);

	schedule_delayed_work(&charger->delay_work, charger->polling_invl);
	return 0;

err_free:
	cancel_delayed_work(&charger->delay_work);
	//free_irq(pdata->gpio_stat, NULL);
	kfree(charger);
	return ret;
}


/*****************************************************************************
 * Description:
 *		seaelf_charger_remove
 * Parameters:
 *		
 * Return:
 *		see I2C_STATUS_XXX define
 *****************************************************************************/
static int __devexit seaelf_charger_remove(struct i2c_client *client)
{
	struct seaelf_charger_info *charger = i2c_get_clientdata(client);
	mutex_lock(&charger->msg_lock);
	mutex_unlock(&charger->msg_lock);
	power_supply_unregister(&charger->usb);
	cancel_delayed_work(&charger->delay_work);
	flush_scheduled_work();
	printk("%s: SeaElf Charger Driver removed\n", __func__);
	kfree(charger);
	return 0;
}


/*****************************************************************************
 * Description:
 *		seaelf_charger_suspend
 * Parameters:
 *		
 * Return:
 *		see I2C_STATUS_XXX define
 *****************************************************************************/
static int seaelf_charger_suspend(struct device *dev)
{
	struct platform_device * const pdev = to_platform_device(dev);
	struct seaelf_charger_info * const charger = platform_get_drvdata(pdev);

	printk("%s: SeaElf Charger Driver Suspended\n", __func__);
	mutex_lock(&charger->msg_lock);
	mutex_unlock(&charger->msg_lock);
	return 0;
}


/*****************************************************************************
 * Description:
 *		seaelf_charger_resume
 * Parameters:
 *		
 * Return:
 *		see I2C_STATUS_XXX define
 *****************************************************************************/
static int seaelf_charger_resume(struct device *dev)
{
	struct platform_device * const pdev = to_platform_device(dev);
	struct seaelf_charger_info * const charger = platform_get_drvdata(pdev);

	printk("%s: SeaElf Charger Driver Resumed\n", __func__);
	mutex_lock(&charger->msg_lock);
	mutex_unlock(&charger->msg_lock);
	schedule_delayed_work(&charger->delay_work, charger->polling_invl);
	schedule_work(&charger->usbdet_work);
	return 0;
}



/*****************************************************************************
 * Description:
 *		seaelf_charger_shutdown
 * Parameters:
 *		
 * Return:
 *		see I2C_STATUS_XXX define
 *****************************************************************************/
static void seaelf_charger_shutdown(struct i2c_client *client)
{
	struct seaelf_charger_info *charger = i2c_get_clientdata(client);
	mutex_lock(&charger->msg_lock);
	mutex_unlock(&charger->msg_lock);
	power_supply_unregister(&charger->usb);
	cancel_delayed_work(&charger->delay_work);
	//free_irq(OMAP_GPIO_IRQ(charger->pdata.gpio_stat), charger->dev);
	printk("%s: SeaElf Charger Driver Shutdown\n", __func__);
	kfree(charger);
}

static const struct i2c_device_id seaelf_i2c_ids[] = {
		{"seaelf-charger", 0},
		{}
}; 

MODULE_DEVICE_TABLE(i2c, seaelf_i2c_ids);

static const struct dev_pm_ops pm_ops = {
        .suspend        = seaelf_charger_suspend,
        .resume			= seaelf_charger_resume,
};

static struct i2c_driver seaelf_charger_driver = {
		.driver		= {
			.name 		= "seaelf-charger",
			.pm			= &pm_ops,
		},
		.probe		= seaelf_charger_probe,
		.remove		= __devexit_p(seaelf_charger_remove),
		.shutdown	= seaelf_charger_shutdown,
		.id_table	= seaelf_i2c_ids,
};

static int __init seaelf_charger_init(void)
{
	printk("seaelf driver init\n");
	return i2c_add_driver(&seaelf_charger_driver);
}

static void __exit seaelf_charger_exit(void)
{
	printk("seaelf driver exit\n");
	i2c_del_driver(&seaelf_charger_driver);
}

module_init(seaelf_charger_init);
module_exit(seaelf_charger_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("O2Micro");
MODULE_DESCRIPTION("O2Micro SeaElf Charger Driver");
