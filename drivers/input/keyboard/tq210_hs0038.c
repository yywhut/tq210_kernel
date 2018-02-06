/*
  *  hs0038 driver for tq210
  *
  *  Copyright (c) 2011 www.embedsky.net
  *
  *
   *  This program is free software; you can redistribute it and/or modify
  *  it under the terms of the GNU General Public License version 2 as
  *  published by the Free Software Foundation.
  *
  */

#include <linux/module.h>

#include <linux/init.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/sched.h>
#include <linux/pm.h>
#include <linux/slab.h>
#include <linux/sysctl.h>
#include <linux/proc_fs.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/gpio_keys.h>
#include <linux/workqueue.h>
#include <linux/gpio.h>
#include <asm/io.h>
#include <mach/map.h>
#include <asm/irq.h>
#include <mach/regs-clock.h>
//#include <mach/regs-mem.h>
#include <mach/gpio.h>
#include <mach/gpio-smdkc110.h>
#include <mach/regs-gpio.h>

//#define KEY_VENTER		232
#define KEY_VENTER		28
/*tq210_hs0038_keycode必须包含所有要用到的系统键值*/
static const unsigned int tq210_hs0038_keycode[]={
	139/*MENU*/,105/*LEFT*/,KEY_VENTER/*DPAD_CENTER*/,/*KEY_9*/4,
	103/*UP*/,  158/*HOME*/,108/*DOWN*/, /*KEY_6*/12,
	102/*BACK*/, 106/*RIGHT*/ ,KEY_3, KEY_BACKSLASH,
	KEY_1,KEY_2,KEY_4,
	KEY_5,KEY_6,KEY_7,KEY_8,
	KEY_9,KEY_0,231,/*CALL*/
	15,	/*TAB*/		57,	/*SPACE*/	KEY_0,
	107,	/*ENDCALL*/	14,	/*DEL*/		231,	/*CALL*/	
	106,/*RIGHT*/	116,/*POWER*/	108,/*DOWN*/	212,	/*CAMERA*/	
	165,/*M_PREV*/	200,/*M_PLAY*/	163,	/*M_NEXT*/
	166,/*M_STOP*/	115,/*VOLUME+*/	114,	/*VOLUME-*/
};

static const unsigned int tq210_hs0038_keycode_807F[]={
	0,/*NULL*/0,/*NULL*//*0~1*/
	KEY_3,KEY_2,KEY_1,	/*4~2*/
	KEY_4,KEY_5,KEY_6,	/*5~7*/
	KEY_9,KEY_8,KEY_7,	/*a~8*/
	15,/*TAB*/	57,	/*SPACE*/	KEY_0,	/*b~d*/
	107,/*ENDCALL*/	14,	/*DEL*/		231,	/*CALL*/ 	/*10~e*/
	139,/*MENU*/	103,/*UP*/		158,	/*BACK*/	/*11~13*/
	106,/*RIGHT*/	KEY_VENTER,/*DPAD_CENTER*/	105,/*LEFT*/	/*16~14*/
	102,/*HOME*/	108,/*DOWN*/	116,	/*POWER*/	/*17~19*/
	165,/*M_PREV*/	200,/*M_PLAY*/	163,	/*M_NEXT*/		
	166,/*M_STOP*/	115,/*VOLUME+*/	114,	/*VOLUME-*/

};
static const unsigned int tq210_hs0038_keycode_00FF[32]={
	231,/*CALL*/0,/*NULL*/0,/*NULL*/0,/*NULL*/
	139,/*MENU*/103,/*UP*/158,/*BACK*/102,/*HOME*/
	105,/*LEFT*/KEY_VENTER,/*DPAD_CENTER*/106,/*RIGHT*/0,/*NULL*/
	139,/*MENU*/108,/*DOWN*/0,/*NULL*/0,/*NULL*/
	0,/*NULL*/0,/*NULL*/0,/*NULL*/0,/*NULL*/
	KEY_1,KEY_2,KEY_3,KEY_4,
	KEY_5,KEY_6,KEY_7,KEY_8,
	KEY_9,KEY_0,139,139,
};
static const unsigned int tq210_keycode_00FF[32]={
	0x10,0x03,0x01,0x06,
	0x09,0x1d,0x1f,0x0d,
	0x19,0x1b,0x11,0x15,
	0x17,0x12,0x16,0x4d,
	0x40,0x4c,0x04,0x00,
	0x0a,0x1e,0x0e,0x1a,
	0x1c,0x14,0x0f,0x0c,
	0x02,0x48,0x54,0x05
};
struct tq210_hs0038
{
	unsigned int keycode[ARRAY_SIZE(tq210_hs0038_keycode)];
	struct input_dev *input;
	spinlock_t lock;
	struct gpio_keys_button *button;
};
static unsigned int recvcode_to_keycode(unsigned int code)
{
	int i;
	for(i=0 ; i<sizeof(tq210_keycode_00FF)/sizeof(tq210_keycode_00FF[0]) ; i++)
	{
		if(code == tq210_keycode_00FF[i])
		{
			if(sizeof(tq210_hs0038_keycode_00FF)/sizeof(tq210_hs0038_keycode_00FF[0]) >= i)
			return  tq210_hs0038_keycode_00FF[i];
		}
	}
	return 0xFFFF;
}

