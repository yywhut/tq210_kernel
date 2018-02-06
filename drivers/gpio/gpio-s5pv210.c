/* linux/arch/arm/mach-s5pv210/gpiolib.c
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * S5PV210 - GPIOlib support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <plat/gpio-core.h>
#include <plat/gpio-cfg.h>
#include <plat/gpio-cfg-helpers.h>
#include <mach/map.h>

static struct s3c_gpio_cfg gpio_cfg = {
	.set_config	= s3c_gpio_setcfg_s3c64xx_4bit,
	.set_pull	= s3c_gpio_setpull_updown,
	.get_pull	= s3c_gpio_getpull_updown,
};

static struct s3c_gpio_cfg gpio_cfg_noint = {
	.set_config	= s3c_gpio_setcfg_s3c64xx_4bit,
	.set_pull	= s3c_gpio_setpull_updown,
	.get_pull	= s3c_gpio_getpull_updown,
};

/* GPIO bank's base address given the index of the bank in the
 * list of all gpio banks.
 */
#define S5PV210_BANK_BASE(bank_nr)	(S5P_VA_GPIO + ((bank_nr) * 0x20))

/*
 * Following are the gpio banks in v210.
 *
 * The 'config' member when left to NULL, is initialized to the default
 * structure gpio_cfg in the init function below.
 *
 * The 'base' member is also initialized in the init function below.
 * Note: The initialization of 'base' member of s3c_gpio_chip structure
 * uses the above macro and depends on the banks being listed in order here.
 */
