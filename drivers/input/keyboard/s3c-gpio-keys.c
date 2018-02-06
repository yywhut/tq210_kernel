/*
 * Driver for keys on GPIO lines capable of generating interrupts.
 *
 * Copyright 2005 Phil Blundell
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>

#include <linux/init.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/sched.h>
#include <linux/pm.h>
#include <linux/sysctl.h>
#include <linux/proc_fs.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/gpio_keys.h>


#include <mach/gpio.h>
#include <plat/gpio-cfg.h>

#include <mach/regs-gpio.h>

struct gpio_button_data {
	struct gpio_keys_button *button;
	struct input_dev *input;
	struct timer_list timer;
};

struct gpio_keys_drvdata {
	struct input_dev *input;
	struct gpio_button_data data[0];
};
struct s3c_gpio_key{
	int pin;
	int eint;
	int eintcfg;
	int inputcfg;
};
struct s3c_gpio_key s3c_gpio_keys[]=
{	
	{ 
		.pin = S5PV210_GPH0(0),
		.eintcfg = 0X0f<<0,    //这两个表明控制寄存器的位置     
		.inputcfg = 0<<0,
		.eint = IRQ_EINT0,
	},
	{ 
		.pin = S5PV210_GPH0(1),
		.eintcfg = 0X0f<<4,      
		.inputcfg = 0<<4,
		.eint = IRQ_EINT1,
	},
	{ //lhh add
		.pin = S5PV210_GPH0(2),
		.eintcfg = 0X0f<<8,      
		.inputcfg = 0<<8,
		.eint = IRQ_EINT2,
	},
	{ 
		.pin = S5PV210_GPH0(3), 
		.eintcfg = 0X0f<<12,      
		.inputcfg = 0<<12,		  
		.eint = IRQ_EINT3,
	},
	{ 
		.pin = S5PV210_GPH0(4),  
		.eintcfg = 0X0f<<16,      
		.inputcfg = 0<<16,			  
		.eint = IRQ_EINT4,
	},		
	{ 
		.pin = S5PV210_GPH0(5), 
		.eintcfg = 0X0f<<20,      
		.inputcfg = 0<<20,
		.eint = IRQ_EINT5,
	},
	{ 
		.pin = S5PV210_GPH2(6),  
		.eintcfg = 0X0f<<24,      
		.inputcfg = 0<<24,			  
		.eint = IRQ_EINT16_31,
	},		
	{ 
		.pin = S5PV210_GPH2(7), 
		.eintcfg = 0X0f<<28,      
		.inputcfg = 0<<28,
		.eint = IRQ_EINT16_31,
	},
};

static void gpio_keys_report_event(struct gpio_button_data *bdata)
{
	struct gpio_keys_button *button = bdata->button;
	struct input_dev *input = bdata->input;
	unsigned int type = button->type ?: EV_KEY;

	int state = (gpio_get_value(button->gpio) ? 1 : 0) ^ button->active_low;
	input_event(input, type, button->code, !!state);
	input_sync(input);
//	printk("%s:*****button->code: %d*******\n",__func__,button->code);
}

static void gpio_check_button(unsigned long _data)
{
	struct gpio_button_data *data = (struct gpio_button_data *)_data;

	gpio_keys_report_event(data);
}

static irqreturn_t gpio_keys_isr(int irq, void *dev_id)
{
	struct gpio_button_data *bdata = dev_id;
	struct gpio_keys_button *button = bdata->button;
	int i;
//	BUG_ON(irq != gpio_to_irq(button->gpio));

	//add by Figo
	//将所有的pin都设置成输入状态
	for(i=0;i<(sizeof(s3c_gpio_keys)/sizeof(struct s3c_gpio_key));i++)
	{
		s3c_gpio_cfgpin(s3c_gpio_keys[i].pin,s3c_gpio_keys[i].inputcfg);
	}
//	if(button->code == 102 || button->code == 106)
//		s3c_gpio_cfgpin(button->gpio,S3C_GPIO_INPUT);
//	else
//		s3c_gpio_cfgpin(button->gpio,0);
		
//	printk("The code is %d\n",button->code);


	printk("S5PV210_GPH0(0) is %d \n",S5PV210_GPH0(0)); 128

	 printk("irq is %d \n",irq);  32
	printk("The code is %d\n",button->code); 103
	printk("The gpio is %d\n",button->gpio);  128
	printk("The gpiofff is %d\n",gpio_to_irq(button->gpio)); 32

	printk("The s3c_gpio_keys.pin is %d\n",s3c_gpio_keys[0].pin);128

	printk("The gpio_get_value(button->gpio) is %d\n",gpio_get_value(button->gpio));0

	int state = (gpio_get_value(button->gpio) ? 1 : 0) ^ button->active_low; 

	printk("The state is %d\n",state);1


	if (button->debounce_interval)
		mod_timer(&bdata->timer,
			jiffies + msecs_to_jiffies(button->debounce_interval));
	else
		gpio_keys_report_event(bdata);

	//add by Figo
	//将所有的pin都设置成中断状态
	for(i=0;i<(sizeof(s3c_gpio_keys)/sizeof(struct  s3c_gpio_key));i++)
	{
		s3c_gpio_cfgpin(s3c_gpio_keys[i].pin,s3c_gpio_keys[i].eintcfg);
	}
//	if(button->code == 102 || button->code == 106)
//		s3c_gpio_cfgpin(button->gpio,S3C_GPIO_SFN(3));
//	else
//		s3c_gpio_cfgpin(button->gpio,2);

	return IRQ_HANDLED;
}

static int __devinit gpio_keys_probe(struct platform_device *pdev)
{
	//从device那边得到数据
	struct gpio_keys_platform_data *pdata = pdev->dev.platform_data;
	struct gpio_keys_drvdata *ddata;
	struct input_dev *input;
	int i, error,irq;
	int wakeup = 0;

	//分配一些空间来放按键数据
	ddata = kzalloc(sizeof(struct gpio_keys_drvdata) +
			pdata->nbuttons * sizeof(struct gpio_button_data),
			GFP_KERNEL);
	input = input_allocate_device();
	if (!ddata || !input) {
		error = -ENOMEM;
		goto fail1;
	}

//pdev 是平台总线设备，是全局可见的，所以这里产生的局部ddata 就放在了
// pdev->dev->p->driver_data
	platform_set_drvdata(pdev, ddata);

	input->name = pdev->name;
	input->phys = "gpio-keys/input0";
	input->dev.parent = &pdev->dev;

	input->id.bustype = BUS_HOST;
	input->id.vendor = 0x0001;
	input->id.product = 0x0001;
	input->id.version = 0x0100;

	ddata->input = input;

	for (i = 0; i < pdata->nbuttons; i++) {
		struct gpio_keys_button *button = &pdata->buttons[i];//最小单位
		struct gpio_button_data *bdata = &ddata->data[i];   //driver数据
		unsigned int type = button->type ?: EV_KEY;

		bdata->input = input;
		bdata->button = button;
	
		//void setup_timer( struct timer_list *timer,   
                    // void (*function)(unsigned long), unsigned long data ); 
		
		setup_timer(&bdata->timer,
			    gpio_check_button, (unsigned long)bdata);

		error = gpio_request(button->gpio, button->desc ?: "gpio_keys");

		if (error < 0) {
			pr_err("gpio-keys: failed to request GPIO %d,"
				" error %d\n", button->gpio, error);
			goto fail2;
		}

		//设定输入方向
		//看下这个gpio是不是就是个数字
		//前面5个号码是 128-133
		//后面两个是 152  153
		error = gpio_direction_input(button->gpio);
		if (error < 0) {
			pr_err("gpio-keys: failed to configure input"
				" direction for GPIO %d, error %d\n",
				button->gpio, error);
			gpio_free(button->gpio);
			goto fail2;
		}
#if 1
//		s3c_gpio_cfgpin(s3c_gpio_keys[i].pin,s3c_gpio_keys[i].eintcfg);


//后面两个才执行到这里
		if(IRQ_EINT16_31 == s3c_gpio_keys[i].eint)
		{
			irq = gpio_to_irq(button->gpio);
			//可能是两个io共享中断，所以用到这个
	
			error = request_irq(irq, gpio_keys_isr,
						IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING | IRQF_SHARED,
						/*button->desc ? button->desc : "gpio_keys",*/"MENU_TEST",
						bdata);
			if (error) {
				pr_err("gpio-keys: Unable to claim irq %d; error %d index %d\n",
					irq, error,i);
				gpio_free(button->gpio);
				goto fail2;
			}
		}
		else
		{
			// 这个函数将引脚寄存器转化为中断方式

			error = request_irq(s3c_gpio_keys[i].eint, gpio_keys_isr,
						IRQF_SAMPLE_RANDOM | IRQF_TRIGGER_RISING |
						IRQF_TRIGGER_FALLING,
						button->desc ? button->desc : "gpio_keys",
						bdata);
			if (error) {
				pr_err("gpio-keys: Unable to claim irq %d; error %d\n",
					irq, error);
				gpio_free(button->gpio);
				goto fail2;
			}

		}
		
