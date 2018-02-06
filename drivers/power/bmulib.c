/*****************************************************************************
* Copyright(c) O2Micro, 2013. All rights reserved.
*	
* O2Micro OZ8806 battery gauge driver
* File: bmulib.c

* Author: Eason.yuan
* $Source: /data/code/CVS
* $Revision: 4.00.01 $
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
/****************************************************************************************
****************************************************************************************/
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/syscalls.h>
#include <linux/string.h>

#include "parameter.h"
#include "oz8806_regdef.h"
#include <linux/kmod.h>

#include <linux/wakelock.h>


/*****************************************************************************
* Define section
* add all #define here
*****************************************************************************/

//#define FCC_UPDATA_CHARGE
#define VERSION						"2014.05.22.1/4.00.02 "	
#define charge_step					parameter->charge_pursue_step
#define discharge_step				parameter->discharge_pursue_step
#define discharge_th				parameter->discharge_pursue_th
#define config_data					parameter->config

#define	RETRY_CNT	8
/*
#define BATT_TEST 		"/mnt/sdcard/init_ok.dat"
#define BATT_CAPACITY 	"/data/sCaMAH.dat"
#define BATT_FCC 		"/data/fcc.dat"
#define BATT_OFFSET 	"/data/offset.dat"
*/

char * BATT_TEST 		= "/mnt/sdcard/init_ok.dat";
char * BATT_CAPACITY 	= "/data/sCaMAH.dat";
char * BATT_FCC 		= "/data/fcc.dat";
char * OCV_FLAG 		= "/data/ocv_flag.dat";

char * BATT_OFFSET 		= "/data/offset.dat";

char * CM_PATH 	   		= "/system/xbin/oz8806api";


//#define BMU_INIT_OK 	"/mnt/sdcard/bmu_init_ok.dat"
//#define BMU_DEBUG_ON 	"/mnt/sdcard/bmu_debug_on.dat"


#define SHUTDOWN_HI          50
#define SHUTDOWN_TH1         100
#define SHUTDOWN_TH2         300

#define CHARGE_END_CURRENT2  350

#define FCC_UPPER_LIMIT		 110
//#define FCC_LOWER_LIMIT		 80
#define FCC_LOWER_LIMIT		 40   //for lianxiang

/*****************************************************************************
* Global variables section - Exported
* add declaration of global variables that will be exported here
* e.g.
*	int8_t foo;
****************************************************************************/
bmu_data_t 	*batt_info;
gas_gauge_t	*gas_gauge;
uint8_t 	bmu_init_ok = 0;
uint8_t     oz8806_pec_check  = 0;
uint8_t     oz8806_cell_num  = 1;
int32_t     res_divider_ratio = 1000;
uint8_t     charger_finish = 0;
uint8_t     charge_end_flag = 0;

uint8_t     wait_dc_charger= 0;
uint8_t     wait_voltage_end = 0;


/*****************************************************************************
* Local variables section - Local
* define global variables(will be refered only in this file) here,
* static keyword should be used to limit scope of local variable to this file
* e.g.
*	static uint8_t ufoo;
*****************************************************************************/
static uint8_t discharge_end = 0;
static uint8_t charge_end = 0;
static uint8_t charge_fcc_update = 0;
static uint8_t discharge_fcc_update = 0;

static parameter_data_t *parameter;
static uint8_t power_on_flag = 0;
static uint8_t write_offset = 0;
static uint8_t check_offset_flag = 0;

static uint8_t bmu_sleep_flag = 0;
static int32_t fRSOC_PRE;

static uint8_t discharge_end_flag = 0;
static uint8_t sleep_ocv_flag = 0;
static uint8_t sleep_charge_flag = 0;

/******************************************************************************
* Function prototype section
* add prototypes for all functions called by this file,execepting those
* declared in header file
*****************************************************************************/
static int32_t 	one_latitude_table(int32_t number,one_latitude_data_t *data,int32_t value);
static void 	oz8806_over_flow_prevent(void);
static void 	check_pec_control(void);

static int32_t 	afe_register_read_byte(uint8_t index, uint8_t *dat);
static int32_t 	afe_register_write_byte(uint8_t index, uint8_t dat);
static int32_t 	afe_register_read_word(uint8_t index, uint16_t *dat);
static int32_t 	afe_register_write_word(uint8_t index, uint16_t dat);

static int32_t 	afe_read_cell_volt(int32_t *voltage);
static int32_t 	afe_read_ocv_volt(int32_t *voltage);
static int32_t 	afe_read_current(int32_t *dat);
static int32_t 	afe_read_car(int32_t *dat);
static int32_t 	afe_write_car(int32_t dat);
static int32_t 	afe_read_cell_temp(int32_t *date);
static int32_t 	afe_read_board_offset(int32_t *dat);
static int32_t 	afe_write_board_offset(int32_t date);

static int32_t 	oz8806_read_byte( uint8_t index);
static int32_t 	oz8806_write_byte( uint8_t index, uint8_t data);
static int32_t 	oz8806_read_word(uint8_t index);
static int32_t 	oz8806_write_word( uint8_t index, uint16_t data);
static int32_t 	oz8806_cell_voltage_read(int32_t *voltage);
static int32_t 	oz8806_ocv_voltage_read(int32_t *voltage);
static int32_t 	oz8806_temp_read(int32_t *voltage);
static int32_t 	oz8806_current_read(int32_t *data);
static int32_t 	oz8806_car_read(int32_t *car);
static int32_t 	oz8806_car_write(int32_t data);
static uint8_t 	pec_calculate (uint8_t ucCrc, uint8_t ucData);
static int32_t 	oz8806_write_byte_pec(uint8_t index, uint8_t data);
static int 		bmu_check_file(char * address);
static int 		bmu_write_data(char * address,int data);
static int 		bmu_read_data(char * address);
static int 		bmu_write_string(char * address,char * data);
static void	 	bmu_wait_ready(void);
static int32_t 	i2c_read_byte(uint8_t addr,uint8_t index,uint8_t *data);
static int32_t 	i2c_write_byte(uint8_t addr,uint8_t index,uint8_t data);
static void 	check_oz8806_staus(void);
static void 	discharge_end_process(void);
static void 	charge_end_process(void);
static void 	check_board_offset(void);
static void 	check_shutdwon_voltage(void);
static int 		oz8806_create_sys(void);
static void 	trim_bmu_VD23(void);





/*****************************************************************************
 * Description:
 *		bmu_call
 * Parameters:
 *		None
 * Return:
 *		None
 *****************************************************************************/
void bmu_call(void)
{
	int result  = num_0;
	char* cmdArgv[]={CM_PATH,"/oz8806.txt",NULL};
	char* cmdEnvp[]={"HOME=/","PATH=/system/xbin:/system/bin:/sbin:/bin:/usr/bin:/mnt/sdcard",NULL};
	result=call_usermodehelper(CM_PATH,cmdArgv,cmdEnvp,UMH_WAIT_PROC);

	if(0 == result)
	{
		if(parameter->config->debug)
		{
			printk("test call_usermodehelper is %d\n",result);
		}
	}
	else
		printk("test call_usermodehelper is %d\n",result);
	
}

/*****************************************************************************
 * Description:
 *		bmu init chip
 * Parameters:
 *		used to init bmu
 * Return:
 *		None
 *****************************************************************************/