static u32 __inline__ serial_data_read_byte(void)
{
	
	int j;
	u32 CodeTemp=0;
	
	for(j=1;j<=8;j++)      //每个字节8个bit的判断
	{	
		
		while(((gpio_get_value(S5PV210_GPH0(6))) & 1) ==0 );       //等待上升沿
		udelay(900);
		if(((gpio_get_value(S5PV210_GPH0(6))) & 1) ==1 )
		{	
			udelay(900);
			CodeTemp=CodeTemp|0x80;
			if(j<8) CodeTemp=CodeTemp>>1;	
		}
		else
		if(j<8)CodeTemp=CodeTemp>>1;//如果是"0",则向右移一位，自动补"0"
 	}
  	
	return CodeTemp;
	
}

static int tq210_hs0038_read(struct tq210_hs0038 *hs0038_data)
{

	u32 values[4];
	int i=0;
	struct gpio_keys_button *button = hs0038_data->button;
	
	gpio_direction_input(button->gpio);

	while(((gpio_get_value(S5PV210_GPH0(6))) & 1)==0);//在9ms内判断IO口的值

	while(((gpio_get_value(S5PV210_GPH0(6))) & 1)==1)
	{
		udelay(900);
		if(i>4)
			return -1;
		i++;
	}

	values[0] = serial_data_read_byte();
	values[1] = serial_data_read_byte();
	values[2] = serial_data_read_byte();
	values[3] = serial_data_read_byte();

/*	for(i=0;i<sizeof(values)/sizeof(values[0]);i++)
	{
		printk("values[%d]=0x%x\n", i,values[i]);		
	}*/

	if(values[0]==15 && values[1]==255)
	{
		i=values[2];
		input_report_key(hs0038_data->input,hs0038_data->keycode[i], 1);
		input_report_key(hs0038_data->input,hs0038_data->keycode[i], 0);
		input_sync(hs0038_data->input);
#ifdef CONFIG_tq210_DEBUG_HS0038
		printk("\n i =%d\n", i);			
		printk("\n keycode =%d\n", hs0038_data->keycode[i]);
#endif
	return 0;			
	}
	else if(values[0]==0x0 && values[1]==0xff)
	{
		i=values[2];
		input_report_key(hs0038_data->input,recvcode_to_keycode(i), 1);
		input_report_key(hs0038_data->input,recvcode_to_keycode(i), 0);
		input_sync(hs0038_data->input);
		return 0;			
	}
	else if(values[0]==0x80 && values[1]==0x7f)
	{
		i=values[2];
		input_report_key(hs0038_data->input,tq210_hs0038_keycode_807F[i], 1);
		input_report_key(hs0038_data->input,tq210_hs0038_keycode_807F[i], 0);
		input_sync(hs0038_data->input);

		return 0;			
	}
	else
	return -1;
		

}
static irqreturn_t tq210_hs0038_interrupt(int irq, void *dev_id)
{
	struct tq210_hs0038 *hs0038 = dev_id;
	unsigned long irqflags = IRQF_TRIGGER_FALLING;

	disable_irq_nosync(irq);
	tq210_hs0038_read(hs0038);
	irq_set_irq_type(irq, irqflags);
	enable_irq(irq);
	return IRQ_HANDLED;
 }


static int __devinit hs0038_setup_key(struct platform_device *pdev)
{
	struct tq210_hs0038 *hs0038 = platform_get_drvdata(pdev);
	struct gpio_keys_button *button=hs0038->button;
	const char *desc = button->desc ? button->desc : "hs0038";
	unsigned long irqflags;
	int irq, error;
	error = gpio_request(button->gpio, NULL);
	if (error < 0) {
		printk("failed to request GPIO %d, error %d\n",button->gpio, error);
		goto fail2;
	}

	error = gpio_direction_input(button->gpio);
	if (error < 0) {
		printk("failed to configure direction for GPIO %d , error %d\n",button->gpio,error);
		goto fail3;
	}

	irq = gpio_to_irq(button->gpio);
	if (irq < 0) {
		error = irq;
		printk("Unable to get irq number for GPIO %d,error %d\n",button->gpio,error);
		goto fail3;
	}

	irqflags = IRQF_TRIGGER_FALLING;

	error = request_irq(irq, tq210_hs0038_interrupt, irqflags, desc, hs0038);
	if (error) {
		printk("Unable to claim irq %d; error %d\n",irq, error);
		goto fail3;
	}
	return 0;

fail3:
	gpio_free(button->gpio);
fail2:
	return error;
}