#else
		error = request_irq(interrupt_group[i], gpio_keys_isr,
				    IRQF_SAMPLE_RANDOM | IRQF_TRIGGER_RISING |
					IRQF_TRIGGER_FALLING,
				    button->desc ? button->desc : "gpio_keys",
				    bdata);
		if (error) {
			pr_err("gpio-keys: Unable to claim irq %d; error %d;index %d\n",
				interrupt_group[i], error,i);
			gpio_free(button->gpio);
			goto fail2;
		}
#endif
		if (button->wakeup)
			wakeup = 1;

		input_set_capability(input, type, button->code);
	}

	error = input_register_device(input);
	if (error) {
		pr_err("gpio-keys: Unable to register input device, "
			"error: %d\n", error);
		goto fail2;
	}

	device_init_wakeup(&pdev->dev, wakeup);

	return 0;

 fail2:
	while (--i >= 0) {
		free_irq(gpio_to_irq(pdata->buttons[i].gpio), &ddata->data[i]);
		if (pdata->buttons[i].debounce_interval)
			del_timer_sync(&ddata->data[i].timer);
		gpio_free(pdata->buttons[i].gpio);
	}

	platform_set_drvdata(pdev, NULL);
 fail1:
	input_free_device(input);
	kfree(ddata);

	return error;
}