void bmu_init_chip(parameter_data_t *paramter_temp)
{

	int32_t ret;
    uint16_t value;	
	uint8_t i;
	int32_t data;
	uint8_t * addr;
	config_data_t * config_test;
	uint32_t byte_num = num_0;

	byte_num = sizeof(config_data_t) + sizeof(bmu_data_t) +  sizeof(gas_gauge_t);
	//byte_num += 4* (gas_gauge->rc_x_num + gas_gauge->rc_y_num +gas_gauge->rc_z_num);
	//byte_num += 4*gas_gauge->rc_x_num *gas_gauge->rc_y_num*gas_gauge->rc_z_num;
	//byte_num += (gas_gauge->charge_table_num ) * 4 *2;
	
	memset((uint8_t *)kernel_memaddr,num_0,byte_num);
	

	parameter = paramter_temp;
	addr = (uint8_t *)(parameter->config);
	for(byte_num = num_0;byte_num < sizeof(config_data_t);byte_num++)
	{
		 *((uint8_t *)kernel_memaddr + byte_num) = *addr++;
	}

	config_test = (config_data_t *)kernel_memaddr;

	batt_info = (bmu_data_t *)((uint8_t *)kernel_memaddr + byte_num);
	byte_num +=  sizeof(bmu_data_t);

	gas_gauge = (gas_gauge_t *)((uint8_t *)kernel_memaddr + byte_num);
	byte_num += sizeof(gas_gauge_t);
	//--------------------------------------------------------------------------------------------------
	gas_gauge->charge_end = num_0;
	gas_gauge->charge_end_current_th2 = CHARGE_END_CURRENT2;
	gas_gauge->charge_strategy = 1;
	//gas_gauge->charge_max_ratio = 1100;
	gas_gauge->charge_max_ratio = 3000;  // for lianxiang
	
	
	gas_gauge->discharge_end = num_0;
	gas_gauge->discharge_current_th = DISCH_CURRENT_TH;
	gas_gauge->discharge_strategy = 1;

	gas_gauge->dsg_end_voltage_hi = SHUTDOWN_HI;

	//this for test gas gaugue
	if(parameter->config->debug)
		gas_gauge->dsg_end_voltage_hi = 0;
	gas_gauge->dsg_end_voltage_th1 = SHUTDOWN_TH1;
	gas_gauge->dsg_end_voltage_th2 = SHUTDOWN_TH2;
	gas_gauge->dsg_count_2 = num_0;
	bmu_init_gg();
	//--------------------------------------------------------------------------------------------------
	memcpy(((uint8_t *)kernel_memaddr + byte_num),(uint8_t *)charge_data,4*gas_gauge->charge_table_num* 2);
	byte_num += 4 * gas_gauge->charge_table_num * 2;
	
	memcpy(((uint8_t *)kernel_memaddr + byte_num),(uint8_t *)XAxisElement,4*gas_gauge->rc_x_num );
	byte_num += 4*gas_gauge->rc_x_num ;

	memcpy(((uint8_t *)kernel_memaddr + byte_num),(uint8_t *)YAxisElement,4*gas_gauge->rc_y_num);
	byte_num += 4*gas_gauge->rc_y_num;

	memcpy(((uint8_t *)kernel_memaddr + byte_num),(uint8_t *)ZAxisElement,4*gas_gauge->rc_z_num);
	byte_num += 4*gas_gauge->rc_z_num;

	memcpy(((uint8_t *)kernel_memaddr + byte_num),(uint8_t *)RCtable,4*gas_gauge->rc_x_num *gas_gauge->rc_y_num*gas_gauge->rc_z_num);
	byte_num += 4*gas_gauge->rc_x_num *gas_gauge->rc_y_num*gas_gauge->rc_z_num;


	printk("byte_num is %d\n",byte_num);
	
	power_on_flag = num_0;
	bmu_sleep_flag = num_0;
	batt_info->i2c_error_times = num_0;
	
	gas_gauge->overflow_data = num_32768 * num_5 / config_data->fRsense;

	//bmu_init_gg();

	printk("AAAA O2MICRO OZ8806 DRIVER VERSION is %s\n",VERSION);

	if(parameter->config->debug){
		printk("yyyy gas_gauge->overflow_data is %d\n",gas_gauge->overflow_data);	
	}

	printk("O2MICRO OZ8806 test parameter  %d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,\n",
		config_test->fRsense,
		config_test->temp_pull_up,
		config_test->temp_ref_voltage,
		config_test->dbCARLSB,
		config_test->dbCurrLSB,
		config_test->fVoltLSB,
		config_test->design_capacity,
		config_test->charge_cv_voltage,
		config_test->charge_end_current,
		config_test->discharge_end_voltage,
		config_test->board_offset,
		config_test->debug

	);


	check_oz8806_staus();
	//check_pec_control();

	oz8806_create_sys();
	
	afe_register_read_word(OZ8806_OP_BOARD_OFFSET,(uint16_t *)&data);
	if(config_data->debug)printk("AAAAAAAAAAAAA regeidter is  %x\n",data);
	
	ret  = afe_read_board_offset(&data);
	printk("AAAA board_offset is  %d\n",data);


	if(ret >= num_0)
	{
		if((data > num_10) || (data <= num_0))
			afe_write_board_offset(num_7);
	}

	//wake up OZ8806 into FullPower mode
	ret = afe_register_read_byte(OZ8806_OP_CTRL,&i);
	printk("first read 0x09 ret is %d,i is %d \n",ret,i);

	if(oz8806_cell_num > num_1)
		afe_register_write_byte(OZ8806_OP_CTRL,num_0x2c);
	else
		afe_register_write_byte(OZ8806_OP_CTRL,num_0x20);

	if((i & num_0x40) || (ret < num_0))
	{
		if(oz8806_cell_num > num_1)
		{
			afe_register_write_byte(OZ8806_OP_CTRL,num_0x2c);
		}
		else
			afe_register_write_byte(OZ8806_OP_CTRL,0x20);
		msleep(1000);
		printk("Wake up ret is %d,i is %d \n",ret,i);
		if(parameter->config->debug)
			printk("Wake up oz8806 write op_ctrl 0x20 \n");

		ret = afe_register_read_byte(OZ8806_OP_CTRL,&i);
		printk("second read 0x09 ret is %d,i is %d \n",ret,i);

		
	}
	

	printk("O2MICRO OZ8806 parameter  %d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,\n",
		config_data->fRsense,
		config_data->temp_pull_up,
		config_data->temp_ref_voltage,
		config_data->dbCARLSB,
		config_data->dbCurrLSB,
		config_data->fVoltLSB,
		config_data->design_capacity,
		config_data->charge_cv_voltage,
		config_data->charge_end_current,
		config_data->discharge_end_voltage,
		config_data->board_offset,
		config_data->debug

	);

	trim_bmu_VD23();

	//read data
	afe_read_cell_volt(&batt_info->fVolt);
	afe_read_current(&batt_info->fCurr);
	afe_read_cell_temp(&batt_info->fCellTemp);
	afe_read_car(&batt_info->fRC);
	for(i = 0;i < 3;i++)
	{
		ret = afe_register_read_word(OZ8806_OP_OCV_LOW,&value);
		if(ret >= 0)break;
	}
	

	printk("read batt_info.fVolt is %d\n",(batt_info->fVolt * oz8806_cell_num));
	printk("read batt_info.fRC is %d\n",batt_info->fRC);
	printk("read batt_info.fCurr is %d\n",batt_info->fCurr);
	printk("read ocv flag ret is %d,value is %d \n",ret,value);
	if (ret >= num_0 )	
	{
		// OZ8806 First power on 
		if (value & POWER_OCV_FLAG)
		{
			power_on_flag = num_1;
			msleep(2000);
			afe_read_ocv_volt(&batt_info->fOCVVolt);
			afe_read_cell_volt(&batt_info->fVolt);
			
			if(oz8806_cell_num > num_1)
				batt_info->fOCVVolt = batt_info->fVolt;
			
			printk("AAAA ocv volt is %d\n",(batt_info->fOCVVolt * oz8806_cell_num));
			printk("AAAA volt is %d\n",(batt_info->fVolt * oz8806_cell_num));
			
			
			if((batt_info->fOCVVolt  > config_data->charge_cv_voltage) || (batt_info->fOCVVolt  < parameter->ocv[num_0].x)){
				msleep(num_1000);
				afe_read_cell_volt(&batt_info->fVolt);
				batt_info->fOCVVolt = batt_info->fVolt;
				printk("AAAAA ocv data errror ,so batt_info->fVolt is %d\n",batt_info->fVolt);
			}

			afe_read_current(&batt_info->fCurr);
			printk("AAAA batt_info.fCurr is %d\n",batt_info->fCurr);

			if(batt_info->fCurr > num_50)
				data = batt_info->fOCVVolt - num_100;
			else
				data = batt_info->fOCVVolt;
			
			batt_info->fRSOC = one_latitude_table(parameter->ocv_data_num,parameter->ocv,data);
			printk("find table batt_info.fRSOC is %d\n",batt_info->fRSOC); 
			
			if((batt_info->fRSOC >num_100) || (batt_info->fRSOC < num_0))
				batt_info->fRSOC = num_50;

			batt_info->fRC = batt_info->fRSOC * config_data->design_capacity / num_100;
			
			afe_read_current(&batt_info->fCurr);
			
			
			//be carefull lirui don't want this code
			if((batt_info->fVolt < (config_data->discharge_end_voltage + 350))&&(batt_info->fCurr > num_50)){
				printk("Power on mode vs charge on ");
				printk("AAAA batt_info->fCurr %d\n",batt_info->fCurr);
				batt_info->fRSOC = num_1;
				batt_info->fRC = config_data->design_capacity / num_100 + num_10;

			}
			
			
			if(batt_info->fRC  >= (gas_gauge->overflow_data -num_10))
			{
				batt_info->fRC = gas_gauge->overflow_data  - gas_gauge->overflow_data /num_100;
				batt_info->fRCPrev = batt_info->fRC;	
			}
			
			afe_write_car(batt_info->fRC);
			batt_info->fRCPrev = batt_info->fRC;
			gas_gauge->fcc_data= config_data->design_capacity;

			printk("Power on mode is activated \n");
			batt_info->sCaMAH = batt_info->fRSOC * config_data->design_capacity / num_100;
			gas_gauge->sCtMAH = batt_info->sCaMAH; 
			gas_gauge->discharge_sCtMAH = config_data->design_capacity - batt_info->sCaMAH; 
			
			if(batt_info->fRSOC <= num_1){
				gas_gauge->charge_fcc_update = num_1;
				gas_gauge->sCtMAH = num_0;
			}
			if(batt_info->fRSOC <= num_0)
			{
				batt_info->fRSOC = num_0;
				gas_gauge->discharge_end = num_1;
			}		
			if(batt_info->fRSOC >= num_100)
			{
				gas_gauge->charge_end = num_1;
				batt_info->fRSOC = num_100;
				gas_gauge->discharge_sCtMAH = num_0;
				gas_gauge->discharge_fcc_update = num_1;
			}		

			if(parameter->config->debug){
				printk("----------------------------------------------------\n");
				printk("AAAA batt_info->fVolt is %d\n",(batt_info->fVolt * oz8806_cell_num));
				printk("AAAA batt_info->fRSOC is %d\n",batt_info->fRSOC);
				printk("AAAA batt_info->sCaMAH is %d\n",batt_info->sCaMAH);
				printk("AAAA batt_info->fRC is %d\n",batt_info->fRC);
				printk("AAAA batt_info->fCurr is %d\n",batt_info->fCurr);
				printk("----------------------------------------------------\n");
			}
		}
		else if(value & SLEEP_OCV_FLAG)
		{
			afe_read_ocv_volt(&batt_info->fOCVVolt);
			sleep_ocv_flag = 1;
			printk("Sleep ocv mode is activated \n");
		}
		else
			printk("Normal mode is activated \n");
	}
	else
	{
		printk("AAAA O2MICRO OZ8806 DRIVER Big Error\n");
		printk("AAAA O2MICRO OZ8806 can't read OZ8806_OP_OCV_LOW\n");

	}

	//for ifive
	afe_read_car(&batt_info->fRC);
	if((batt_info->fRC < (config_data->design_capacity / num_100 )) && (batt_info->fRC > -3000))
	{
		if(parameter->config->debug)
			printk("yyyy fRC will over fRC  is %d\n",batt_info->fRC);
		
		batt_info->fRC = config_data->design_capacity / num_100 - num_1;
		afe_write_car(batt_info->fRC);
		batt_info->fRCPrev = batt_info->fRC;
	}
	afe_read_car(&batt_info->fRC);
	printk("fRC  is %d\n",batt_info->fRC);

	
	
	afe_register_read_byte(num_0,&i);
	printk("O2micro regeidter 0x00 is %x\n",i);

	afe_register_read_byte(num_9,&i);
	printk("O2micro regeidter 0x09 is %x\n",i);	

	
}
/*****************************************************************************
 * Description:
 *		trim_bmu_VD23
 * Parameters:
 *		None
 * Return:
 *		None
 *****************************************************************************/
static void trim_bmu_VD23(void)
{
	int32_t ret;
	uint8_t i;
	uint16_t value;
	uint8_t data;
	uint8_t temp;
	

	for(i = 0;i < 3;i++)
	{
		ret = afe_register_read_word(OZ8806_OP_VD23,&value);
		if(ret >= 0)break;
	}
	printk("READ oz8806 REG 0x03 is 0x%x\n",value);
	data = ((value >> 6) & 0x02) | ((value >> 8) & 0x01);
	data &= 0x03; 
	printk("So trim is 0x%x\n",data);

	for(i = 0;i < 3;i++)
	{
		ret = afe_register_read_byte(OZ8806_OP_PEC_CTRL,&temp);
		if(ret >= 0)break;
	}
	printk("READ oz8806 REG 0x08 is 0x%x\n",temp);

	
	temp &= 0xfc;
	
	if(data != 0x02)
	{
		temp |= data;
		for(i = 0;i < 3;i++)
		{
			ret = afe_register_write_byte(OZ8806_OP_PEC_CTRL,temp);
			printk("write1 oz8806 REG 0x08 is 0x%x\n",temp);
			if(ret >= 0)break;
		}

	}
	else
	{
		temp |= 0x03;
		for(i = 0;i < 3;i++)
		{
			ret = afe_register_write_byte(OZ8806_OP_PEC_CTRL,temp);
			printk("write2 oz8806 REG 0x08 is 0x%x\n",temp);
			if(ret >= 0)break;
		}

	}

	ret = afe_register_read_byte(OZ8806_OP_PEC_CTRL,&temp);
	printk(" oz8806 REG 0x08 is 0x%x\n",temp);

}




/*****************************************************************************
* Description:
*		check shudown voltage control
* Parameters:
*		None
* Return:
*		None
*****************************************************************************/
static void check_shutdwon_voltage(void)
{
	//int32_t ret;
	//uint8_t i;
	int32_t temp;
	afe_read_cell_volt(&batt_info->fVolt);
	
	//very dangerous
	if(batt_info->fVolt <= (config_data->discharge_end_voltage - num_100))
	{
		discharge_end_process();
		return;
	}

	if(!sleep_ocv_flag) return;
	
	temp = one_latitude_table(parameter->ocv_data_num,parameter->ocv,batt_info->fOCVVolt);
	printk("Sleep ocv soc is %d \n",temp);

	//Very dangerous
	//if((batt_info->fRSOC - temp) > 5)
	if(temp < batt_info->fRSOC)
	{
		
		//batt_info->fRSOC -= 5;
		batt_info->fRSOC = temp;
		if(batt_info->fRSOC < 0)	batt_info->fRSOC = 0;
		
		batt_info->sCaMAH = batt_info->fRSOC * gas_gauge->fcc_data/ 100;


		if(batt_info->fRC >  (gas_gauge->overflow_data - 20))
			 batt_info->fRC = gas_gauge->overflow_data - 20;
		afe_write_car(batt_info->fRC);
		
		batt_info->fRCPrev 	= batt_info->fRC;

		gas_gauge->sCtMAH = batt_info->sCaMAH;
		if(gas_gauge->fcc_data > batt_info->sCaMAH)
			gas_gauge->discharge_sCtMAH = gas_gauge->fcc_data- batt_info->sCaMAH;
		else
			gas_gauge->discharge_sCtMAH = 0;
	}

	
}


/*****************************************************************************
* Description:
*		check pec control
* Parameters:
*		None
* Return:
*		None
*****************************************************************************/
static void check_pec_control(void)
{
}

/*****************************************************************************
* Description:
*		check_oz8806_staus
* Parameters:
*		None
* Return:
*		None
*****************************************************************************/
static void check_oz8806_staus(void)
{
	uint8_t i;
	uint8_t data;
	int32_t ret;
	
	for(i = num_0x2f;i >= num_0x28;i--)
	{
		ret = i2c_read_byte(i,OZ8806_OP_I2CCONFIG,&data);
		data >>= num_1;
		data = data  & num_0x07;
		if((ret == num_0) && (data == num_0x07))
		{
			//printk("data address is %d\n",data);	
			return;
		}
		else if(ret == num_0)
		{
			printk("yyfind error address is %d\n",data);
			data += num_0x28;
			i2c_write_byte(data,OZ8806_OP_I2CCONFIG,0x0e);
			return;
		}
		else
			printk("can't find address is %x\n",i);
	}
	printk("big error  can't find address \n"); 
}