static struct s3c_gpio_chip s5pv210_gpio_4bit[] = {
	{
		.chip	= {
			.base	= S5PV210_GPA0(0),    //0
			.ngpio	= S5PV210_GPIO_A0_NR,  // 这个组有多少个gpio
			.label	= "GPA0",
		},
	}, {
		.chip	= {
			.base	= S5PV210_GPA1(0),   //  8  这个组的 base的 number，用来算偏移的
			.ngpio	= S5PV210_GPIO_A1_NR,
			.label	= "GPA1",
		},
	}, {
		.chip	= {
			.base	= S5PV210_GPB(0),
			.ngpio	= S5PV210_GPIO_B_NR,
			.label	= "GPB",
		},
	}, {
		.chip	= {
			.base	= S5PV210_GPC0(0),
			.ngpio	= S5PV210_GPIO_C0_NR,
			.label	= "GPC0",
		},
	}, {
		.chip	= {
			.base	= S5PV210_GPC1(0),
			.ngpio	= S5PV210_GPIO_C1_NR,
			.label	= "GPC1",
		},
	}, {
		.chip	= {
			.base	= S5PV210_GPD0(0),
			.ngpio	= S5PV210_GPIO_D0_NR,
			.label	= "GPD0",
		},
	}, {
		.chip	= {
			.base	= S5PV210_GPD1(0),
			.ngpio	= S5PV210_GPIO_D1_NR,
			.label	= "GPD1",
		},
	}, {
		.chip	= {
			.base	= S5PV210_GPE0(0),
			.ngpio	= S5PV210_GPIO_E0_NR,
			.label	= "GPE0",
		},
	}, {
		.chip	= {
			.base	= S5PV210_GPE1(0),
			.ngpio	= S5PV210_GPIO_E1_NR,
			.label	= "GPE1",
		},
	}, {
		.chip	= {
			.base	= S5PV210_GPF0(0),
			.ngpio	= S5PV210_GPIO_F0_NR,
			.label	= "GPF0",
		},
	}, {
		.chip	= {
			.base	= S5PV210_GPF1(0),
			.ngpio	= S5PV210_GPIO_F1_NR,
			.label	= "GPF1",
		},
	}, {
		.chip	= {
			.base	= S5PV210_GPF2(0),
			.ngpio	= S5PV210_GPIO_F2_NR,
			.label	= "GPF2",
		},
	}, {
		.chip	= {
			.base	= S5PV210_GPF3(0),
			.ngpio	= S5PV210_GPIO_F3_NR,
			.label	= "GPF3",
		},
	}, {
		.chip	= {
			.base	= S5PV210_GPG0(0),
			.ngpio	= S5PV210_GPIO_G0_NR,
			.label	= "GPG0",
		},
	}, {
		.chip	= {
			.base	= S5PV210_GPG1(0),
			.ngpio	= S5PV210_GPIO_G1_NR,
			.label	= "GPG1",
		},
	}, {
		.chip	= {
			.base	= S5PV210_GPG2(0),
			.ngpio	= S5PV210_GPIO_G2_NR,
			.label	= "GPG2",
		},
	}, {
		.chip	= {
			.base	= S5PV210_GPG3(0),
			.ngpio	= S5PV210_GPIO_G3_NR,
			.label	= "GPG3",
		},
	}, {
		.config	= &gpio_cfg_noint,
		.chip	= {
			.base	= S5PV210_GPI(0),
			.ngpio	= S5PV210_GPIO_I_NR,
			.label	= "GPI",
		},
	}, {
		.chip	= {
			.base	= S5PV210_GPJ0(0),
			.ngpio	= S5PV210_GPIO_J0_NR,
			.label	= "GPJ0",
		},
	}, {
		.chip	= {
			.base	= S5PV210_GPJ1(0),
			.ngpio	= S5PV210_GPIO_J1_NR,
			.label	= "GPJ1",
		},
	}, {
		.chip	= {
			.base	= S5PV210_GPJ2(0),
			.ngpio	= S5PV210_GPIO_J2_NR,
			.label	= "GPJ2",
		},
	}, {
		.chip	= {
			.base	= S5PV210_GPJ3(0),
			.ngpio	= S5PV210_GPIO_J3_NR,
			.label	= "GPJ3",
		},
	}, {
		.chip	= {
			.base	= S5PV210_GPJ4(0),
			.ngpio	= S5PV210_GPIO_J4_NR,
			.label	= "GPJ4",
		},
	}, {
		.config	= &gpio_cfg_noint,
		.chip	= {
			.base	= S5PV210_MP01(0),
			.ngpio	= S5PV210_GPIO_MP01_NR,
			.label	= "MP01",
		},
	}, {
		.config	= &gpio_cfg_noint,
		.chip	= {
			.base	= S5PV210_MP02(0),
			.ngpio	= S5PV210_GPIO_MP02_NR,
			.label	= "MP02",
		},
	}, {
		.config	= &gpio_cfg_noint,
		.chip	= {
			.base	= S5PV210_MP03(0),
			.ngpio	= S5PV210_GPIO_MP03_NR,
			.label	= "MP03",
		},
	}, {
		.config	= &gpio_cfg_noint,
		.chip	= {
			.base	= S5PV210_MP04(0),
			.ngpio	= S5PV210_GPIO_MP04_NR,
			.label	= "MP04",
		},
	}, {
		.config	= &gpio_cfg_noint,
		.chip	= {
			.base	= S5PV210_MP05(0),
			.ngpio	= S5PV210_GPIO_MP05_NR,
			.label	= "MP05",
		},
	}, {
		.base	= (S5P_VA_GPIO + 0xC00),
		.config	= &gpio_cfg_noint,
		.irq_base = IRQ_EINT(0),
		.chip	= {
			.base	= S5PV210_GPH0(0),
			.ngpio	= S5PV210_GPIO_H0_NR,
			.label	= "GPH0",
			.to_irq = samsung_gpiolib_to_irq,
		},
	}, {
		.base	= (S5P_VA_GPIO + 0xC20),
		.config	= &gpio_cfg_noint,
		.irq_base = IRQ_EINT(8),
		.chip	= {
			.base	= S5PV210_GPH1(0),
			.ngpio	= S5PV210_GPIO_H1_NR,
			.label	= "GPH1",
			.to_irq = samsung_gpiolib_to_irq,
		},
	}, {
		.base	= (S5P_VA_GPIO + 0xC40),
		.config	= &gpio_cfg_noint,
		.irq_base = IRQ_EINT(16),
		.chip	= {
			.base	= S5PV210_GPH2(0),
			.ngpio	= S5PV210_GPIO_H2_NR,
			.label	= "GPH2",
			.to_irq = samsung_gpiolib_to_irq,
		},
	}, {
		.base	= (S5P_VA_GPIO + 0xC60),
		.config	= &gpio_cfg_noint,
		.irq_base = IRQ_EINT(24),
		.chip	= {
			.base	= S5PV210_GPH3(0),
			.ngpio	= S5PV210_GPIO_H3_NR,
			.label	= "GPH3",
			.to_irq = samsung_gpiolib_to_irq,
		},
	},
};