static int __devexit gpio_keys_remove(struct platform_device *pdev)
{
	struct gpio_keys_platform_data *pdata = pdev->dev.platform_data;
	struct gpio_keys_drvdata *ddata = platform_get_drvdata(pdev);
	struct input_dev *input = ddata->input;
	int i;

	device_init_wakeup(&pdev->dev, 0);

	for (i = 0; i < pdata->nbuttons; i++) {
		int irq = gpio_to_irq(pdata->buttons[i].gpio);
		free_irq(irq, &ddata->data[i]);
		if (pdata->buttons[i].debounce_interval)
			del_timer_sync(&ddata->data[i].timer);
		gpio_free(pdata->buttons[i].gpio);
	}

	input_unregister_device(input);

	return 0;
}


#ifdef CONFIG_PM
static int gpio_keys_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct gpio_keys_platform_data *pdata = pdev->dev.platform_data;
	int i;

	if (device_may_wakeup(&pdev->dev)) {

		for (i = 0; i < pdata->nbuttons; i++) {

			struct gpio_keys_button *button = &pdata->buttons[i];

			if (button->wakeup) {

				int irq = gpio_to_irq(button->gpio);

//				enable_irq_wake(irq);
				enable_irq_wake(s3c_gpio_keys[i].eint);

			}
		}
	}

	return 0;
}

static int gpio_keys_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct gpio_keys_platform_data *pdata = pdev->dev.platform_data;
	int i;

	if (device_may_wakeup(&pdev->dev)) {
		for (i = 0; i < pdata->nbuttons; i++) {
			struct gpio_keys_button *button = &pdata->buttons[i];
			if (button->wakeup) {
				int irq = gpio_to_irq(button->gpio);
	//			disable_irq_wake(irq);
				disable_irq_wake(s3c_gpio_keys[i].eint);

			}
		}
	}

	return 0;
}

static const struct dev_pm_ops gpio_keys_pm_ops = {
	.suspend	= gpio_keys_suspend,
	.resume		= gpio_keys_resume,
};
#endif

static struct platform_driver gpio_keys_device_driver = {
	.probe		= gpio_keys_probe,
	.remove		= __devexit_p(gpio_keys_remove),

	.driver		= {
		.name	= "gpio-keys",
		.owner	= THIS_MODULE,
#ifdef CONFIG_PM
		.pm	= &gpio_keys_pm_ops,
#endif
	}
	
};

static int __init gpio_keys_init(void)
{
	printk("gpio-keys init\n");
	return platform_driver_register(&gpio_keys_device_driver);
}

static void __exit gpio_keys_exit(void)
{
	platform_driver_unregister(&gpio_keys_device_driver);
}

module_init(gpio_keys_init);
module_exit(gpio_keys_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Phil Blundell <pb@handhelds.org>");
MODULE_DESCRIPTION("Keyboard driver for CPU GPIOs");
MODULE_ALIAS("platform:gpio-keys");