/*****************************************************************************
* Description:
*		bmu_polling_loop
* Parameters:
*		description for each argument, new argument starts at new line
* Return:
*		what does this function returned?
*****************************************************************************/
static void bmu_wait_ready(void)
{
	int32_t data;
	int32_t fcc;
	static uint8_t times = num_0;
	uint8_t i;
	int32_t ret;
    uint16_t value;	

	printk("AAAA bmu wait times %d \n",times);
	//wake up OZ8806 into FullPower mode
	ret = afe_register_read_byte(OZ8806_OP_CTRL,&i);
	printk("bmu_wait_ready read 0x09 ret is %d,i is %d \n",ret,i);

	if((i & 0x40) || (ret < 0))
	{
		if(oz8806_cell_num > 1)
		{
			afe_register_write_byte(OZ8806_OP_CTRL,0x2c);
		}
		else
			afe_register_write_byte(OZ8806_OP_CTRL,0x20);
		printk("oz8806 wake up function\n");

	}
	ret = afe_register_read_byte(OZ8806_OP_CTRL,&i);
	printk("bmu_wait_ready read 0x09 ret is %d,i is %d \n",ret,i);

	//do we need add check ocv flag now?
	ret = afe_register_read_word(OZ8806_OP_OCV_LOW,&value);
	if ((ret >= 0) && (value & POWER_OCV_FLAG))	
		printk("big error,read ocv flag too late\n");	
	
	
	afe_read_cell_temp(&batt_info->fCellTemp);
	afe_read_cell_volt(&batt_info->fVolt);
	afe_read_current(&batt_info->fCurr);
	afe_read_car(&batt_info->fRC);
	//batt_info->fRSOC = num_1;
	
	if(power_on_flag){
		bmu_write_data(BATT_CAPACITY,batt_info->sCaMAH);
		data =  bmu_read_data(BATT_CAPACITY);
		if(data != batt_info->sCaMAH) return;
		bmu_write_data(BATT_FCC,gas_gauge->fcc_data);
		data = bmu_read_data(BATT_FCC);
		if(data != gas_gauge->fcc_data) return;
		bmu_write_data(OCV_FLAG,1);
		data = bmu_read_data(OCV_FLAG);
		if(data != 1) return;
		bmu_init_ok = num_1;
		gas_gauge->ocv_flag = 1;
		batt_info->fRCPrev = batt_info->fRC;
		printk("bmu_wait_ready, power on ok \n");

		#ifdef FCC_UPDATA_CHARGE
			bmu_write_data(FCC_UPDATE_FLAG,num_0);
		#endif
	}
	data = bmu_read_data(BATT_CAPACITY);
	fcc  = bmu_read_data(BATT_FCC);
	gas_gauge->ocv_flag = bmu_read_data(OCV_FLAG);
	
	if((data < 0)||(fcc < 0)|| (gas_gauge->ocv_flag<0)){
		if(times < 6)times++;
		
		if(times > 5){
			times = num_0;
			printk("Can't read battery capacity,use default\n");

			gas_gauge->fcc_data = config_data->design_capacity;

			if((batt_info->fRC >= (gas_gauge->fcc_data / num_100)) && (batt_info->fRC < (gas_gauge->overflow_data - 5)))
			{
				batt_info->sCaMAH = batt_info->fRC;	
				if(batt_info->sCaMAH > (gas_gauge->fcc_data + config_data->design_capacity / 100))
				{
					printk("big error sCaMAH is %d\n",batt_info->sCaMAH);
					batt_info->sCaMAH = gas_gauge->fcc_data + config_data->design_capacity / 100;
					bmu_write_data(BATT_CAPACITY,batt_info->sCaMAH);
				}
				batt_info->fRSOC = batt_info->sCaMAH   * num_100;
				batt_info->fRSOC = batt_info->fRSOC / gas_gauge->fcc_data ;
			}
			else
			{
				afe_read_cell_volt(&batt_info->fVolt);
				batt_info->fRSOC = one_latitude_table(parameter->ocv_data_num,parameter->ocv,batt_info->fVolt);
				if((batt_info->fRSOC >num_100) || (batt_info->fRSOC < num_0))
					batt_info->fRSOC = num_50;

				batt_info->fRC = batt_info->fRSOC * gas_gauge->fcc_data / num_100;
				if(batt_info->fRC  >= (gas_gauge->overflow_data -num_10))
				{
					batt_info->fRC = gas_gauge->overflow_data  - gas_gauge->overflow_data/num_100;
					batt_info->fRCPrev = batt_info->fRC;	
				}
				
				afe_write_car(batt_info->fRC);
				batt_info->fRCPrev = batt_info->fRC;
				
				batt_info->sCaMAH = batt_info->fRC;	
			}
			
			gas_gauge->sCtMAH = batt_info->sCaMAH;
			gas_gauge->discharge_sCtMAH = config_data->design_capacity - batt_info->sCaMAH;
			
			bmu_write_data(BATT_CAPACITY,batt_info->sCaMAH);
			data =  bmu_read_data(BATT_CAPACITY);
			if(data != batt_info->sCaMAH) return;
			
			bmu_write_data(BATT_FCC,config_data->design_capacity);
			data = bmu_read_data(BATT_FCC);
			if(data != config_data->design_capacity) return;

			bmu_write_data(OCV_FLAG,1);
			data = bmu_read_data(OCV_FLAG);
			if(data != 1) return;
			
			batt_info->fRSOC = batt_info->sCaMAH   * num_100;
			batt_info->fRSOC = batt_info->fRSOC / gas_gauge->fcc_data ;
			bmu_init_ok = num_1;
			

			printk("AAAA batt_info->fVolt is %d\n",(batt_info->fVolt * oz8806_cell_num));
			printk("AAAA batt_info->fRSOC is %d\n",batt_info->fRSOC);
			printk("AAAA batt_info->sCaMAH is %d\n",batt_info->sCaMAH);
			printk("AAAA sCtMAH is %d\n",gas_gauge->sCtMAH);
			printk("AAAA fcc is %d\n",gas_gauge->fcc_data);
			printk("AAAA batt_info->fRC is %d\n",batt_info->fRC);
			printk("AAAA batt_info->fCurr is %d\n",batt_info->fCurr);
			if(batt_info->fRSOC <= num_1){
				//gas_gauge->charge_fcc_update = num_1;
				gas_gauge->sCtMAH = num_0;
			}
			if(batt_info->fRSOC <= num_0)
			{
				gas_gauge->discharge_end = num_1;
				batt_info->fRSOC = num_0;
			}	
			if(batt_info->fRSOC >= num_100)
			{
				gas_gauge->charge_end = num_1;
				batt_info->fRSOC = num_100;
				gas_gauge->discharge_sCtMAH = num_0;
			}
			fRSOC_PRE = batt_info->fRSOC;
			return;
		}
		else	
			return;
	}

	data = bmu_read_data(BATT_CAPACITY);
	printk("AAAA read battery capacity data is %d\n",data);
	batt_info->sCaMAH = data;
	if((batt_info->sCaMAH <= num_0) || (batt_info->sCaMAH > (config_data->design_capacity *3/2)))
	{
		if((batt_info->fRC > num_0) && (batt_info->fRC < (gas_gauge->overflow_data - 5)))
		{
			batt_info->sCaMAH = batt_info->fRC;	
		}
		else
		{
			afe_read_cell_volt(&batt_info->fVolt);
			batt_info->fRSOC = one_latitude_table(parameter->ocv_data_num,parameter->ocv,batt_info->fVolt);
			if((batt_info->fRSOC >num_100) || (batt_info->fRSOC < num_0))
				batt_info->fRSOC = num_50;

			batt_info->fRC = batt_info->fRSOC * gas_gauge->fcc_data / num_100;
			if(batt_info->fRC  >= (gas_gauge->overflow_data -num_10))
			{
				batt_info->fRC = gas_gauge->overflow_data  - gas_gauge->overflow_data/num_100;
				batt_info->fRCPrev = batt_info->fRC;	
			}
			
			afe_write_car(batt_info->fRC);
			batt_info->fRCPrev = batt_info->fRC;
			
			batt_info->sCaMAH = batt_info->fRC;

		}
			
	}
	fcc  = bmu_read_data(BATT_FCC);

	if(fcc > (config_data->design_capacity * FCC_UPPER_LIMIT / 100))
	{	
		fcc=  config_data->design_capacity  * FCC_UPPER_LIMIT / 100 ;
		bmu_write_data(BATT_FCC,fcc);
	
		printk("fcc1 update is %d\n",fcc);

	}
	if((fcc <= 0) || (fcc  < (config_data->design_capacity * FCC_LOWER_LIMIT / 100)))
	{	
		fcc = config_data->design_capacity * FCC_LOWER_LIMIT/ 100;
		bmu_write_data(BATT_FCC,fcc);
	
		printk("fcc2 update is %d\n",fcc);

	}
	gas_gauge->fcc_data= fcc;
	

	if(batt_info->sCaMAH > (gas_gauge->fcc_data + config_data->design_capacity / 100))
	{
		printk("big error sCaMAH is %d\n",batt_info->sCaMAH);
		batt_info->sCaMAH = gas_gauge->fcc_data + config_data->design_capacity / 100;
		bmu_write_data(BATT_CAPACITY,batt_info->sCaMAH);
	}
	
	batt_info->fRSOC = batt_info->sCaMAH   * num_100;
	batt_info->fRSOC = batt_info->fRSOC / gas_gauge->fcc_data ;

	batt_info->fRC = batt_info->fRSOC * gas_gauge->fcc_data / num_100;
	if(batt_info->fRC  >= (gas_gauge->overflow_data -num_10))
	{
		batt_info->fRC = gas_gauge->overflow_data  - gas_gauge->overflow_data/num_100;
		batt_info->fRCPrev = batt_info->fRC;	
	}

	printk("AAAA batt_info->fVolt is %d\n",(batt_info->fVolt * oz8806_cell_num));
	printk("AAAA batt_info->fRSOC is %d\n",batt_info->fRSOC);
	printk("AAAA batt_info->sCaMAH is %d\n",batt_info->sCaMAH);
	printk("AAAA gas_gauge->sCtMAH is %d\n",gas_gauge->sCtMAH);
	printk("AAAA fcc is %d\n",gas_gauge->fcc_data);
	printk("AAAA batt_info->fRC is %d\n",batt_info->fRC);
	printk("AAAA batt_info->fCurr is %d\n",batt_info->fCurr);
		
	afe_write_car(batt_info->fRC);	
	batt_info->fRCPrev = batt_info->fRC;	
	bmu_init_ok = num_1;

	gas_gauge->sCtMAH = batt_info->sCaMAH;
	
	if(fcc > batt_info->sCaMAH)
		gas_gauge->discharge_sCtMAH = fcc - batt_info->sCaMAH;
	else
		gas_gauge->discharge_sCtMAH = num_0;
	
	if(batt_info->fRSOC <= num_2)
	{
		gas_gauge->charge_fcc_update = num_1;
	
	}
	if(batt_info->fRSOC <= num_0)
	{
		gas_gauge->discharge_end = num_1;
		batt_info->fRSOC = num_0;
		gas_gauge->sCtMAH = num_0;
		gas_gauge->ocv_flag = 0;
		bmu_write_data(OCV_FLAG,0);
	}
	if(batt_info->fRSOC >= num_100)
	{
		gas_gauge->charge_end = num_1;
		batt_info->fRSOC = num_100;
		gas_gauge->discharge_sCtMAH = num_0;
		gas_gauge->discharge_fcc_update = num_1;
		gas_gauge->ocv_flag = 0;
		bmu_write_data(OCV_FLAG,0);
	}

	if(batt_info->fRSOC >= num_95)
	{
		gas_gauge->discharge_fcc_update = num_1;
	}

	check_shutdwon_voltage();
	fRSOC_PRE = batt_info->fRSOC;

	if(gas_gauge->ocv_flag)
	{
		charge_fcc_update = 0;
		discharge_fcc_update = 0;
	}
	else
	{
		charge_fcc_update = 1;
		discharge_fcc_update = 1;

	}

	

	


}