__init int s5pv210_gpiolib_init(void)
{
	struct s3c_gpio_chip *chip = s5pv210_gpio_4bit;
	int nr_chips = ARRAY_SIZE(s5pv210_gpio_4bit);
	int gpioint_group = 0;
	int i = 0;

	// 如果在这里做 io remap会死掉
	// 这个函数并没有将GPH0 修改为中断引脚

void __iomem *reg = (S5P_VA_GPIO + 0xC00);
uint32_t con;	
	con = __raw_readl(reg);
	






	
	
	for (i = 0; i < nr_chips; i++, chip++) {
		if (chip->config == NULL) {
			chip->config = &gpio_cfg;
			chip->group = gpioint_group++;   // 从0开始，哪个组
		}
		if (chip->base == NULL)   //注意这个chip 是 s3c_gpio_chip，跟后面注册进去的不是一个
								//所以这个base 跟chip 中的base不是一个base
			chip->base = S5PV210_BANK_BASE(i);   //虚拟地址
	}

	//在这里注册 chip 数组，每个数组都有一个base，这个base 是这个bank的
	// 基地址，chip->base = S5PV210_BANK_BASE(i);
	//nr_chips 表示有多少组 chip，将gpio_desc 跟gpio_chip 相关联
	samsung_gpiolib_add_4bit_chips(s5pv210_gpio_4bit, nr_chips);


	
	s5p_register_gpioint_bank(IRQ_GPIOINT, 0, S5P_GPIOINT_GROUP_MAXNR);
	// 我觉得这个函数应该是空函数

	return 0;
}
/*
ARRAY_SIZE(s5pv210_gpio_4bit) is 32 
chip->group is 0 
chip->base fe500000 
chip->group is 1 
chip->base fe500020 
chip->group is 2 
chip->base fe500040 
chip->group is 3 
chip->base fe500060 
chip->group is 4 
chip->base fe500080 
chip->group is 5 
chip->base fe5000a0 
chip->group is 6 
chip->base fe5000c0 
chip->group is 7 
chip->base fe5000e0 
chip->group is 8 
chip->base fe500100 
chip->group is 9 
chip->base fe500120 
chip->group is 10 
chip->base fe500140 
chip->group is 11 
chip->base fe500160 
chip->group is 12 
chip->base fe500180 
chip->group is 13 
chip->base fe5001a0 
chip->group is 14 
chip->base fe5001c0 
chip->group is 15 
chip->base fe5001e0 
chip->group is 16 
chip->base fe500200 
chip->group is 0 
chip->base fe500220 
chip->group is 17 
chip->base fe500240 
chip->group is 18 
chip->base fe500260 
chip->group is 19 
chip->base fe500280 
chip->group is 20 
chip->base fe5002a0 
chip->group is 21 
chip->base fe5002c0 
chip->group is 0 
chip->base fe5002e0 
chip->group is 0 
chip->base fe500300 
chip->group is 0 
chip->base fe500320 
chip->group is 0 
chip->base fe500340 
chip->group is 0 
chip->base fe500360 
chip->group is 0 
chip->base fe500c00 
chip->group is 0 
chip->base fe500c20 
chip->group is 0 
chip->base fe500c40 
chip->group is 0 
chip->base fe500c60 


*/



//core_initcall(s5pv210_gpiolib_init);