static int __devinit tq210_hs0038_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct gpio_keys_platform_data *pdata = pdev->dev.platform_data;
	struct device *dev = &pdev->dev;
	struct input_dev *input;
	struct tq210_hs0038 *hs0038;
	int i=0;

	hs0038= kzalloc(sizeof(struct tq210_hs0038),GFP_KERNEL);

	input = input_allocate_device();
	if (!input || !hs0038)
	{
		dev_err(dev, "failed to allocate state\n");
		ret= -ENOMEM;
		goto err_free_mem;
	}


	spin_lock_init(&hs0038->lock);

	hs0038->input = input;

	platform_set_drvdata(pdev, hs0038);
	
	input->name = "tq210_hs0038";
	input->phys = "tq210_hs0038/input1";

	input->id.bustype = BUS_HOST;
	input->id.vendor = 0xabce;
	input->id.product = 0xecba;
	input->id.version = 0x0100;

	input->evbit[0] = BIT(EV_KEY);
	input->keycode = hs0038->keycode;
	input->keycodesize = sizeof(unsigned int);
	input->keycodemax = ARRAY_SIZE(tq210_hs0038_keycode);
	set_bit(EV_KEY, input->evbit);


	for (i = 0; i < ARRAY_SIZE(tq210_hs0038_keycode); i++){
		hs0038->keycode[i]= tq210_hs0038_keycode[i];
		set_bit(hs0038->keycode[i], input->keybit);
	}

	hs0038->button = &pdata->buttons[0];
	ret=hs0038_setup_key(pdev);
	if(ret)
		goto err_gpio_free;
	ret = input_register_device(input);
	if (ret < 0){
		printk(KERN_ERR "tq210_hs0038.c: input_register_device() return %d!\n", ret);
		goto  err_input_free;
	}
	printk("Input: S3C GPIO  hs0038 Registered\n");
	return 0;

err_input_free: //free input device
	input_free_device(input);
err_gpio_free: // free gpio
	gpio_free(pdata->buttons[0].gpio);
err_free_mem:
	kfree(input);
	kfree(hs0038);
	return ret;
}

static int __devexit tq210_hs0038_remove(struct platform_device *pdev)
{
	struct tq210_hs0038 *s3c_hs0038 = platform_get_drvdata(pdev);
	struct gpio_keys_platform_data *pdata = pdev->dev.platform_data;

	int i=0;

	for (i = 0; i < pdata->nbuttons; i++) {
		int irq = gpio_to_irq(pdata->buttons[i].gpio);
		free_irq(irq, &pdata->buttons[i]);
		gpio_free(pdata->buttons[i].gpio);
	}
	input_unregister_device(s3c_hs0038->input);

	return 0;
}

#ifdef CONFIG_PM
static int tq210_hs0038_suspend(struct device *dev)
{

	struct platform_device *pdev = to_platform_device(dev);
	struct tq210_hs0038 *s3c_hs0038 = platform_get_drvdata(pdev);
	s3c_gpio_cfgpin(s3c_hs0038->button->gpio, S3C_GPIO_SFN(0));

///	s3c_gpio_cfgpin(S3C64XX_GPL(9), S3C_GPIO_SFN(0));
	return 0;
}

static int tq210_hs0038_resume(struct device *dev)
{

	struct platform_device *pdev = to_platform_device(dev);
	struct tq210_hs0038 *s3c_hs0038 = platform_get_drvdata(pdev);
	s3c_gpio_cfgpin(s3c_hs0038->button->gpio, S3C_GPIO_SFN(3));

	//s3c_gpio_cfgpin(S3C64XX_GPL(9), S3C_GPIO_SFN(3));
	return 0;
}
static const struct dev_pm_ops tq210_hs0038_pm_ops = {
	.suspend	= tq210_hs0038_suspend,
	.resume		= tq210_hs0038_resume,
};
#else
#define  tq210_hs0038_suspend NULL
#define  tq210_hs0038_resume NULL
#endif
static struct platform_driver s3c_hs0038_driver = {
	.probe		= tq210_hs0038_probe,
	.remove		= __devexit_p(tq210_hs0038_remove),
	.driver		= {
		.name   = "hs0038",
		.owner	= THIS_MODULE,
#ifdef CONFIG_PM
		.pm	= &tq210_hs0038_pm_ops,
#endif
	},
};


static int __init tq210_hs0038_init(void)
{
	return platform_driver_register(&s3c_hs0038_driver);
}

static void __exit tq210_hs0038_exit(void)
{
	platform_driver_unregister(&s3c_hs0038_driver);
}

module_init(tq210_hs0038_init);
module_exit(tq210_hs0038_exit);

MODULE_AUTHOR("www.embedsky.net");
MODULE_DESCRIPTION("tq210 GPIO Keyboard Driver");
MODULE_LICENSE("GPL");