/*****************************************************************************
* Description:
*		power down  8806 chip
* Parameters:
*		None
* Return:
*		None
*****************************************************************************/
void bmu_power_down_chip(void)
{
	
	if(oz8806_cell_num > num_1)
		afe_register_write_byte(OZ8806_OP_CTRL,num_0x4c |SLEEP_OCV_EN);
	else
		afe_register_write_byte(OZ8806_OP_CTRL,SLEEP_MODE | SLEEP_OCV_EN);

	afe_read_cell_temp(&batt_info->fCellTemp);
	afe_read_cell_volt(&batt_info->fVolt);
	afe_read_current(&batt_info->fCurr);
	afe_read_car(&batt_info->fRC);
	
	if(parameter->config->debug){	
		printk("eeee power down  oz8806 \n");
		printk("eeee batt_info->fVolt is %d\n",(batt_info->fVolt * oz8806_cell_num));
		printk("eeee batt_info->fRSOC is %d\n",batt_info->fRSOC);
		printk("eeee batt_info->sCaMAH is %d\n",batt_info->sCaMAH);
		printk("eeee batt_info->fRC is %d\n",batt_info->fRC);
		printk("eeee batt_info->fCurr is %d\n",batt_info->fCurr);
	}
	
}

/*****************************************************************************
* Description:
*		power up  8806 chip
* Parameters:
*		None
* Return:
*		None
*****************************************************************************/
void bmu_wake_up_chip(void)
{
	uint8_t data;
	int32_t ret;
	int32_t value;
	uint8_t discharge_flag = num_0;
	uint8_t i;
	static uint32_t charge_tick = 0;

	printk("fCurr is %d\n",batt_info->fCurr);
	if(batt_info->fCurr < gas_gauge->discharge_current_th)
	{
		sleep_charge_flag = 0;
		discharge_flag = 1;
	}
	else
		sleep_charge_flag = 1;

	bmu_sleep_flag = 1;
	
	
	ret = afe_register_read_byte(OZ8806_OP_CTRL,&data);//wake up OZ8806 into FullPower mode
	if((data & 0x40) || (ret < 0))
	{
		printk("bmu_wake_up_chip read 0x09 ret is %d,i is %d \n",ret,data);
		if(oz8806_cell_num > 1)
		{
			afe_register_write_byte(OZ8806_OP_CTRL,0x2c);
		}
		else
			afe_register_write_byte(OZ8806_OP_CTRL,0x20);
		printk("oz8806 wake up function\n");

	}

	afe_read_current(&batt_info->fCurr);
	afe_read_cell_volt(&batt_info->fVolt);
	//very dangerous
	if(batt_info->fVolt <= (config_data->discharge_end_voltage - SHUTDOWN_TH2))
	{
		discharge_end_process();
		return;
	}

	
	if(batt_info->fRSOC >=num_100)
	{
		if(batt_info->fRSOC >= num_100)	batt_info->fRSOC = num_100;
		if(batt_info->sCaMAH > (gas_gauge->fcc_data + config_data->design_capacity / 100))
		{
			printk("oz8806 wake up batt_info.sCaMAH big error is %d\n",batt_info->sCaMAH);
			batt_info->sCaMAH = gas_gauge->fcc_data + config_data->design_capacity / 100;
			
		}
		if((!discharge_flag) && (batt_info->fVolt >= (config_data->charge_cv_voltage - 150)))
		{
			afe_read_car(&batt_info->fRC);
			printk("batt_info->fRC is %d\n",batt_info->fRC);
			if(batt_info->fRC > num_0)
				batt_info->fRCPrev = batt_info->fRC;
			else
				oz8806_over_flow_prevent();
			return;
		}
			
	}
	
	afe_read_current(&batt_info->fCurr);
	afe_read_current(&batt_info->fCurr);
	printk("sCaMAH:%d,fRC:%d,fcurr:%d,volt:%d,sCtMAH:%d\n",
		batt_info->sCaMAH,batt_info->fRC,batt_info->fCurr,
		(batt_info->fVolt * oz8806_cell_num),
		(batt_info->fVolt * oz8806_cell_num));
	
	for(i = num_0;i< num_3;i++)
	{
		ret = afe_read_car(&batt_info->fRC);
		if(ret >= num_0)break;
	}
	
	if(batt_info->fRC < num_0)  
	{
	    printk("fRC is %d\n",batt_info->fRC);
		oz8806_over_flow_prevent();
		gas_gauge->charge_fcc_update = num_0;
		gas_gauge->discharge_fcc_update = num_0;
		
		if(discharge_flag)
		{
			//value = batt_info->sCaMAH;
			check_shutdwon_voltage();
			value = one_latitude_table(parameter->ocv_data_num,parameter->ocv,batt_info->fVolt);
			if(value < batt_info->sCaMAH)
			{
				batt_info->sCaMAH = value;
				batt_info->fRCPrev = batt_info->fRC;
				batt_info->fRSOC = batt_info->sCaMAH  * num_100;
				batt_info->fRSOC = batt_info->fRSOC / gas_gauge->fcc_data ;
			}

			if(batt_info->fRSOC <= num_0)
			{
				if((batt_info->fVolt <= config_data->discharge_end_voltage))
				{
					discharge_end_process();
				}
				//wait voltage
				else
				{
					batt_info->fRSOC  = num_1;
					batt_info->sCaMAH = gas_gauge->fcc_data / num_100 ;
					gas_gauge->discharge_end = num_0;
				}
			}
			return;
		}
		else  //charge
		{
			msleep(1000);
			ret = afe_read_current(&batt_info->fCurr);
		
			if((batt_info->fCurr <= config_data->charge_end_current)&&(batt_info->fCurr >= gas_gauge->discharge_current_th)
				&&(batt_info->fVolt >= (config_data->charge_cv_voltage - 150)))
			{
				charge_end_process();
				return;
			}
			else
			{
				//value = batt_info->sCaMAH;
				//check_shutdwon_voltage();
				value = one_latitude_table(parameter->ocv_data_num,parameter->ocv,batt_info->fVolt);
				if(value > batt_info->sCaMAH)
				{
					batt_info->sCaMAH = value;
					batt_info->fRCPrev = batt_info->fRC;
					batt_info->fRSOC = batt_info->sCaMAH  * num_100;
					batt_info->fRSOC = batt_info->fRSOC / gas_gauge->fcc_data ;
				}

				if(batt_info->fRSOC >= num_100)
				{
					batt_info->fRSOC  = num_99;
					batt_info->sCaMAH = gas_gauge->fcc_data - num_1 ;
					gas_gauge->discharge_end = num_0;
				}
				return;
			}
		}	
	}
	
	value = batt_info->fRC - batt_info->fRCPrev;
	printk("fRCPrev:%d,fRC:%d\n",batt_info->fRCPrev,batt_info->fRC);
	
	if(discharge_flag && (batt_info->fRC > batt_info->fRCPrev))
	{
		batt_info->sCaMAH = batt_info->sCaMAH;
			printk("it seems error 1");
	}
	else if((!discharge_flag) && (batt_info->fRC < batt_info->fRCPrev))
	{
		batt_info->sCaMAH = batt_info->sCaMAH; 
			printk("it seems error 2");
	}
	else
	{
		if((!discharge_flag) && (value > 0))
			charge_tick += value;

		printk("charge_tick is %d \n",charge_tick);
		
		if(!discharge_flag)
		{
			if(charge_tick < 25)
				batt_info->sCaMAH += value;
			else
			{
				batt_info->sCaMAH += value - charge_tick / 25;
				charge_tick %= 25;
			}	
		}	
		else
			batt_info->sCaMAH += value;

		if(batt_info->sCaMAH > (gas_gauge->fcc_data + config_data->design_capacity / 100))
		{
			printk("sleep big error sCaMAH is %d\n",batt_info->sCaMAH);
			batt_info->sCaMAH = gas_gauge->fcc_data + config_data->design_capacity / 100;	
		}
		
		gas_gauge->sCtMAH += value;
		gas_gauge->discharge_sCtMAH -= value;
	}
	
	if(gas_gauge->sCtMAH < num_0)	gas_gauge->sCtMAH = num_0;
	if(gas_gauge->discharge_sCtMAH < num_0)	gas_gauge->discharge_sCtMAH = num_0;
	
	if(parameter->config->debug)printk("tttt batt_info->fRC is %d\n",batt_info->fRC);
	
	/*
	if(discharge_flag)
	{
		value = batt_info->sCaMAH;
		check_shutdwon_voltage();
		if(value < batt_info->sCaMAH)
		{
			batt_info->sCaMAH = value;
		}
	}
	*/

	batt_info->fRCPrev = batt_info->fRC;
	batt_info->fRSOC = batt_info->sCaMAH  * num_100;
	batt_info->fRSOC = batt_info->fRSOC / gas_gauge->fcc_data ;

	if((batt_info->fRSOC >= num_100) && (!discharge_flag))
	{	
		ret = afe_read_current(&batt_info->fCurr);
		printk("11batt_info->fCurr is %d\n",batt_info->fCurr);
		msleep(num_1000);
		ret = afe_read_current(&batt_info->fCurr);
		
		if(wait_dc_charger || gas_gauge->ocv_flag || (batt_info->fCurr >= CHARGE_END_CURRENT2))
		{
			if((batt_info->fCurr <= config_data->charge_end_current)&&(batt_info->fCurr >= gas_gauge->discharge_current_th) 
				&& (batt_info->fVolt >= (config_data->charge_cv_voltage - 100)))
			{
				printk("sleep CAR ok and end charge1\n");
				charge_end_process();
				return;
			}
			//wait charger
			else
			{
				batt_info->fRSOC  = 99;
				batt_info->sCaMAH = gas_gauge->fcc_data - 1;
				charge_end = 0;
				printk("sleep wake up waiter charger. is %d\n",batt_info->sCaMAH);
				return;
			}
		}
		else
		{
			if((batt_info->fCurr >= gas_gauge->discharge_current_th)&&(batt_info->fCurr <= CHARGE_END_CURRENT2) 
				&& (batt_info->fVolt >= (config_data->charge_cv_voltage - 100)))
			{
				printk("sleep CAR ok,end charge2\n");
				charge_end_process();
				return;
			}
			printk("nothing to do\n");

		}
	}
	else if(batt_info->fRSOC <= num_0)
	{
		afe_read_cell_volt(&batt_info->fVolt);
		printk("batt_info->fVolt is %d\n",batt_info->fVolt);
		if((batt_info->fVolt <= config_data->discharge_end_voltage))
		{
			printk("sleep CAR ok and end discharge\n");
			discharge_end_process();
			return;
		}

		//wait voltage
		else
		{
			batt_info->fRSOC  = num_1;
			batt_info->sCaMAH = gas_gauge->fcc_data / num_100 ;
			gas_gauge->discharge_end = num_0;
			if(parameter->config->debug)printk("fffff wake up waiter voltage. is %d\n",batt_info->sCaMAH);
			return;
		}
	}
	

}


/*****************************************************************************
* Description:
*		bmu polling loop
* Parameters:
*		None
* Return:
*		None
*****************************************************************************/
static void check_board_offset(void)
{
	int32_t data;
	int32_t ret;
	int32_t offset;
	static uint8_t i = num_0;
	int32_t ret2;


	if(config_data->board_offset != num_0)
	{
		afe_write_board_offset(config_data->board_offset);
		check_offset_flag = num_0;
		return;
		
	}
	
	ret = bmu_check_file(BATT_OFFSET);
	if((ret < num_0) && (i < num_3))
	{
		i++;
		return;
	}
		
	ret2 = afe_read_board_offset(&data);
	//printk("AAAA board_offset is  %d\n",data);

	if(ret < num_0)
	{

		if((data > num_10) || (data <= num_0))
			afe_write_board_offset(7);
		
		if(ret2 >= num_0)
		{
			if((data < num_10) && (data > num_0) && (data != num_0))
			{
				ret = bmu_write_data(BATT_OFFSET,data);
				if(ret <num_0)
					printk("first write board_offset error\n");

				data = bmu_read_data(BATT_OFFSET);
				
				printk("first write board_offset is %d\n",data);
				write_offset = num_1;

			}
		}
	}
	else
	{
		
		offset = bmu_read_data(BATT_OFFSET);
		if(((offset - data) > num_2) || ((offset - data) < -num_2))
			afe_write_board_offset(offset);
	}

	afe_read_board_offset(&data);
	if((data > num_10) || (data <= num_0))
		afe_write_board_offset(num_7);
		
	afe_read_board_offset(&data);
	printk("AAAA board_offset is  %d\n",data);
	check_offset_flag = num_0;


}

/*****************************************************************************
* Description:
*		bmu polling loop
* Parameters:
*		None
* Return:
*		None
*****************************************************************************/
void bmu_polling_loop(void)
{
	int32_t data;

	int32_t ret;
	uint8_t i;

	if(!bmu_init_ok)
	{
		bmu_wait_ready();
		if(bmu_init_ok)	
		{
			check_offset_flag = 1;
		}
		return;
	}
	if(check_offset_flag)
	{
		check_board_offset();
	}
	
	batt_info->fRCPrev = batt_info->fRC;

	afe_read_cell_temp(&batt_info->fCellTemp);
	afe_read_cell_volt(&batt_info->fVolt);
	afe_read_current(&batt_info->fCurr);
	afe_read_car(&batt_info->fRC);
	
	if(parameter->config->debug)
		printk("first sCaMAH:%d,fRC:%d,fRCPrev:%d,fcurr:%d,volt:%d\n",
			batt_info->sCaMAH,batt_info->fRC,batt_info->fRCPrev,
			batt_info->fCurr,(batt_info->fVolt * oz8806_cell_num));

	//this code must be here,very carefull.
	if(bmu_sleep_flag && !sleep_charge_flag)
	{
		if(batt_info->fCurr >= gas_gauge->discharge_current_th)
		{
			batt_info->fCurr += gas_gauge->discharge_current_th;
			if(parameter->config->debug)
				printk("drop current\n");
		}

	}
	
	//back sCaMAH
	
	gas_gauge->sCtMAH += batt_info->fRC - batt_info->fRCPrev;
	gas_gauge->discharge_sCtMAH += batt_info->fRCPrev - batt_info->fRC;
	
	if(gas_gauge->sCtMAH < num_0)  gas_gauge->sCtMAH = num_0;
	if(gas_gauge->discharge_sCtMAH < num_0)  gas_gauge->discharge_sCtMAH = num_0;

	bmu_call();

	if(parameter->config->debug)
		printk("second sCaMAH:%d,fRC:%d,fRCPrev:%d,fcurr:%d,volt:%d\n",
			batt_info->sCaMAH,batt_info->fRC,batt_info->fRCPrev,
			batt_info->fCurr,(batt_info->fVolt * oz8806_cell_num));

	if(gas_gauge->charge_end)
	{
		if(!charge_end_flag)
		{	
			printk("enter 8806 charge end\n");
			charge_end_flag = num_1;
			charge_end_process();
		}

	}
	else
	{
		charge_end_flag = num_0;
	}

	if(charger_finish)
	{
		if(!charge_end_flag)
		{
			if(batt_info->fRSOC < 99)
			{
				batt_info->fRSOC++;
				batt_info->sCaMAH = batt_info->fRSOC * gas_gauge->fcc_data / num_100;
  				batt_info->sCaMAH += gas_gauge->fcc_data / num_100 - 1;		
			}
			else
			{
				printk("enter charger charge end\n");
				charge_end_flag = num_1;
				gas_gauge->charge_end = 1;
				charge_end_process();
				charger_finish = 0;
			}
			
		}
		else
			charger_finish = 0;
	}
	

	if(gas_gauge->discharge_end)
	{
		if(!discharge_end_flag)
		{
			discharge_end_flag = num_1;
			discharge_end_process();
		}

	}
	else
	{
		discharge_end_flag = num_0;
	}

	oz8806_over_flow_prevent();

	
	//very dangerous
	if(batt_info->fVolt <= (config_data->discharge_end_voltage - num_100))
	{
		discharge_end_process();
	}


	gas_gauge->bmu_tick++;

	if(parameter->config->debug){
		printk("----------------------------------------------------\n");
		printk("AAAA VERSION is %s\n",VERSION);
		printk("AAAA charge_fcc_update is %d\n",gas_gauge->charge_fcc_update);
		printk("AAAA discharge_fcc_update is %d\n",gas_gauge->discharge_fcc_update);
		printk("AAAA batt_info.fVolt is %d  %d\n",(batt_info->fVolt * oz8806_cell_num),gas_gauge->bmu_tick);
		printk("AAAA batt_info.fRSOC is %d  %d\n",batt_info->fRSOC,gas_gauge->bmu_tick);
		printk("AAAA batt_info.sCaMAH is %d\n",batt_info->sCaMAH);
		printk("AAAA sCtMAH1 is %d\n",gas_gauge->sCtMAH);
		printk("AAAA sCtMAH2 is %d\n",gas_gauge->discharge_sCtMAH);
		printk("AAAA fcc is %d\n",gas_gauge->fcc_data);
		printk("AAAA batt_info.fRC is %d\n",batt_info->fRC);
		printk("AAAA batt_info.fCurr is %d  %d\n",batt_info->fCurr,gas_gauge->bmu_tick);
		printk("AAAA batt_info.fCellTemp is %d  %d\n",batt_info->fCellTemp,gas_gauge->bmu_tick);
		printk("AAAA batt_info.i2c_error_times++ is %d\n", batt_info->i2c_error_times);
		printk("charger_finish is %d\n",charger_finish);
		printk(" gas_gauge->charge_end is %d\n",gas_gauge->charge_end);
		printk(" charge_end_flag is %d\n",charge_end_flag);
		printk(" gas_gauge->discharge_end is %d\n",gas_gauge->discharge_end);
		printk("----------------------------------------------------\n");
	}

	if(bmu_sleep_flag)
	{
		bmu_sleep_flag = num_0;
		if(sleep_charge_flag)
		{
			msleep(500);
			
			afe_read_current(&batt_info->fCurr);
			afe_read_cell_volt(&batt_info->fVolt);
			printk("sleep charge fCurr is %d\n",batt_info->fCurr);
			if((batt_info->fCurr <= config_data->charge_end_current) && (batt_info->fCurr >= gas_gauge->discharge_current_th) && (batt_info->fVolt > (config_data->charge_cv_voltage - 150) ))
			{
				if(batt_info->fRSOC < 100)
				{
					charge_end_process();	
					printk("wake up fast finish charger\n");
				}
			}	
			
		}
		if(charger_finish)
		{		
			printk("enter dc charge end\n");
			charge_end = 1;
			charge_end_process();
			charger_finish = 0;
		}

	
	}
	
	data = bmu_read_data(BATT_CAPACITY);
	if(batt_info->sCaMAH > (gas_gauge->fcc_data + config_data->design_capacity / 100))
	{
		printk("big error sCaMAH is %d\n",batt_info->sCaMAH);
		batt_info->sCaMAH = gas_gauge->fcc_data + config_data->design_capacity / 100;	
	}

	if(gas_gauge->fcc_data > (config_data->design_capacity *FCC_UPPER_LIMIT / 10))
	{	
		gas_gauge->fcc_data=  config_data->design_capacity  * FCC_UPPER_LIMIT / 10 ;
		bmu_write_data(BATT_FCC,gas_gauge->fcc_data);
	
		printk("fcc error is %d\n",gas_gauge->fcc_data);

	}
	if((gas_gauge->fcc_data <= 0) || (gas_gauge->fcc_data  < (config_data->design_capacity * FCC_LOWER_LIMIT / 100)))
	{	
		gas_gauge->fcc_data = config_data->design_capacity * FCC_LOWER_LIMIT /100;
		bmu_write_data(BATT_FCC,gas_gauge->fcc_data);
	
		printk("fcc error is %d\n",gas_gauge->fcc_data);

	}
	
	if(config_data->debug)printk("read from RAM batt_info->sCaMAH is %d\n",data);
	if(data >= num_0)
	{
		if(fRSOC_PRE != batt_info->fRSOC)
		{
			fRSOC_PRE = batt_info->fRSOC;
			bmu_write_data(BATT_CAPACITY,batt_info->sCaMAH);
			if(config_data->debug)printk("o2 back batt_info->sCaMAH num_1 is %d\n",batt_info->sCaMAH);
			return;

		}
		
		if(((batt_info->sCaMAH - data)> (gas_gauge->fcc_data/200))||((data - batt_info->sCaMAH)> (gas_gauge->fcc_data/200))){
			bmu_write_data(BATT_CAPACITY,batt_info->sCaMAH);
			if(config_data->debug)printk("o2 back batt_info->sCaMAH 2 is %d\n",batt_info->sCaMAH);
		}
	}


	//wake up OZ8806 into FullPower mode
	ret = afe_register_read_byte(OZ8806_OP_CTRL,&i);

	if((i & num_0x40) || (ret < num_0))
	{
		printk("bmu_polling_loop read 0x09 ret is %d,i is %d \n",ret,i);
		if(oz8806_cell_num > 1)
		{
			afe_register_write_byte(OZ8806_OP_CTRL,num_0x2c);
		}
		else
			afe_register_write_byte(OZ8806_OP_CTRL,num_0x20);
		printk("oz8806 wake up function\n");

	}

	
	
}

/*****************************************************************************
* Description:
*		 prevent oz8806 over flow 
* Parameters:
*		None
* Return:
*		None
*****************************************************************************/
static void oz8806_over_flow_prevent(void)
{
	int32_t ret;

    if((batt_info->fRSOC > num_0) && gas_gauge->discharge_end)
		gas_gauge->discharge_end = num_0;
   
	if((batt_info->fRSOC < num_100) && gas_gauge->charge_end)
		gas_gauge->charge_end = num_0;

	if(batt_info->fRC< num_0)
	{
		if(batt_info->fVolt >= (config_data->charge_cv_voltage - 200))
		{	
			batt_info->fRC = gas_gauge->fcc_data -num_1;
		}
		else
		{				
			batt_info->fRC = batt_info->fRSOC * gas_gauge->fcc_data / num_100 - num_1;
		}
		afe_write_car(batt_info->fRC);
		batt_info->fRCPrev = batt_info->fRC;
	}

	ret = oz8806_read_word(OZ8806_OP_CAR);

	if(ret >= num_0)
	{
		ret = (int16_t)ret;
		if(ret >= (num_32768 - num_10 * config_data->fRsense))
		{
			if(parameter->config->debug)printk("yyyy  CAR WILL UP OVER %d\n",ret);
			ret = 32768 - 15 * config_data->fRsense;
			oz8806_write_word(OZ8806_OP_CAR,(int16_t)ret);
			afe_read_car(&batt_info->fRC);
			batt_info->fRCPrev = batt_info->fRC;
				
		}
		else if(ret <= (num_10 * config_data->fRsense))
		{
			if(parameter->config->debug)printk("yyyy  CAR WILL DOWN OVER %d\n",ret);
			ret =  num_15 * config_data->fRsense;
			oz8806_write_word(OZ8806_OP_CAR,(int16_t)ret);
			afe_read_car(&batt_info->fRC);
			batt_info->fRCPrev = batt_info->fRC;	
		}	
				

	}	


	if(((batt_info->sCaMAH - batt_info->fRC) > (config_data->design_capacity / num_100)) || ((batt_info->fRC- batt_info->sCaMAH) > (config_data->design_capacity / num_100)))
	{
		if((batt_info->sCaMAH < (gas_gauge->overflow_data - gas_gauge->overflow_data / num_100)) && (batt_info->sCaMAH > gas_gauge->overflow_data / num_100))
		{
			afe_write_car(batt_info->sCaMAH);
			batt_info->fRCPrev = batt_info->sCaMAH;
			batt_info->fRC 	= batt_info->sCaMAH;
			if(parameter->config->debug){
				printk("dddd write car batt_info->fRCPrev is %d\n",batt_info->fRCPrev);
				printk("dddd write car batt_info->fRC is %d\n",batt_info->fRC);
				printk("dddd write car batt_info->sCaMAH is %d\n",batt_info->sCaMAH);


			}

		}
	}
	

}



/*****************************************************************************
* Description:
*		charge fcc update process
* Parameters:
*		 None
* Return:
*		None
*****************************************************************************/
static void charge_end_process(void)
{
	//FCC UPdate
	if(gas_gauge->charge_fcc_update)
	{
		if(batt_info->fCurr < config_data->charge_end_current)
			gas_gauge->fcc_data = gas_gauge->sCtMAH;
		bmu_write_data(BATT_FCC,gas_gauge->fcc_data);
		
		printk("charge1 fcc update is %d\n",gas_gauge->fcc_data);

	}

	if(gas_gauge->fcc_data > (config_data->design_capacity *FCC_UPPER_LIMIT / 100))
	{	
		gas_gauge->fcc_data=  config_data->design_capacity  * FCC_UPPER_LIMIT / 100 ;
		bmu_write_data(BATT_FCC,gas_gauge->fcc_data);
	
		printk("charge2 fcc update is %d\n",gas_gauge->fcc_data);

	}
	if((gas_gauge->fcc_data <= 0) || (gas_gauge->fcc_data  < (config_data->design_capacity * FCC_LOWER_LIMIT / 100)))
	{	
		gas_gauge->fcc_data = config_data->design_capacity * FCC_LOWER_LIMIT / 100;
		bmu_write_data(BATT_FCC,gas_gauge->fcc_data);
	
		printk("charge3 fcc update is %d\n",gas_gauge->fcc_data);

	}
	

	batt_info->sCaMAH = gas_gauge->fcc_data + config_data->design_capacity / 100;;
	bmu_write_data(BATT_CAPACITY,batt_info->sCaMAH);

	if(gas_gauge->ocv_flag)
		bmu_write_data(OCV_FLAG,0);
	
	printk("yyyy  end charge \n");	
	batt_info->fRSOC = num_100;
	gas_gauge->charge_end = num_1;
	charge_end_flag = num_1;
	charger_finish = 0;
	gas_gauge->charge_fcc_update = num_0;
	gas_gauge->discharge_fcc_update = num_1;
	gas_gauge->discharge_sCtMAH = num_0;
	power_on_flag = 0;
	
}

/*****************************************************************************
* Description:
*		discharge fcc update process
* Parameters:
*		 None
* Return:
*		None
*****************************************************************************/
static void discharge_end_process(void)
{
	int32_t voltage_end = config_data->discharge_end_voltage;

	//FCC UPdate
	if(gas_gauge->discharge_fcc_update)
	{
		gas_gauge->fcc_data = gas_gauge->discharge_sCtMAH;
		bmu_write_data(BATT_FCC,gas_gauge->fcc_data);
		printk("discharge1 fcc update is %d\n",gas_gauge->fcc_data);

	}

	if(gas_gauge->fcc_data > (config_data->design_capacity *FCC_UPPER_LIMIT / 100))
	{	
		gas_gauge->fcc_data=  config_data->design_capacity  * FCC_UPPER_LIMIT / 100 ;	
		bmu_write_data(BATT_FCC,gas_gauge->fcc_data);
		printk("discharge2 fcc update is %d\n",gas_gauge->fcc_data);
	}
	if((gas_gauge->fcc_data <= 0) || (gas_gauge->fcc_data  < (config_data->design_capacity * FCC_LOWER_LIMIT / 100)))
	{	
		gas_gauge->fcc_data = config_data->design_capacity * FCC_LOWER_LIMIT / 100;
		bmu_write_data(BATT_FCC,gas_gauge->fcc_data);
		printk("discharge3 fcc update is %d\n",gas_gauge->fcc_data);
	}
	
	if(gas_gauge->ocv_flag)
		bmu_write_data(OCV_FLAG,0);
		
	batt_info->sCaMAH = gas_gauge->fcc_data / 100 - 1;
	bmu_write_data(BATT_CAPACITY,batt_info->sCaMAH);
	
	afe_write_car(batt_info->sCaMAH);
	batt_info->fRCPrev = batt_info->sCaMAH;
	
	
	printk("yyyy  end discharge \n");
	batt_info->fRSOC = num_0;
	gas_gauge->discharge_end = num_1;
	gas_gauge->discharge_fcc_update = num_0;
	gas_gauge->charge_fcc_update = num_1;
	gas_gauge->sCtMAH = num_0;
	
}

/*****************************************************************************
* Description:
*		one_latitude_table
* Parameters:
*		description for each argument, new argument starts at new line
* Return:
*		what does this function returned?
*****************************************************************************/
static int32_t one_latitude_table(int32_t number,one_latitude_data_t *data,int32_t value)
{
	int j;
	int res;
	
	for (j = num_0;j < number;j++)
	{
		if (data[j].x ==value)
		{
			res = data[j].y;
			return res;
		}
		if(data[j].x > value)
			break;
	}
	
	if(j == num_0)
		res = data[j].y;
	else if(j == number)
		res = data[j -num_1].y;
	else
	{
		res = ((value - data[j -num_1].x) * (data[j].y - data[j -num_1].y));
		
		if((data[j].x - data[j -num_1].x) != num_0)
			res = res / (data[j].x  - data[j-num_1].x );
		res += data[j-num_1].y;
	}

	
	return res;
}	


/*****************************************************************************
* Description:
*		below is linux file operation
* Parameters:
*		description for each argument, new argument starts at new line
* Return:
*		what does this function returned?
*****************************************************************************/
static int bmu_check_file(char * address)
{
        long fd = sys_open(address,O_RDONLY,num_0);
    
        if(fd < num_0)
        {
                //if(config_data->debug)printk(" bmu check file  fail %s\n",address);
				return -num_1;
        } 
        sys_close(fd);
        return (num_0);
}


static int bmu_read_data(char * address)
{
        char value[4];
        int* p = (int *)value;
        long fd = sys_open(address,O_RDONLY,num_0);
    
        if(fd < num_0)
        {
                if(config_data->debug)printk(" bmu read open file fail %s\n",address);
				return -num_1;
        }
        
        sys_read(fd,(char __user *)value,4);
        
        sys_close(fd);
    
        return (*p);
}

static int bmu_write_data(char * address,int data)
{
        char value[4];
        int* p = (int *)value;
        long fd = sys_open(address,O_CREAT | O_RDWR,num_0);
    
        if(fd < num_0)
        {
                if(config_data->debug)printk("bmu wirte open file fail %s failed\n",address);
                return -num_1;
        }
        *p = data;
        sys_write(fd, (const char __user *)value, 4);
        
        sys_close(fd);
		return num_0;
}


static int bmu_write_string(char * address,char * data)
{
        
	int32_t len;
   
    long fd = sys_open(address,O_CREAT | O_RDWR | O_APPEND,num_0);

    if(fd < num_0)
    {
            if(config_data->debug)printk("bmu wirte open file fail %s failed\n",address);
            return -num_1;
    }
    len = strlen(data);
    sys_write(fd, (const char __user *)data, len);
    
    sys_close(fd);
	return num_0;
}

//----------------------------------------------------------------------------
//----------------------------------------------------------------------------
//----------------------------------------------------------------------------
//----------------------------------------------------------------------------
//----------------------------------------------------------------------------
//----------------------------------------------------------------------------
//----------------------------------------------------------------------------
//----------------------------------------------------------------------------

/****************************************************************************
 * Description:
 *		oz8806_write_byte_pec
 * Parameters:
 *		//addr:		oz8806 slave address
 *      	index:		oz8806 operation register 
 *		data: 		write data
 * Return:
 *		see I2C_STATUS_XXX define	
 ****************************************************************************/
static int32_t oz8806_write_byte_pec(uint8_t index, uint8_t data)
{
	int32_t ret;
	
	uint8_t pec_data = num_0;
	uint16_t temp = num_0;
	pec_data =pec_calculate(pec_data,0x5e);
	pec_data =pec_calculate(pec_data,index);
	pec_data =pec_calculate(pec_data,data);
	temp = pec_data;
	temp <<= 8;
	temp |= data;
	ret = i2c_smbus_write_word_data(parameter->client, index,temp);
	return ret;
}

/****************************************************************************
 * Description:
 *		read oz8806 operation register
 * Parameters:
 *		//addr:		oz8806 slave address
 *      	index:		oz8806 operation register 
 *		buf: 		pointer to buffer to store read data
 * Return:
 *		see I2C_STATUS_XXX define	
 ****************************************************************************/
static int32_t oz8806_read_byte(uint8_t index)
{
	int32_t ret;
	uint8_t pec_data = num_0;
	uint8_t temp;
	

	if(num_0)
	{
		ret = i2c_smbus_read_word_data(parameter->client, index);
		if(ret < num_0 ) return ret;
		pec_data =pec_calculate(pec_data,0x5e);
		pec_data =pec_calculate(pec_data,index);
		pec_data =pec_calculate(pec_data,0x5f);
		temp = (uint8_t)(ret & 0xff);
		pec_data =pec_calculate(pec_data,temp);
		temp = (uint8_t)(ret >> 8);
		
		if(pec_data != temp)
		{ 
			printk("33333333333333333 oz8806_read_byte\n");
			printk("33333333333333333 pec_data is %d\n",pec_data);
			printk("33333333333333333 temp is %d\n",temp);
			return -num_1;
		}
			
		return ret;
	}
	else
	{
		ret = i2c_smbus_read_byte_data(parameter->client, index);
		return ret;
	}
}


/****************************************************************************
 * Description:
 *		write oz8806 operation register
 * Parameters:
 *		//addr:		oz8806 slave address
 *      	index:		oz8806 operation register 
 *		data: 		write data
 * Return:
 *		see I2C_STATUS_XXX define	
 ****************************************************************************/
static int32_t oz8806_write_byte(uint8_t index, uint8_t data)
{
	int32_t ret;
	uint8_t pec_data = num_0;
	uint16_t temp = num_0;
	
	if(oz8806_pec_check)
	{
		pec_data =pec_calculate(pec_data,0x5e);
		pec_data =pec_calculate(pec_data,index);
		pec_data =pec_calculate(pec_data,data);
		temp = pec_data;
		temp <<= 8;
		temp |= data;
		ret = i2c_smbus_write_word_data(parameter->client, index,temp);
		return ret;
	}
	else
	{
		ret = i2c_smbus_write_byte_data(parameter->client, index,data);
		return ret;
	}
}

/****************************************************************************
 * Description:
 *		read oz8806 operation register word
 * Parameters:
 *		//addr:		oz8806 slave address
 *      	index:		oz8806 operation register 
 *		buf: 		pointer to buffer to store read data
 * Return:
 *		see I2C_STATUS_XXX define	
 ****************************************************************************/
static int32_t oz8806_read_word(uint8_t index)
{
	int32_t ret = num_0;
	int32_t ret1 = num_0;
	uint8_t temp;
	uint8_t pec_data = num_0;
	
	if(num_0)
	{
		ret = i2c_smbus_read_word_data(parameter->client, index);
		if(ret < num_0)	return ret;
		pec_data =pec_calculate(pec_data,0x5e);
		pec_data =pec_calculate(pec_data,index);
		pec_data =pec_calculate(pec_data,0x5f);
		temp = (uint8_t)(ret & 0xff);
		pec_data =pec_calculate(pec_data,temp);
		temp = (uint8_t)(ret >> 8);

		if(pec_data != temp)
		{ 
			printk("index %x\n",index);
			printk("%x\n",(uint8_t)(ret & 0xff));
			printk("33333333333333333 oz8806_read_word num_1\n");
			printk("33333333333333333 pec_data is %x\n",pec_data);
			printk("33333333333333333 temp is %x\n",temp);

			printk("33333333333333333 ret is %x\n",ret);
			return -num_1;
		}
		
		ret1 = i2c_smbus_read_word_data(parameter->client, (index + num_1));
		if(ret1 < num_0)	return ret1;

		pec_data = num_0;
		pec_data =pec_calculate(pec_data,0x5e);
		pec_data =pec_calculate(pec_data,(index +num_1));
		pec_data =pec_calculate(pec_data,0x5f);
		temp = (uint8_t)(ret1 & 0xff);
		pec_data =pec_calculate(pec_data,temp);
		temp = (uint8_t)(ret1 >> 8);

		if(pec_data != temp)
		{ 
			printk("33333333333333333 oz8806_read_word 2\n");
			printk("33333333333333333 pec_data is %d\n",pec_data);
			printk("33333333333333333 temp is %d\n",temp);
			return -num_1;
		}
		
		ret1 <<= 8;
		ret1 |= (uint8_t)(0xff & ret);
		return ret1;
	}
	else
	{	
		ret = i2c_smbus_read_word_data(parameter->client, index);
		return ret;
	}
}

/****************************************************************************
 * Description:
 *		write oz8806 operation register
 * Parameters:
 *		//addr:		oz8806 slave address
 *      	index:		oz8806 operation register 
 *		data: 		write data
 * Return:
 *		see I2C_STATUS_XXX define	
 ****************************************************************************/
static int32_t oz8806_write_word( uint8_t index, uint16_t data)
{
	int32_t ret;
	uint8_t pec_data = num_0;
	uint16_t temp = num_0;

	
	if(oz8806_pec_check)
	{
	
		pec_data =pec_calculate(pec_data,0x5e);
		pec_data =pec_calculate(pec_data,index);
		pec_data =pec_calculate(pec_data,(uint8_t)data);
		temp = pec_data;
		temp <<= 8;
		temp |=  (uint8_t)data;
		ret  = i2c_smbus_write_word_data(parameter->client, index,temp);
		
		if(ret < num_0) return ret;
		pec_data = num_0;
		
		pec_data =pec_calculate(pec_data,0x5e);
		pec_data =pec_calculate(pec_data,(index +num_1));
		pec_data =pec_calculate(pec_data,(uint8_t)(data>>8));
		temp = pec_data;
		temp <<= 8;
		temp |= (uint8_t)(data >> 8);
		ret = i2c_smbus_write_word_data(parameter->client, (index +num_1),temp);

		return ret;
	}
	else
	{
		ret = i2c_smbus_write_word_data(parameter->client, index,data);
		return ret;
	}
}

/****************************************************************************
 * Description:
 *		read oz8806 cell voltage
 * Parameters: 
 *		voltage: 	cell voltage in mV, range from -5120mv to +5120mv
 * Return:
 *		see I2C_STATUS_XXX define	
 ****************************************************************************/
static int32_t oz8806_cell_voltage_read(int32_t *voltage)
{
	int32_t ret;
	int32_t data;
	
	ret = oz8806_read_word(OZ8806_OP_CELL_VOLT);
	if (ret < num_0)  return ret;
	
	data = ret;
	data = data >> 4;
	data = data & CELL_VOLT_MASK;
	data = (data * parameter->config->fVoltLSB) / num_100;	//fVoltLSB = 250 (2.5 mV)

	*voltage = (int16_t)data;
	
	return ret;
}

/****************************************************************************
 * Description:
 *		read oz8806 cell voltage
 * Parameters: 
 *		voltage: 	cell voltage in mV, range from -5120mv to +5120mv
 * Return:
 *		see I2C_STATUS_XXX define	
 ****************************************************************************/
static int32_t oz8806_ocv_voltage_read(int32_t *voltage)
{
	int32_t ret;
	int32_t data;
	
	ret = oz8806_read_word(OZ8806_OP_OCV_LOW);
	if (ret < num_0)  return ret;
	
	data = ret;
	data = data >> 4;
	data = data & CELL_VOLT_MASK;
	data = (data * parameter->config->fVoltLSB) / num_100;	//fVoltLSB = 250 (2.5 mV)

	*voltage = (int16_t)data;
	
	return ret;
}

/****************************************************************************
 * Description:
 *		read oz8806 current
 * Parameters: 
 *		current: 	current value in mA, range is +/-64mv with 7.8uv LSB / Rsense
 * Return:
 *		see I2C_STATUS_XXX define	
 ****************************************************************************/
static int32_t oz8806_current_read(int32_t *data)

{
	int32_t ret;
	int32_t temp;
	
	ret = oz8806_read_word(OZ8806_OP_CURRENT);
	temp = ret & 0xFFFF;
	temp = (int16_t)temp * parameter->config->dbCurrLSB;	//dbCurrLSB = 391 (3.90625 mA)
	temp = (temp / parameter->config->fRsense) / num_100;
	temp /= 4;

	*data = temp;
	return ret;
}

/****************************************************************************
 * Description:
 *		read oz8806 temp
 * Parameters: 
 *		current: 	current value in mA, range is +/-64mv with 7.8uv LSB / Rsense
 * Return:
 *		see I2C_STATUS_XXX define	
 ****************************************************************************/
static int32_t oz8806_temp_read(int32_t *voltage)
{
	int32_t ret;
	int32_t data;
	
	ret = oz8806_read_word(OZ8806_OP_CELL_TEMP);
	if (ret < num_0)  return ret;

	
	//printk("1111111111111 regeister is %x\n",ret);
	
	data = ret;
	data = data >> 4;
	data = data & CELL_VOLT_MASK;
	data = (data * parameter->config->fVoltLSB) / num_100;	//fVoltLSB = 250 (2.5 mV)

	*voltage = (int16_t)data;

	//printk("1111111111111 voltage is %d\n",*voltage);
	
	return ret;
}

/****************************************************************************
 * Description:
 *		read oz8806 CAR
 * Parameters: 
 *		current: 	current value in mA, range is +/-64mv with 7.8uv LSB / Rsense
 * Return:
 *		see I2C_STATUS_XXX define	
 ****************************************************************************/
static int32_t oz8806_car_read(int32_t *car)
{
	int32_t ret;
	int32_t data;

	
	ret = oz8806_read_word(OZ8806_OP_CAR);
	if (ret < num_0)  return ret;

		
	data = (int16_t)ret;
	data = data * parameter->config->dbCARLSB;
	data = data / parameter->config->fRsense;

	*car = (int16_t)data;
	return ret;
}


/****************************************************************************
 * Description:
 *		read oz8806 CAR
 * Parameters: 
 *		current: 	current value in mA, range is +/-64mv with 7.8uv LSB / Rsense
 * Return:
 *		see I2C_STATUS_XXX define	
 ****************************************************************************/
static int32_t oz8806_car_write(int32_t data)
{
	int32_t temp;
	int32_t ret;
	
	temp = data;
	temp = (temp * parameter->config->fRsense) / parameter->config->dbCARLSB;		//transfer to CAR
	ret = oz8806_write_word(OZ8806_OP_CAR,(uint16_t)temp);

	return ret;
}

/****************************************************************************
 * Description:
 *		read oz8806 board offset
 * Parameters: 
 *		data: 	 board offset 
 * Return:
 *		see I2C_STATUS_XXX define	
 ****************************************************************************/
static int32_t oz8806_board_offset_read(int32_t *data)
{
	int32_t ret;
	int32_t temp;

	
	ret = oz8806_read_word(OZ8806_OP_BOARD_OFFSET);
	if (ret < num_0)  return ret;

		
	temp = (int16_t)ret;
	temp = temp & 0x7ff;
	if(temp & 0x0400)
		temp = temp |0xfffffc00;
	temp = temp * 78;
	temp = temp / (config_data->fRsense * num_10);

	*data = (int16_t)temp;
	return ret;
}

/****************************************************************************
 * Description:
 *		write oz8806 board offset
 * Parameters: 
 *		data: 	the data will be wrriten
 * Return:
 *		see I2C_STATUS_XXX define	
 ****************************************************************************/
static int32_t oz8806_board_offset_write(int32_t data)
{
	int32_t ret;
	int32_t over_data;
	
	
	
	if(data > num_0){
		data = data * config_data->fRsense * num_10 / 78;
		if(data > 0x3ff)data = 0x3ff;
		ret = oz8806_write_word(OZ8806_OP_BOARD_OFFSET,(uint16_t)data);
	}
	else if(data < num_0){
		over_data = (-1024 * 78) / (config_data->fRsense * num_10);
		if(data < over_data)
			data = over_data;
		data = data * config_data->fRsense * num_10 / 78;
		data = (data & 0x07ff) | 0x0400;
		ret = oz8806_write_word(OZ8806_OP_BOARD_OFFSET,(uint16_t)data);
	}
	else
		ret = oz8806_write_word(OZ8806_OP_BOARD_OFFSET,num_0);

	return ret;
}


/*****************************************************************************
 * Description:
 *		wrapper function for operation register byte reading 
 * Parameters:
 *		index:	register index to be read
 *		*dat:	buffer to store data read back
 * Return:
 *		I2C_STATUS_OK if read success, else inidcate error
 *****************************************************************************/
 int32_t afe_register_read_byte(uint8_t index, uint8_t *dat)
{
	int32_t ret;
	uint8_t i;

	for(i = num_0; i < RETRY_CNT; i++){
		ret = oz8806_read_byte(index);
		if(ret >=num_0) break;
	}
	if(i >= RETRY_CNT){
		batt_info->i2c_error_times++;
		if(parameter->config->debug)
			printk("yyyy. afe_register_read_byte\n");
		printk("ret is %d\n",ret);
		return ret;
	} 

	*dat = (uint8_t)ret;

	return ret;
}


/*****************************************************************************
 * Description:
 *		wrapper function for operation register byte reading 
 * Parameters:
 *		index:	register index to be read
 *		dat:		write data
 * Return:
 *		see I2C_STATUS_XXX define
 *****************************************************************************/
 int32_t afe_register_write_byte(uint8_t index, uint8_t dat)
{
	int32_t ret;
	uint8_t i;

	for(i = num_0; i < RETRY_CNT; i++){
		ret = oz8806_write_byte(index, dat);
		if(ret >= num_0) break;
	}
	if(i >= RETRY_CNT){
		batt_info->i2c_error_times++;
		if(parameter->config->debug)
		printk("yyyy. afe_register_write_byte\n");
		printk("ret is %d\n",ret);
		return ret;
	} 

	return ret;
}


/*****************************************************************************
 * Description:
 *		wrapper function for operation register word reading 
 * Parameters:
 *		index:	register index to be read
 *		*dat:	buffer to store data read back
 * Return:
 *		I2C_STATUS_OK if read success, else inidcate error
 *****************************************************************************/
static int32_t afe_register_read_word(uint8_t index, uint16_t *dat)
{
	int32_t ret;
	uint8_t i;
	

	for(i = num_0; i < RETRY_CNT; i++){
		ret = oz8806_read_word(index);
		if(ret >= num_0) break;
	}
	if(i >= RETRY_CNT){
		batt_info->i2c_error_times++;
		if(parameter->config->debug)
		printk("yyyy. afe_register_read_word\n");
		printk("ret is %d\n",ret);
		return ret;
	} 

	*dat = (uint16_t)ret;

	return ret;
}


/*****************************************************************************
 * Description:
 *		wrapper function for operation register word reading 
 * Parameters:
 *		index:	register index to be read
 *		dat:		write data
 * Return:
 *		see I2C_STATUS_XXX define
 *****************************************************************************/
static int32_t afe_register_write_word(uint8_t index, uint16_t dat)
{ 
	int32_t ret;
	uint8_t i;

	for(i = num_0; i < RETRY_CNT; i++){
		ret = oz8806_write_word(index, dat);
		if(ret >= num_0) break;
	}
	if(i >= RETRY_CNT){
		batt_info->i2c_error_times++;
		if(parameter->config->debug)
			printk("yyyy. afe_register_write_word\n");
		printk("ret is %d\n",ret);
		return ret;
	} 

	return ret;
}



/*****************************************************************************
 * Description:
 *		wrapper function to read cell voltage 
 * Parameters:
 *		vol:	buffer to store voltage read back
 * Return:
 *		see I2C_STATUS_XXX define
 * Note:
 *		it's acceptable if one or more times reading fail
 *****************************************************************************/
static int32_t afe_read_cell_volt(int32_t *voltage)
{
	int32_t ret;
	uint8_t i;
	int32_t buf;

	for(i = num_0; i < RETRY_CNT; i++){
		ret = oz8806_cell_voltage_read(&buf);
		if(ret >= num_0)
				break;
		msleep(5);
	}
	if(i >= RETRY_CNT){
		batt_info->i2c_error_times++;
		if(parameter->config->debug)
			printk("yyyy. afe_read_cell_volt\n");
		printk("ret is %d\n",ret);
		return ret;
	} 
	if(oz8806_cell_num > num_1)
	{
		*voltage = buf * num_1000 / res_divider_ratio;
		*voltage /= oz8806_cell_num;
	}
	else	
		*voltage = buf;
	return ret;
}


/*****************************************************************************
 * Description:
 *		wrapper function to read cell ocv  voltage 
 * Parameters:
 *		vol:	buffer to store voltage read back

 * Return:
 *		see I2C_STATUS_XXX define
 * Note:
 *		it's acceptable if one or more times reading fail
 *****************************************************************************/
static int32_t afe_read_ocv_volt(int32_t *voltage)
{
	int32_t ret;
	uint8_t i;
	int32_t buf;

	for(i = num_0; i < RETRY_CNT; i++){
		ret = oz8806_ocv_voltage_read(&buf);
		if(ret >= num_0)
				break;
	}
	if(i >= RETRY_CNT){
		batt_info->i2c_error_times++;
		if(parameter->config->debug)
			printk("yyyy. afe_read_cell_volt\n");
		printk("ret is %d\n",ret);
		return ret;
	} 

	if(oz8806_cell_num > num_1)
	{
		*voltage = buf * num_1000 / res_divider_ratio;
		*voltage /= oz8806_cell_num;
	}
	else	
		*voltage = buf;
	
	return ret;
}

/*****************************************************************************
 * Description:
 *		wrapper function to read current 
 * Parameters:
 *		dat:	buffer to store current value read back	
 * Return:
 *		see I2C_STATUS_XXX define
 * Note:
 *		it's acceptable if one or more times reading fail
 *****************************************************************************/
static int32_t afe_read_current(int32_t *dat)
{
	int32_t ret;
	uint8_t i;
	int32_t buf;

	for(i = num_0; i < RETRY_CNT; i++){
		ret = oz8806_current_read(&buf);
		if(ret >= num_0)
				break;
		msleep(5);
	}
	if(i >= RETRY_CNT){
		batt_info->i2c_error_times++;
		if(parameter->config->debug)
			printk("yyyy. afe_read_current\n");
		printk("ret is %d\n",ret);
		return ret;
	} 
	*dat = buf;
	return ret;
}


/*****************************************************************************
 * Description:
 *		wrapper function to read current 
 * Parameters:
 *		dat:	buffer to store current value read back	
 * Return:
 *		see I2C_STATUS_XXX define
 * Note:
 *		it's acceptable if one or more times reading fail
 *****************************************************************************/
static int32_t afe_read_car(int32_t *dat)
{
	int32_t ret;
	uint8_t i;
	int32_t buf;

	for(i = num_0; i < RETRY_CNT; i++){
		ret = oz8806_car_read(&buf);
		if(ret >= num_0)
				break;
		msleep(5);
	}
	if(i >= RETRY_CNT){
		batt_info->i2c_error_times++;
		if(parameter->config->debug)
			printk("yyyy. afe_read_car\n");
		printk("ret is %d\n",ret);
		return ret;
	} 
	*dat = buf;
	return ret;
}



/*****************************************************************************
 * Description:
 *		wrapper function to write car 
 * Parameters:
 *		dat:	buffer to store current value read back	
 * Return:
 *		see I2C_STATUS_XXX define
 * Note:
 *		it's acceptable if one or more times reading fail
 *****************************************************************************/
static int32_t afe_write_car(int32_t date)
{
	int32_t ret;
	uint8_t i;


	for(i = num_0; i < RETRY_CNT; i++){
		ret = oz8806_car_write(date);
		if(ret >= num_0)	break;
		msleep(5);
	}
	if(i >= RETRY_CNT){
		batt_info->i2c_error_times++;
		if(parameter->config->debug)
			printk("yyyy. afe_write_carte\n");
		printk("ret is %d\n",ret);
		return ret;
	} 
	return ret;
}

/*****************************************************************************
 * Description:
 *		wrapper function to write board offset 
 * Parameters:
 *		dat:	buffer to store current value read back	
 * Return:
 *		see I2C_STATUS_XXX define
 * Note:
 *		it's acceptable if one or more times reading fail
 *****************************************************************************/
static int32_t afe_write_board_offset(int32_t date)
{
	int32_t ret;
	uint8_t i;


	for(i = num_0; i < RETRY_CNT; i++){
		ret = oz8806_board_offset_write(date);
		if(ret >= num_0)	break;
	}
	if(i >= RETRY_CNT){
		batt_info->i2c_error_times++;
		if(parameter->config->debug)
			printk("yyyy. afe_write_board_offset\n");
		printk("ret is %d\n",ret);
		return ret;
	} 
	return ret;
}

/*****************************************************************************
 * Description:
 *		wrapper function to read board offset 
 * Parameters:
 *		dat:	buffer to store current value read back	
 * Return:
 *		see I2C_STATUS_XXX define
 * Note:
 *		it's acceptable if one or more times reading fail
 *****************************************************************************/
static int32_t afe_read_board_offset(int32_t *dat)
{
	int32_t ret;
	uint8_t i;
	int32_t buf;

	for(i = num_0; i < RETRY_CNT; i++){
		ret = oz8806_board_offset_read(&buf);
		if(ret >= num_0)
				break;
	}
	if(i >= RETRY_CNT){
		batt_info->i2c_error_times++;
		if(parameter->config->debug)
			printk("yyyy. afe_read_car\n");
		printk("ret is %d\n",ret);
		return ret;
	} 
	*dat = buf;
	return ret;
}
/*****************************************************************************
 * Description:
 *		wrapper function to read current 
 * Parameters:
 *		dat:	buffer to store current value read back	
 * Return:
 *		see I2C_STATUS_XXX define
 * Note:
 *		it's acceptable if one or more times reading fail
 *****************************************************************************/
static int32_t afe_read_cell_temp(int32_t *data)
{
	int32_t ret;
	uint8_t i;
	int32_t temp;
	int32_t buf;


	for(i = num_0; i < RETRY_CNT; i++){
		ret = oz8806_temp_read(&buf);
		if(ret >= num_0)	break;
		msleep(5);
	}
	if(i >= RETRY_CNT){
		batt_info->i2c_error_times++;
		if(parameter->config->debug)
			printk("yyyy afe_write_carte\n");
		printk("ret is %d\n",ret);
		return ret;
	} 

	
	if((buf >= config_data->temp_ref_voltage)|| (buf <= num_0)){
		*data = num_25;
	}
	else{
		temp = buf * config_data->temp_pull_up;
		temp = temp / (config_data->temp_ref_voltage - buf);
		*data =	one_latitude_table(parameter->cell_temp_num,parameter->temperature,temp);
		 
	}

	//printk("1111111111111 r is %d\n",temp);
	return ret;
}

/*****************************************************************************
 * Description:
 *		pec_calculate
 * Parameters:
 *		ucCrc:	source
 *		ucData: data
 * Return:
 *		crc data
 * Note:
 *		
 *****************************************************************************/
static uint8_t pec_calculate (uint8_t ucCrc, uint8_t ucData)
{
       uint8_t j;
       for (j = num_0x80; j != num_0; j >>= num_1)
       {
              if((ucCrc & num_0x80) != num_0)
              {
                     ucCrc <<= num_1;
                     ucCrc ^= num_0x07;
              }
              else
                     ucCrc <<= num_1;
              if (ucData & j)
                     ucCrc ^= num_0x07;
       }
       return ucCrc;
}

/*****************************************************************************
 * Description:
 *		pec_calculate
 * Parameters:
 *		ucCrc:	source
 *		ucData: data
 * Return:
 *		crc data
 * Note:
 *		
 *****************************************************************************/
static int32_t i2c_read_byte(uint8_t addr,uint8_t index,uint8_t *data)
{
	
	struct i2c_adapter *adap=parameter->client->adapter;
	struct i2c_msg msgs[num_2];
	int32_t ret;
	#if 0
	int scl_rate= num_100 * num_1000;   
	msgs[num_0].addr = addr;
	msgs[num_0].flags = parameter->client->flags;
	msgs[num_0].len = num_1;
	msgs[num_0].buf = &index;
	msgs[num_0].scl_rate = scl_rate;

	msgs[num_1].addr = addr;
	msgs[num_1].flags = parameter->client->flags | I2C_M_RD;
	msgs[num_1].len = num_1;
	msgs[num_1].buf = (char *)data;
	msgs[num_1].scl_rate = scl_rate;

	ret = i2c_transfer(adap, msgs, num_2);

	return (ret == num_2)? num_0 : ret;
	#endif
}

/*****************************************************************************
 * Description:
 *		pec_calculate
 * Parameters:
 *		ucCrc:	source
 *		ucData: data
 * Return:
 *		crc data
 * Note:
 *		
 *****************************************************************************/
static int32_t i2c_write_byte(uint8_t addr,uint8_t index,uint8_t data)
{
	struct i2c_adapter *adap=parameter->client->adapter;
	struct i2c_msg msg;
	int32_t ret;
      
	int32_t scl_rate = num_100 * num_1000;
	
	char tx_buf[num_3];
	tx_buf[num_0] = index;
	tx_buf[num_1] = data;
        
   #if 0
	msg.addr = addr;
	msg.flags = parameter->client->flags;
	msg.len = num_2;
	msg.buf = (char *)tx_buf;
	msg.scl_rate = scl_rate;

	if(oz8806_pec_check)
	{
		msg.len = num_3;
		tx_buf[num_2] = num_0;
		tx_buf[num_2] =pec_calculate(tx_buf[num_2],(addr<<num_1));
		tx_buf[num_2] =pec_calculate(tx_buf[num_2],index);
		tx_buf[num_2] =pec_calculate(tx_buf[num_2],(uint8_t)data);
	}

	ret = i2c_transfer(adap, &msg, num_1);
	return (ret == num_1) ? num_0 : ret;
	#endif

}




/*****************************************************************************
 * Description:
 *		oz8806_register_store
 * Parameters:
 * 		read example: echo 88 > ftstprwreg ---read register 0x88
 * 		write example:echo 8807 > ftstprwreg ---write 0x07 into register 0x88
 * Return:
 *		
 * Note:
 *		
 *****************************************************************************/

static ssize_t oz8806_register_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t _count)
{
	char data[num_3];
	uint8_t address;
	uint8_t value;
	char *endp; 
	int len;

	printk(" oz8806 register storre\n");
	//printk("%s \n",buf);
	
	data[num_0] = buf[num_0];
 	data[num_1] =buf[num_1];
 	data[num_2] =num_0;
	//printk(" data is %s \n",data);


	address = simple_strtoul(data, &endp, 16); 

  	//printk("address   is %x \n",address);

	//printk("--------------------------- \n");


    len = strlen(buf);

	//printk("len is %d \n",len);
	if(len > num_3)
	{
		printk("write data \n");
        data[num_0] = buf[num_2];
        data[num_1] =buf[num_3];
        data[num_2] =num_0;

        value = simple_strtoul(data, &endp, 16); 
		
        afe_register_write_byte(address,value);
        printk("value is %x \n",value);
        return _count;
	}
	else
	{
		printk("read data \n");
		afe_register_read_byte(address,&value);
		printk("value is %x \n",value);
		return value;	

	}
}


/*
	example:cat register
*/
static ssize_t oz8806_register_show(struct device *dev, struct device_attribute *attr,char *buf)
{	
   	int yy = 3850;
	
	printk(" tp_version_show \n");
	printk(" wo zhong yu cheng gong le  \n");
	//*buf = 0xaa;
	return sprintf(buf,"%d\n",yy);
}


/*****************************************************************************
 * Description:
 *		oz8806_register_store
 * Parameters:
 *		write example: echo num_1 > debug ---open debug
 * Return:
 *		
 * Note:
 *		
 *****************************************************************************/
static ssize_t oz8806_debug_ctrl(struct device *dev, struct device_attribute *attr, const char *buf, size_t _count)
{

    char data[num_3];
	uint8_t debug;
	char *endp; 
	
	printk(" debug_store\n");
	
	data[num_0] = buf[num_0];
 	data[num_1] =buf[num_1];
 	data[num_2] =num_0;
	printk(" data is %s \n",data);
	
	debug = simple_strtoul(data, &endp, 16); 
	
	if(debug)
	{
		parameter->config->debug = num_1;
		printk("DEBUG ON \n");
	}
	else
	{
		parameter->config->debug = num_0;
		printk("DEBUG CLOSE \n");
	}

	return _count;
}



//__ATTR(tp_version, S_IRUGO | S_IWUSR, tp_version_show, tp_version_store);
static DEVICE_ATTR(register, S_IRUGO | S_IWUSR, oz8806_register_show, oz8806_register_store);
static DEVICE_ATTR(debug, S_IRUGO | S_IWUGO, NULL, oz8806_debug_ctrl);


static struct attribute *oz8806_attributes[] = {
	&dev_attr_register.attr,
	&dev_attr_debug.attr,
	NULL,
};

static struct attribute_group oz8806_attribute_group = {
	.attrs = oz8806_attributes
};

static struct kobject *oz8806;

static int oz8806_create_sys(void)
{
	int err;
	printk(" oz8806_create_sysfs\n");
	
	oz8806 = kobject_create_and_add("oz8806", kernel_kobj);  

	err = sysfs_create_group(oz8806, &oz8806_attribute_group);
	if (num_0 != err) 
	{
		printk(" creat oz8806 sysfs group fail(\n");
		sysfs_remove_group(&oz8806, &oz8806_attribute_group);
		return -EIO;
	} 
	else 
	{
		printk(" creat oz8806 sysfs group ok \n");	
     	
		
	}
	return err;
}





