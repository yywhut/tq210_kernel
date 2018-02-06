/* linux/arch/arm/mach-s5pv210/mach-tq210.c
 *
 * Copyright (c) 2012 EmbedSky Tech
 *		http://www.embedsky.net
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/i2c.h>
#include <linux/i2c-gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/regulator/fixed.h>
#include <linux/regulator/machine.h>
#include <linux/mfd/max8698.h>
#include <mach/power-domain.h>
#include <linux/init.h>
#include <linux/serial_core.h>
#include <linux/sysdev.h>
#ifdef CONFIG_DM9000
#include <linux/dm9000.h>
#endif
#include <linux/fb.h>
#include <linux/gpio.h>
#include <linux/videodev2.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/pwm_backlight.h>
#include <linux/usb/ch9.h>
#include <linux/spi/spi.h>
#include <linux/gpio_keys.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/setup.h>
#include <asm/mach-types.h>

#include <video/platform_lcd.h>

#include <mach/map.h>
#include <mach/regs-clock.h>
#include <mach/spi-clocks.h>
#include <mach/regs-fb.h>

#ifdef CONFIG_VIDEO_S5K4BA
#include <media/s5k4ba_platform.h>
#undef	CAM_ITU_CH_A
#define	CAM_ITU_CH_B
#endif
#include <plat/regs-serial.h>
#include <plat/regs-srom.h>
#include <plat/gpio-cfg.h>
#include <plat/s3c64xx-spi.h>
#include <plat/s5pv210.h>
#include <plat/devs.h>
#include <plat/cpu.h>
#include <plat/adc.h>
#include <plat/ts.h>
#include <plat/ata.h>
#include <plat/iic.h>
#include <plat/keypad.h>
#include <plat/pm.h>
#include <plat/fb.h>
#include <plat/mfc.h>
#include <plat/s5p-time.h>
#include <plat/sdhci.h>
#include <plat/fimc.h>
#include <plat/csis.h>
#include <plat/jpeg.h>
#include <plat/clock.h> 
#include <plat/regs-otg.h>
#include <plat/otg.h>
#include <plat/ehci.h>
#include <plat/ohci.h>
#include <../../../drivers/video/samsung/s3cfb.h>
#include <mach/regs-gpio.h>
#include <mach/gpio.h>
#ifdef CONFIG_ANDROID_PMEM
#include <linux/android_pmem.h>
#endif
#include <plat/media.h>
#include <mach/media.h>
#include <mach/gpio-smdkc110.h>
#include <mach/ts-s3c.h>
#ifdef CONFIG_MTD_NAND
#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/nand_ecc.h>
#include <linux/mtd/partitions.h>
#include <plat/nand.h>
#endif
#if defined(CONFIG_MFD_TPS65910)
#include <linux/mfd/tps65910.h>
#endif
#ifdef CONFIG_TOUCHSCREEN_EGALAX
#include <linux/i2c/egalax.h>
#define EETI_TS_DEV_NAME        "egalax_i2c"

//

static struct egalax_i2c_platform_data  egalax_platdata  = {
        .gpio_int = EGALAX_IRQ,
        .gpio_en = NULL,
        .gpio_rst = NULL,
};
#endif
/* Following are default values for UCON, ULCON and UFCON UART registers */
#define TQ210_UCON_DEFAULT	(S3C2410_UCON_TXILEVEL |	\
				 S3C2410_UCON_RXILEVEL |	\
				 S3C2410_UCON_TXIRQMODE |	\
				 S3C2410_UCON_RXIRQMODE |	\
				 S3C2410_UCON_RXFIFO_TOI |	\
				 S3C2443_UCON_RXERR_IRQEN)

#define TQ210_ULCON_DEFAULT	S3C2410_LCON_CS8

#define TQ210_UFCON_DEFAULT	(S3C2410_UFCON_FIFOMODE |	\
				 S5PV210_UFCON_TXTRIG4 |	\
				 S5PV210_UFCON_RXTRIG4)

static struct s3c2410_uartcfg tq210_uartcfgs[] __initdata = {
	[0] = {
		.hwport		= 0,
		.flags		= 0,
		.ucon		= TQ210_UCON_DEFAULT,
		.ulcon		= TQ210_ULCON_DEFAULT,
		.ufcon		= TQ210_UFCON_DEFAULT,
	},
	[1] = {
		.hwport		= 1,
		.flags		= 0,
		.ucon		= TQ210_UCON_DEFAULT,
		.ulcon		= TQ210_ULCON_DEFAULT,
		.ufcon		= TQ210_UFCON_DEFAULT,
	},
	[2] = {
		.hwport		= 2,
		.flags		= 0,
		.ucon		= TQ210_UCON_DEFAULT,
		.ulcon		= TQ210_ULCON_DEFAULT,
		.ufcon		= TQ210_UFCON_DEFAULT,
	},
	[3] = {
		.hwport		= 3,
		.flags		= 0,
		.ucon		= TQ210_UCON_DEFAULT,
		.ulcon		= TQ210_ULCON_DEFAULT,
		.ufcon		= TQ210_UFCON_DEFAULT,
	},
};

#if defined(CONFIG_REGULATOR_MAX8698)
/* LDO */
static struct regulator_consumer_supply tq210_ldo3_consumer[] = {
	REGULATOR_SUPPLY("pd_io", "s3c-usbgadget"),
	REGULATOR_SUPPLY("pd_io", "s5p-ohci"),
	REGULATOR_SUPPLY("pd_io", "s5p-ehci"),
};

static struct regulator_consumer_supply tq210_ldo5_consumer[] = {
	REGULATOR_SUPPLY("AVDD", "0-001b"),
	REGULATOR_SUPPLY("DVDD", "0-001b"),
};

static struct regulator_consumer_supply tq210_ldo8_consumer[] = {
	REGULATOR_SUPPLY("pd_core", "s3c-usbgadget"),
	REGULATOR_SUPPLY("pd_core", "s5p-ohci"),
	REGULATOR_SUPPLY("pd_core", "s5p-ehci"),
};

static struct regulator_init_data tq210_ldo2_data = {
	.constraints	= {
		.name		= "VALIVE_1.1V",
		.min_uV		= 1100000,
		.max_uV		= 1100000,
		.apply_uV	= 1,
		.always_on	= 1,
		.state_mem	= {
			.enabled = 1,
		},
	},
};

static struct regulator_init_data tq210_ldo3_data = {
	.constraints	= {
		.name		= "VUOTG_D+VUHOST_D_1.1V",
		.min_uV		= 1100000,
		.max_uV		= 1100000,
		.apply_uV	= 1,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		.state_mem	= {
			.disabled = 1,
		},
	},
	.num_consumer_supplies	= ARRAY_SIZE(tq210_ldo3_consumer),
	.consumer_supplies	= tq210_ldo3_consumer,
};

static struct regulator_init_data tq210_ldo4_data = {
	.constraints	= {
		.name		= "V_MIPI_1.8V",
		.min_uV		= 1800000,
		.max_uV		= 1800000,
		.apply_uV	= 1,
		.always_on	= 1,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		.state_mem	= {
			.disabled = 1,
		},
	},
};

static struct regulator_init_data tq210_ldo5_data = {
	.constraints	= {
		.name		= "VMMC+VEXT_2.8V",
		.min_uV		= 2800000,
		.max_uV		= 2800000,
		.apply_uV	= 1,
		.always_on	= 1,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		.state_mem	= {
			.enabled = 1,
		},
	},
	.num_consumer_supplies	= ARRAY_SIZE(tq210_ldo5_consumer),
	.consumer_supplies	= tq210_ldo5_consumer,
};

static struct regulator_init_data tq210_ldo6_data = {
	.constraints	= {
		.name		= "VCC_2.6V",
		.min_uV		= 2600000,
		.max_uV		= 2600000,
		.apply_uV	= 1,
		.always_on	= 1,
		.valid_ops_mask	= REGULATOR_CHANGE_STATUS,
		.state_mem	 = {
			.disabled = 1,
		},
	},
};

static struct regulator_init_data tq210_ldo7_data = {
	.constraints	= {
		.name		= "VDAC_2.8V",
		.min_uV		= 2800000,
		.max_uV		= 2800000,
		.apply_uV	= 1,
		.valid_ops_mask	= REGULATOR_CHANGE_STATUS,
		.state_mem	= {
			.enabled = 1,
		},
	},
};

static struct regulator_init_data tq210_ldo8_data = {
	.constraints	= {
		.name		= "VUOTG_A+VUHOST_A_3.3V",
		.min_uV		= 3300000,
		.max_uV		= 3300000,
		.apply_uV	= 1,
		.valid_ops_mask	= REGULATOR_CHANGE_STATUS,
		.state_mem	= {
			.disabled = 1,
		},
	},
	.num_consumer_supplies	= ARRAY_SIZE(tq210_ldo8_consumer),
	.consumer_supplies	= tq210_ldo8_consumer,
};

static struct regulator_init_data tq210_ldo9_data = {
	.constraints	= {
		.name		= "VADC+VSYS+VKEY_2.8V",
		.min_uV		= 2800000,
		.max_uV		= 2800000,
		.apply_uV	= 1,
		.always_on	= 1,
		.state_mem	= {
			.enabled = 1,
		},
	},
};

/* BUCK */
static struct regulator_consumer_supply tq210_buck1_consumer =
	REGULATOR_SUPPLY("vddarm", NULL);

static struct regulator_consumer_supply tq210_buck2_consumer =
	REGULATOR_SUPPLY("vddint", NULL);

static struct regulator_init_data tq210_buck1_data = {
	.constraints	= {
		.name		= "VCC_ARM",
		.min_uV		= 750000,
		.max_uV		= 1500000,
		.apply_uV	= 1,
		.valid_ops_mask	= REGULATOR_CHANGE_VOLTAGE |
				  REGULATOR_CHANGE_STATUS,
		.state_mem	= {
			.uV	= 1250000,
			.mode	= REGULATOR_MODE_NORMAL,
			.disabled = 1,
		},
	},
	.num_consumer_supplies	= 1,
	.consumer_supplies	= &tq210_buck1_consumer,
};

static struct regulator_init_data tq210_buck2_data = {
	.constraints	= {
		.name		= "VCC_INTERNAL",
		.min_uV		= 950000,
		.max_uV		= 1200000,
		.valid_ops_mask	= REGULATOR_CHANGE_VOLTAGE |
				  REGULATOR_CHANGE_STATUS,
		.state_mem	= {
			.uV	= 1100000,
			.mode	= REGULATOR_MODE_NORMAL,
			.disabled = 1,
		},
	},
	.num_consumer_supplies	= 1,
	.consumer_supplies	= &tq210_buck2_consumer,
};

static struct regulator_init_data tq210_buck3_data = {
	.constraints	= {
		.name		= "VCC_MEM",
		.min_uV		= 1800000,
		.max_uV		= 1800000,
		.always_on	= 1,
		.apply_uV	= 1,
		.state_mem	= {
			.uV	= 1800000,
			.mode	= REGULATOR_MODE_NORMAL,
			.enabled = 1,
		},
	},
};

static struct max8698_regulator_data tq210_regulators[] = {
	{ MAX8698_LDO2,  &tq210_ldo2_data },
	{ MAX8698_LDO3,  &tq210_ldo3_data },
	{ MAX8698_LDO4,  &tq210_ldo4_data },
	{ MAX8698_LDO5,  &tq210_ldo5_data },
	{ MAX8698_LDO6,  &tq210_ldo6_data },
	{ MAX8698_LDO7,  &tq210_ldo7_data },
	{ MAX8698_LDO8,  &tq210_ldo8_data },
	{ MAX8698_LDO9,  &tq210_ldo9_data },
	{ MAX8698_BUCK1, &tq210_buck1_data },
	{ MAX8698_BUCK2, &tq210_buck2_data },
	{ MAX8698_BUCK3, &tq210_buck3_data },
};

static struct max8698_platform_data tq210_max8698_pdata = {
	.num_regulators = ARRAY_SIZE(tq210_regulators),
	.regulators     = tq210_regulators,

	/* 1GHz default voltage */
	.dvsarm1        = 0xa,  /* 1.25v */
	.dvsarm2        = 0x9,  /* 1.20V */
	.dvsarm3        = 0x6,  /* 1.05V */
	.dvsarm4        = 0x4,  /* 0.95V */
	.dvsint1        = 0x7,  /* 1.10v */
	.dvsint2        = 0x5,  /* 1.00V */

	.set1       = S5PV210_GPH1(6),
	.set2       = S5PV210_GPH1(7),
	.set3       = S5PV210_GPH0(4),
};
#endif

static struct s3c_ide_platdata tq210_ide_pdata __initdata = {
	.setup_gpio	= s5pv210_ide_setup_gpio,
};

static uint32_t tq210_keymap[] __initdata = {
	/* KEY(row, col, keycode) */
	KEY(0, 3, KEY_1), KEY(0, 4, KEY_2), KEY(0, 5, KEY_3),
	KEY(0, 6, KEY_4), KEY(0, 7, KEY_5),
	KEY(1, 3, KEY_A), KEY(1, 4, KEY_B), KEY(1, 5, KEY_C),
	KEY(1, 6, KEY_D), KEY(1, 7, KEY_E), KEY(7, 1, KEY_LEFTBRACE)
};

static struct matrix_keymap_data tq210_keymap_data __initdata = {
	.keymap		= tq210_keymap,
	.keymap_size	= ARRAY_SIZE(tq210_keymap),
};

static struct samsung_keypad_platdata tq210_keypad_data __initdata = {
	.keymap_data	= &tq210_keymap_data,
	.rows		= 8,
	.cols		= 8,
};

#ifdef CONFIG_DM9000
static struct resource tq210_dm9000_resources[] = {
	[0] = {
		.start	= S5PV210_PA_SROM_BANK1 + 0x300,
		.end	= S5PV210_PA_SROM_BANK1 + 0x300,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= S5PV210_PA_SROM_BANK1 + 0x300 + 4,
		.end	= S5PV210_PA_SROM_BANK1 + 0x300 + 4,
		.flags	= IORESOURCE_MEM,
	},
	[2] = {
		.start	= IRQ_EINT(10),
		.end	= IRQ_EINT(10),
		.flags	= IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHLEVEL,
	},
};
static struct dm9000_plat_data tq210_dm9000_platdata = {
	.flags		= DM9000_PLATF_16BITONLY | DM9000_PLATF_NO_EEPROM,
	.dev_addr	= { 0x00, 0x09, 0xc2, 0xff, 0xec, 0x68 },
};

struct platform_device tq210_dm9000 = {
	.name		= "dm9000",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(tq210_dm9000_resources),
	.resource	= tq210_dm9000_resources,
	.dev		= {
		.platform_data	= &tq210_dm9000_platdata,
	},
};

#endif /* CONFIG_DM9000 */

#ifdef CONFIG_REGULATOR
static struct regulator_consumer_supply tq210_b_pwr_5v_consumers[] = {
        {
                /* WM8580 */
                .supply         = "PVDD",
                .dev_name       = "0-001b",
        },
};

static struct regulator_init_data tq210_b_pwr_5v_data = {
        .constraints = {
                .always_on = 1,
        },
        .num_consumer_supplies  = ARRAY_SIZE(tq210_b_pwr_5v_consumers),
        .consumer_supplies      = tq210_b_pwr_5v_consumers,
};

static struct fixed_voltage_config tq210_b_pwr_5v_pdata = {
        .supply_name    = "B_PWR_5V",
        .microvolts     = 5000000,
        .init_data      = &tq210_b_pwr_5v_data,
};

static struct platform_device tq210_b_pwr_5v = {
        .name          = "reg-fixed-voltage",
        .id            = -1,
        .dev = {
                .platform_data = &tq210_b_pwr_5v_pdata,
        },
};
#endif
#ifdef CONFIG_TOUCHSCREEN_EGALAX
static struct i2c_gpio_platform_data i2c5_platdata = {
        .sda_pin                = S5PV210_GPB(6),
        .scl_pin                = S5PV210_GPB(7),
        .udelay                 = 2,
        .sda_is_open_drain      = 0,
        .scl_is_open_drain      = 0,
        .scl_is_output_only     = 0,
};

//static struct platform_device   s3c_device_i2c5 = {
struct platform_device   s3c_device_i2c5 = {
        .name                   = "i2c-gpio",
        .id                     = 5,
        .dev.platform_data      = &i2c5_platdata,
};

static struct i2c_board_info i2c_devs5[] __initdata = {
        {
                I2C_BOARD_INFO(EETI_TS_DEV_NAME, 0x04),
                .platform_data = &egalax_platdata,
                .irq = IRQ_EINT6,
        },
};
#endif
struct platform_device tq210_gsensor = {
	.name		= "Acceleration_sensor",
	.id		= -1,
};



#define  S5PV210_VIDEO_SAMSUNG_MEMSIZE_FIMC0	(6144 * SZ_2K)//(6144 * SZ_1K)
#define  S5PV210_VIDEO_SAMSUNG_MEMSIZE_FIMC1	(9900 * SZ_2K)//(9900 * SZ_1K)
#define  S5PV210_VIDEO_SAMSUNG_MEMSIZE_FIMC2	(6144 * SZ_2K)//(6144 * SZ_1K)
#define  S5PV210_VIDEO_SAMSUNG_MEMSIZE_MFC0		(36864 * SZ_1K)
#define  S5PV210_VIDEO_SAMSUNG_MEMSIZE_MFC1		(36864 * SZ_1K)
#if (CONFIG_VIDEO_SAMSUNG_MEMSIZE_FIMD == 0)
#define  S5PV210_VIDEO_SAMSUNG_MEMSIZE_FIMD		(S5PV210_LCD_WIDTH * \
												S5PV210_LCD_HEIGHT * NUM_BUFFER * \
												(CONFIG_FB_S3C_NR_BUFFERS + \
												(CONFIG_FB_S3C_NUM_OVLY_WIN * \
												CONFIG_FB_S3C_NUM_BUF_OVLY_WIN))) //sayanta macro values need to be set
#else
#define  S5PV210_VIDEO_SAMSUNG_MEMSIZE_FIMD		(3072 * SZ_4K)
#endif

#define  S5PV210_VIDEO_SAMSUNG_MEMSIZE_JPEG		(8192 * SZ_1K)
#define  S5PV210_ANDROID_PMEM_MEMSIZE_PMEM_GPU1	(1800 * SZ_1K)
#define  S5PV210_VIDEO_SAMSUNG_MEMSIZE_G2D		(8192 * SZ_1K)

static struct s5p_media_device s5pv210_media_devs[] = {
        [0] = {
                .id = S5P_MDEV_MFC,
                .name = "mfc",
                .bank = 0,
                .memsize = S5PV210_VIDEO_SAMSUNG_MEMSIZE_MFC0,
                .paddr = 0,
        },
        [1] = {
                .id = S5P_MDEV_MFC,
                .name = "mfc",
                .bank = 1,
                .memsize = S5PV210_VIDEO_SAMSUNG_MEMSIZE_MFC1,
                .paddr = 0,
        },
        [2] = {
                .id = S5P_MDEV_FIMC0,
                .name = "fimc0",
                .bank = 1,
                .memsize = S5PV210_VIDEO_SAMSUNG_MEMSIZE_FIMC0,
                .paddr = 0,
        },
        [3] = {
                .id = S5P_MDEV_FIMC1,
                .name = "fimc1",
                .bank = 1,
                .memsize = S5PV210_VIDEO_SAMSUNG_MEMSIZE_FIMC1,
                .paddr = 0,
        },
        [4] = {
                .id = S5P_MDEV_FIMC2,
                .name = "fimc2",
                .bank = 1,
                .memsize = S5PV210_VIDEO_SAMSUNG_MEMSIZE_FIMC2,
                .paddr = 0,
        },
        [5] = {
                .id = S5P_MDEV_JPEG,
                .name = "jpeg",
                .bank = 0,
                .memsize = S5PV210_VIDEO_SAMSUNG_MEMSIZE_JPEG,
                .paddr = 0,
        },
	[6] = {
                .id = S5P_MDEV_FIMD,
                .name = "fimd",
                .bank = 1,
                .memsize = S5PV210_VIDEO_SAMSUNG_MEMSIZE_FIMD,
                .paddr = 0,
        },
	[7] = {
                .id = S5P_MDEV_PMEM_GPU1,
                .name = "pmem_gpu1",
                .bank = 0, /* OneDRAM */
                .memsize = S5PV210_ANDROID_PMEM_MEMSIZE_PMEM_GPU1,
                .paddr = 0,
        },
	[8] = {
		.id = S5P_MDEV_G2D,
		.name = "g2d",
		.bank = 0,
		.memsize = S5PV210_VIDEO_SAMSUNG_MEMSIZE_G2D,
		.paddr = 0,
	},
};

#ifdef CONFIG_ANDROID_PMEM
static struct android_pmem_platform_data pmem_pdata = {
        .name = "pmem",
        .no_allocator = 1,
        .cached = 1,
        .start = 0,
        .size = 0,
};

static struct android_pmem_platform_data pmem_gpu1_pdata = {
        .name = "pmem_gpu1",
        .no_allocator = 1,
        .cached = 1,
        .buffered = 1,
        .start = 0,
        .size = 0,
};
 
static struct android_pmem_platform_data pmem_adsp_pdata = {
        .name = "pmem_adsp",
        .no_allocator = 1,
        .cached = 1,
        .buffered = 1,
        .start = 0,
        .size = 0,
};      

static struct platform_device pmem_device = {
        .name = "android_pmem",
        .id = 0,
        .dev = { .platform_data = &pmem_pdata },
};

static struct platform_device pmem_gpu1_device = {
        .name = "android_pmem",
        .id = 1,
        .dev = { .platform_data = &pmem_gpu1_pdata },
};

static struct platform_device pmem_adsp_device = {
        .name = "android_pmem",
        .id = 2,
        .dev = { .platform_data = &pmem_adsp_pdata },
};

static void __init android_pmem_set_platdata(void)
{
        pmem_pdata.start = (u32)s5p_get_media_memory_bank(S5P_MDEV_PMEM, 0);
        pmem_pdata.size = (u32)s5p_get_media_memsize_bank(S5P_MDEV_PMEM, 0);

        pmem_gpu1_pdata.start =
                (u32)s5p_get_media_memory_bank(S5P_MDEV_PMEM_GPU1, 0);
        pmem_gpu1_pdata.size =
                (u32)s5p_get_media_memsize_bank(S5P_MDEV_PMEM_GPU1, 0);

        pmem_adsp_pdata.start =
                (u32)s5p_get_media_memory_bank(S5P_MDEV_PMEM_ADSP, 0);
        pmem_adsp_pdata.size =
                (u32)s5p_get_media_memsize_bank(S5P_MDEV_PMEM_ADSP, 0);
}
#endif


static void tq210_lcd_set_power(struct plat_lcd_data *pd,
					unsigned int power)
{
	if (power) {
#if !defined(CONFIG_BACKLIGHT_PWM)
		gpio_request(S5PV210_GPD0(0), "GPD0");
		gpio_direction_output(S5PV210_GPD0(0), 1);
		gpio_free(S5PV210_GPD0(0));
#endif

		/* fire nRESET on power up */
		gpio_request(S5PV210_GPH0(6), "GPH0");

		gpio_direction_output(S5PV210_GPH0(6), 1);

		gpio_set_value(S5PV210_GPH0(6), 0);
		mdelay(10);

		gpio_set_value(S5PV210_GPH0(6), 1);
		mdelay(10);

		gpio_free(S5PV210_GPH0(6));
	} else {
#if !defined(CONFIG_BACKLIGHT_PWM)
		gpio_request(S5PV210_GPD0(0), "GPD0");
		gpio_direction_output(S5PV210_GPD0(0), 0);
		gpio_free(S5PV210_GPD0(0));
#endif
	}
}

static struct plat_lcd_data tq210_lcd_data = {
	.set_power	= tq210_lcd_set_power,
};

static struct platform_device tq210_lcd_platdata = {
	.name			= "platform-lcd",
	.dev.parent		= &s3c_device_fb.dev,
	.dev.platform_data	= &tq210_lcd_data,
};


#if defined(CONFIG_FB_S3C_LCD800X480_A70)||defined(CONFIG_FB_TQ_AUTO_DETECT)
static struct s3cfb_lcd A70_TN92 = {
	.width		= 800,
	.height		= 480,
	.bpp		= 24,
	.freq		= 100,
//	.clkval_f	= 6,
	.timing = {
		.h_fp	= 14,
		.h_bp	= 27,
		.h_sw	= 20,
		.v_fp	= 22,
		.v_fpe	= 1,
		.v_bp	= 10,
		.v_bpe	= 1,
		.v_sw	= 13,
	},

	.polarity = {
		.rise_vclk	= 0,
		.inv_hsync	= 1,
		.inv_vsync	= 1,
		.inv_vden	= 0,
	},
};
#elif defined(CONFIG_FB_S3C_LCD800X600_A104)
static struct s3cfb_lcd A104 = {
	.width	= 800,
	.height	= 600,
	.bpp	= 24,
	.freq	= 60,

	.timing = {
		.h_fp	= 10,
		.h_bp	= 10,
		.h_sw	= 36,
		.v_fp	= 22,//12
		.v_fpe	= 2,
		.v_bp	= 10,
		.v_bpe	= 2,//1
		.v_sw	= 13,
	},

	.polarity = {
		.rise_vclk	= 0,
		.inv_hsync	= 1,
		.inv_vsync	= 1,
		.inv_vden	= 0,
	},
};


#elif defined(CONFIG_FB_S3C_VGA1024X768)
static struct s3cfb_lcd VGA1024X768 = {
	.width	= 1024,
	.height	= 768,
	.bpp	= 24,
	.freq	= 45,

	.timing = {
		.h_fp	= 60,
		.h_bp	= 120,
		.h_sw	= 50,
		.v_fp	= 3,
		.v_fpe	= 1,
		.v_bp	= 4,
		.v_bpe	= 1,
		.v_sw	= 3,
	},

	.polarity = {
		.rise_vclk	= 0,
		.inv_hsync	= 1,
		.inv_vsync	= 1,
		.inv_vden	= 0,
	},
};

#elif defined(CONFIG_FB_S3C_LCD480X272_T43)
static struct s3cfb_lcd T43 = {
	.width	= 480,
	.height	= 272,
	.bpp	= 24,
	.freq	= 60,

	.timing = {
		.h_fp	= 2,
		.h_bp	= 2,
		.h_sw	= 41,
		.v_fp	= 3,
		.v_fpe	= 1,
		.v_bp	= 2,
		.v_bpe	= 1,
		.v_sw	= 10,
	},

	.polarity = {
		.rise_vclk	= 0,
		.inv_hsync	= 1,
		.inv_vsync	= 1,
		.inv_vden	= 0,
	},
};
#endif	//#if defined(CONFIG_FB_S3C_LCD800X480_A70)


static void EmbedSky_LCD_cfg_gpio(struct platform_device *pdev)
{
        int i;

        for (i = 0; i < 8; i++) {
                s3c_gpio_cfgpin(S5PV210_GPF0(i), S3C_GPIO_SFN(2));
                s3c_gpio_setpull(S5PV210_GPF0(i), S3C_GPIO_PULL_NONE);
        }

        for (i = 0; i < 8; i++) {
                s3c_gpio_cfgpin(S5PV210_GPF1(i), S3C_GPIO_SFN(2));
                s3c_gpio_setpull(S5PV210_GPF1(i), S3C_GPIO_PULL_NONE);
        }

        for (i = 0; i < 8; i++) {
                s3c_gpio_cfgpin(S5PV210_GPF2(i), S3C_GPIO_SFN(2));
                s3c_gpio_setpull(S5PV210_GPF2(i), S3C_GPIO_PULL_NONE);
        }
        for (i = 0; i < 4; i++) {
                s3c_gpio_cfgpin(S5PV210_GPF3(i), S3C_GPIO_SFN(2));
                s3c_gpio_setpull(S5PV210_GPF3(i), S3C_GPIO_PULL_NONE);
        }

        /* mDNIe SEL: why we shall write 0x2 ? */
        writel(0x2, S5P_MDNIE_SEL);

        writel(0xC0, S5PV210_GPF0_BASE + 0xc);
}



#define S5PV210_GPD_0_0_TOUT_0  (0x2)
#define S5PV210_GPD_0_1_TOUT_1  (0x2 << 4)
#define S5PV210_GPD_0_2_TOUT_2  (0x2 << 8)
#define S5PV210_GPD_0_3_TOUT_3  (0x2 << 12)
static int EmbedSky_LCD_backlight_on(struct platform_device *pdev)
{
        int err;

        err = gpio_request(S5PV210_GPD0(0), "GPD0");

        if (err) {
                printk(KERN_ERR "failed to request GPD0 for "
                        "lcd backlight control\n");
                return err;
        }

        gpio_direction_output(S5PV210_GPD0(0), 1);

#if defined(CONFIG_FB_S3C_VGA640480) || defined(CONFIG_FB_S3C_VGA800600)
	s3c_gpio_cfgpin(S5PV210_GPD0(0), /*S5PV210_GPD_0_0_TOUT_0*/1);
#else
	s3c_gpio_cfgpin(S5PV210_GPD0(0), S5PV210_GPD_0_0_TOUT_0);
#endif
        gpio_free(S5PV210_GPD0(0));

        return 0;
}


static int EmbedSky_LCD_backlight_off(struct platform_device *pdev, int onoff)
{
	int err;

	err = gpio_request(S5PV210_GPD0(0), "GPD0");
	if (err) {
		printk(KERN_ERR "failed to request GPD0 for "
				"lcd backlight control\n");
		return err;
	}

	gpio_direction_output(S5PV210_GPD0(0), 0);

	gpio_free(S5PV210_GPD0(0));
	return 0;
}

static int EmbedSky_LCD_reset_lcd(struct platform_device *pdev)
{
        return 0;
}

static struct s3c_platform_fb EmbedSky_LCD_fb_data __initdata = {
        .hw_ver 			= 0x62,
        .clk_name       	= "sclk_fimd",
        .nr_wins 			= 5,
        .default_win 		= CONFIG_FB_S3C_DEFAULT_WINDOW,
        .swap 				= FB_SWAP_WORD | FB_SWAP_HWORD,

#if defined(CONFIG_FB_S3C_LCD800X480_A70)||defined(CONFIG_FB_TQ_AUTO_DETECT)
        .lcd 				= &A70_TN92,
#elif defined(CONFIG_FB_S3C_LCD800X600_A104)
        .lcd	 			= &A104,
#elif defined(CONFIG_FB_S3C_VGA1024X768)
        .lcd 				= &VGA1024X768,
#elif defined(CONFIG_FB_S3C_LCD480X272_T43)
        .lcd 				= &T43,
#endif
        .cfg_gpio			= EmbedSky_LCD_cfg_gpio,
        .backlight_on		= EmbedSky_LCD_backlight_on,
        .backlight_onoff	= EmbedSky_LCD_backlight_off,
        .reset_lcd			= EmbedSky_LCD_reset_lcd,
};
#ifdef CONFIG_TQ210_VGA
static struct s3cfb_lcd vga = {
	.width	= 800,
	.height	= 600,
	.bpp	= 24,
	.freq	= 60,

	.timing = {
		.h_fp	= 10,
		.h_bp	= 20,
		.h_sw	= 10,
		.v_fp	= 10,
		.v_fpe	= 1,
		.v_bp	= 20,
		.v_bpe	= 1,
		.v_sw	= 10,
	},

	.polarity = {
		.rise_vclk	= 0,
		.inv_hsync	= 1,
		.inv_vsync	= 1,
		.inv_vden	= 0,
	},
};

static void vga_cfg_gpio(struct platform_device *pdev)
{
	int i;

	for (i = 0; i < 8; i++) {
		s3c_gpio_cfgpin(S5PV210_GPF0(i), S3C_GPIO_SFN(2));
		s3c_gpio_setpull(S5PV210_GPF0(i), S3C_GPIO_PULL_NONE);
	}

	for (i = 0; i < 8; i++) {
		s3c_gpio_cfgpin(S5PV210_GPF1(i), S3C_GPIO_SFN(2));
		s3c_gpio_setpull(S5PV210_GPF1(i), S3C_GPIO_PULL_NONE);
	}

	for (i = 0; i < 8; i++) {
		s3c_gpio_cfgpin(S5PV210_GPF2(i), S3C_GPIO_SFN(2));
		s3c_gpio_setpull(S5PV210_GPF2(i), S3C_GPIO_PULL_NONE);
	}

	for (i = 0; i < 4; i++) {
		s3c_gpio_cfgpin(S5PV210_GPF3(i), S3C_GPIO_SFN(2));
		s3c_gpio_setpull(S5PV210_GPF3(i), S3C_GPIO_PULL_NONE);
	}

	/* mDNIe SEL: why we shall write 0x2 ? */
	writel(0x2, S5P_MDNIE_SEL);

	/* drive strength to max */
	writel(0xffffffff, S5PV210_GPF0_BASE + 0xc);
	writel(0xffffffff, S5PV210_GPF1_BASE + 0xc);
	writel(0xffffffff, S5PV210_GPF2_BASE + 0xc);
	writel(0x000000ff, S5PV210_GPF3_BASE + 0xc);
}


static int vga_backlight_on(struct platform_device *pdev)
{
	return 0;
}

static int vga_backlight_off(struct platform_device *pdev, int onoff)
{

}

static int vga_reset_lcd(struct platform_device *pdev)
{

	return 0;
}

static struct s3c_platform_fb vga_fb_data __initdata = {
	.hw_ver	= 0x62,
	.nr_wins = 5,
	.default_win = CONFIG_FB_S3C_DEFAULT_WINDOW,
	.swap = FB_SWAP_WORD | FB_SWAP_HWORD,

	.lcd = &vga,
	.cfg_gpio	= vga_cfg_gpio,
	.backlight_on	= vga_backlight_on,
	.backlight_onoff    = vga_backlight_off,
	.reset_lcd	= vga_reset_lcd,
};
#endif


#ifdef CONFIG_HAVE_PWM
struct s3c_pwm_data {
	/* PWM output port */
	unsigned int gpio_no;
	const char  *gpio_name;
	unsigned int gpio_set_value;
};

struct s3c_pwm_data pwm_data[] = {
	{
		.gpio_no    = S5PV210_GPD0(0),
		.gpio_name  = "GPD",
#if defined(CONFIG_FB_S3C_VGA640480) ||defined(CONFIG_FB_S3C_VGA800600)
		.gpio_set_value = 1,
#else
		.gpio_set_value = S5PV210_GPD_0_0_TOUT_0,
#endif
	},{
		.gpio_no    = S5PV210_GPD0(1),
		.gpio_name  = "GPD",
		.gpio_set_value = S5PV210_GPD_0_1_TOUT_1,
	}, {
		.gpio_no    = S5PV210_GPD0(2),
		.gpio_name      = "GPD",
		.gpio_set_value = S5PV210_GPD_0_2_TOUT_2,
	}, {
		.gpio_no    = S5PV210_GPD0(3),
		.gpio_name      = "GPD",
		.gpio_set_value = S5PV210_GPD_0_3_TOUT_3,
	}
};
#endif	//CONFIG_HAVE_PWM

#if defined(CONFIG_BACKLIGHT_PWM)
static struct platform_pwm_backlight_data smdk_backlight_data = {
	.pwm_id  = 0,
	.max_brightness = 255,
	.dft_brightness = 255,
	.pwm_period_ns  = 20000,//25000,Áî±‰∫éËÉåÂÖâÈ¢ëÁéáÂ§™È´òÔºåËøô‰∏™IOÁ´ØÂè£ÈïøÂ∫¶‰∏çË°å
};

static struct platform_device smdk_backlight_device = {
	.name      = "pwm-backlight",
	.id        = -1,
	.dev        = {
		.parent = &s3c_device_timer[0].dev,
		.platform_data = &smdk_backlight_data,
	},
};

static void __init smdk_backlight_register(void)
{
	int ret;
			#ifdef CONFIG_HAVE_PWM
	int i, ntimer;

	/* Timer GPIO Set */
	ntimer = ARRAY_SIZE(pwm_data);
	for (i = 0; i < ntimer; i++) {
		if (gpio_is_valid(pwm_data[i].gpio_no)) {
			ret = gpio_request(pwm_data[i].gpio_no,
				pwm_data[i].gpio_name);
			if (ret) {
				printk(KERN_ERR "failed to get GPIO for PWM\n");
				return;
			}
			s3c_gpio_cfgpin(pwm_data[i].gpio_no,
				pwm_data[i].gpio_set_value);

			gpio_free(pwm_data[i].gpio_no);
		}
	}
		#endif	//CONFIG_HAVE_PWM
	ret = platform_device_register(&smdk_backlight_device);
	if (ret)
		printk(KERN_ERR "smdk: failed to register backlight device: %d\n", ret);
}

#endif	//CONFIG_BACKLIGHT_PWM

/******************************************************************************
 * keypad
 ******************************************************************************/
#ifdef CONFIG_KEYBOARD_S3C_GPIO
static struct gpio_keys_button gpio_buttons[] = {

	{
		.gpio		= S5PV210_GPH0(0),
		.code		= KEY_UP,
		.desc		= "KEY_UP",
		.active_low	= 1,
		.wakeup		= 1,
	},
	{
		.gpio		= S5PV210_GPH0(1),
		.code		= KEY_DOWN,
		.desc		= "KEY_DOWN",
		.active_low	= 1,
		.wakeup		= 1,
	},	
	{
		.gpio		= S5PV210_GPH0(2),
		.code		= KEY_LEFT,
		.desc		= "KEY_LEFT",
		.active_low	= 1,
		.wakeup		= 1,
	},
	{
		.gpio		= S5PV210_GPH0(3),
		.code		= KEY_RIGHT,
		.desc		= "KEY_RIGHT",
		.active_low	= 1,
		.wakeup		= 1,
	},
	{
		.gpio		= S5PV210_GPH0(4),
		.code		= 28,
		.desc		= "ENTER",
		.active_low	= 1,
		.wakeup		= 1,
	},		
	{
		.gpio		= S5PV210_GPH0(5),
		.code		= 158,
		.desc		= "BACK",
		.active_low	= 1,
		.wakeup		= 1,
	},
	{
		.gpio		= S5PV210_GPH2(6),
		.code		= 139,
		.desc		= "KEY_MENU_SHARD",
		.active_low	= 1,
		.wakeup		= 0,
	},		
	{
		.gpio		= S5PV210_GPH2(7),
		.code		= 116,
		.desc		= "POWER",
		.active_low	= 1,
		.wakeup		= 0,
	},
};


static struct gpio_keys_platform_data gpio_button_data = {
	.buttons	= gpio_buttons,
	.nbuttons	= ARRAY_SIZE(gpio_buttons),
};

static struct platform_device s3c_device_gpio_button = {
	.name		= "gpio-keys",
	.id		= -1,
	.num_resources	= 0,
	.dev		= {
		.platform_data	= &gpio_button_data,
	}
};
#endif

#ifdef CONFIG_KEYBOARD_HS0038
static struct gpio_keys_button tq210_hs0038[] = {
		{
			.gpio		= S5PV210_GPH0(6),/* 4(- + backspace enter) */
			.desc		= "hs0038",
		},
};

static struct gpio_keys_platform_data tq210_hs0038_data = {
	.buttons	= tq210_hs0038,
	.nbuttons	= ARRAY_SIZE(tq210_hs0038),
};

struct platform_device tq210_hs0038_device = {
	.name		= "hs0038",
	.id		= -1,
	.dev		= {
		.platform_data	= &tq210_hs0038_data,
	}
};
#endif
/*********************************************************************************************************/
//------------------------------------------------------------------------------
#include <linux/i2c-gpio.h>
#if 0
#define GPIO_SCK5		S5PV210_GPD1(3)//S5PV210_GPG2(5)
#define GPIO_SDA5		S5PV210_GPD1(2)//S5PV210_GPG2(6)
#elif 1
#define GPIO_SCK5		S5PV210_GPG2(5)
#define GPIO_SDA5		S5PV210_GPG2(6)
#endif 

static struct i2c_gpio_platform_data i2c5_gpio_data = {
	.sda_pin                = GPIO_SDA5,
	.scl_pin                = GPIO_SCK5,

};

static struct platform_device i2c5_gpio = {
	.name                   = "i2c-gpio",
	.id                     = 5,
	.dev = {
		.platform_data  = &i2c5_gpio_data,
	},
 };

 
 
static struct platform_device *tq210_devices[] __initdata = {
	
	&s3c_device_cfcon,
	&s3c_device_fb,
#ifdef CONFIG_MTD_ONENAND
	&s5p_device_onenand,
#endif
#ifdef CONFIG_MTD_NAND
	&s3c_device_nand,
#endif
	&s3c_device_hsmmc0,
	&s3c_device_hsmmc1,
	&s3c_device_hsmmc2,
	&s3c_device_hsmmc3,
	
	&s3c_device_i2c0,
	&s3c_device_i2c1,
	&s3c_device_i2c2,
//------------------------------
	&i2c5_gpio,
#ifdef CONFIG_S3C_ADC
	&s3c_device_adc,
#endif
//------------------------------	
#ifdef CONFIG_TOUCHSCREEN_EGALAX
	&s3c_device_i2c5,
#endif
	&s3c_device_rtc,
#ifdef CONFIG_TOUCHSCREEN_S3C
	&s3c_device_ts,
#endif
	&s3c_device_wdt,
#ifdef CONFIG_SND_SAMSUNG_AC97
	&s5pv210_device_ac97,
#endif
#ifdef CONFIG_SND_SOC_WM8960_TQ210
	&s5pv210_device_iis1,
	&s5pv210_device_iis0,
#endif

	&s5pv210_device_spdif,
#ifdef CONFIG_SND_SAMSUNG_PCM
#ifdef CONFIG_SND_SAMSUNG_PCM_USE_I2S1_MCLK
	&s5pv210_device_pcm0,
#endif
#endif 	/*end of CONFIG_SND_SAMSUNG_PCM*/
	&samsung_asoc_dma,
	&samsung_device_keypad,
#ifdef CONFIG_DM9000
	&tq210_dm9000,
#endif
	&tq210_lcd_platdata,
	&s3c_device_timer[0],
	&s3c_device_timer[1],
	&s3c_device_timer[2],
	&s3c_device_timer[3],
//	&tq210_backlight_device,
//	&smdk_backlight_device,

	&s5p_device_ehci,
	&s5p_device_ohci,
#ifdef CONFIG_USB_GADGET
	&s3c_device_usbgadget,
#endif
#ifdef CONFIG_VIDEO_FIMC
	&s3c_device_fimc0,
	&s3c_device_fimc1,
	&s3c_device_fimc2,
#endif
#ifdef CONFIG_VIDEO_FIMC_MIPI
	&s3c_device_csis,
#endif
#ifdef CONFIG_SND_S5P_RP
	&s3c_device_rp,
#endif

#ifdef CONFIG_VIDEO_JPEG_V2
	&s3c_device_jpeg,
#endif
#ifdef CONFIG_VIDEO_MFC50
        &s3c_device_mfc,
#endif
#ifdef CONFIG_ANDROID_PMEM
	&pmem_gpu1_device,
#endif
#ifdef CONFIG_SPI_S3C64XX
	&s5pv210_device_spi0,
	&s5pv210_device_spi1,
#endif
#ifdef CONFIG_REGULATOR
	&tq210_b_pwr_5v,
#endif
#ifdef CONFIG_S5PV210_POWER_DOMAIN
	&s5pv210_pd_tv,
	&s5pv210_pd_lcd,
	&s5pv210_pd_g3d,
	&s5pv210_pd_mfc,
	&s5pv210_pd_audio,
#endif
	&s3c_device_g3d,
#ifdef CONFIG_VIDEO_G2D
	&s3c_device_g2d,
#endif
#ifdef CONFIG_VIDEO_TV20
	&s5p_device_tvout,
	&s5p_device_cec,
	&s5p_device_hpd,
#endif

#ifdef CONFIG_KEYBOARD_S3C_GPIO
	&s3c_device_gpio_button,
#endif
#ifdef CONFIG_KEYBOARD_HS0038
	&tq210_hs0038_device,
#endif
#ifdef CONFIG_SPI_TQ210	
	&tq210_gsensor,
#endif	
};

#ifdef CONFIG_S3C_ADC

static struct s3c_adc_mach_info s3c_adc_platform __initdata = {
	/* s5pc100 supports 12-bit resolution */
	.delay  = 10,//10000
	.presc  = 19,
	.resolution = 12,//12‰ΩçÁöÑÂàÜËæ®Áéá
};
#endif



 
//---------
#include <media/tqcam_platform.h>
//---------

#define BOARD_VER  3

#if(BOARD_VER == 1)
#define GPIO_CAM0_RST		S5PV210_GPC1(1)
#define GPIO_CAM1_RST		S5PV210_GPC1(1)
#define GPIO_CAM0_POWERDOWN	S5PV210_GPE1(4)		//
#define GPIO_CAM1_POWERDOWN	S5PV210_GPC1(0)
#elif(BOARD_VER == 2)
#define GPIO_CAM0_RST		S5PV210_GPH3(6)
#define GPIO_CAM1_RST		S5PV210_GPJ3(5)
#define GPIO_CAM0_POWERDOWN	S5PV210_GPE1(4)		//
#define GPIO_CAM1_POWERDOWN	S5PV210_GPC1(0)
#elif(BOARD_VER == 3) 
#define GPIO_CAM0_RST		S5PV210_GPH3(6)
#define GPIO_CAM1_RST		S5PV210_GPH3(7)
#define GPIO_CAM0_POWERDOWN	S5PV210_GPE1(4)		//
#define GPIO_CAM1_POWERDOWN	S5PV210_GPJ1(3)
#endif

#define GPIO_RST_9650		GPIO_CAM0_RST
#define GPIO_PWD_9650		GPIO_CAM0_POWERDOWN
#define GPIO_RST_3640		GPIO_CAM1_RST
#define GPIO_PWD_3640		GPIO_CAM1_POWERDOWN
#define GPIO_RST_3640_1		GPIO_CAM0_RST
#define GPIO_PWD_3640_1		GPIO_CAM0_POWERDOWN
#define GPIO_RST_7113		GPIO_CAM0_RST
#define GPIO_PWD_7113		GPIO_CAM0_POWERDOWN
#define GPIO_RST_5150		GPIO_CAM0_RST
#define GPIO_PWD_5150		GPIO_CAM0_POWERDOWN
#define IIC_NUM_CAM_USED		5


void gpio_cam_rst_cfg(unsigned int gpio_rst,int rst_statue)
{
	int err;

	err = gpio_request(gpio_rst, "CAM_RST");
	if (err)
		printk(KERN_ERR "#### failed to request GP_RST\n");	

	s3c_gpio_cfgpin(gpio_rst, S3C_GPIO_SFN(1));
	s3c_gpio_setpull(gpio_rst, S3C_GPIO_PULL_NONE);
	mdelay(1);  
	if(rst_statue != 0)
		gpio_direction_output(gpio_rst, 1);
	else 
		gpio_direction_output(gpio_rst, 0);
	gpio_free(gpio_rst);
	mdelay(15);
}

void gpio_cam_pwd_cfg(unsigned int gpio_pwd,int pwd_statue)
{
	int err;

	err = gpio_request(gpio_pwd, "CAM_PWD");
	if (err)
		printk(KERN_ERR "#### failed to request GP_PWD\n");	

	s3c_gpio_cfgpin(gpio_pwd, S3C_GPIO_SFN(1));
	s3c_gpio_setpull(gpio_pwd, S3C_GPIO_PULL_NONE);
	if(pwd_statue != 0)
		gpio_direction_output(gpio_pwd, 1);
	else 
		gpio_direction_output(gpio_pwd, 0);
	gpio_free(gpio_pwd);
	mdelay(15);
}



/*
 * External camera reset
 * Because the most of cameras take i2c bus signal, so that
 * you have to reset at the boot time for other i2c slave devices.
 * This function also called at fimc_init_camera()
 * Do optimization for cameras on your platform.
*/
#ifdef CONFIG_VIDEO_TVP5150
static int tvp5150_power(int onoff)//low_reset GPE1_4
{
	if(onoff != 0)		//on
	{
		gpio_cam_pwd_cfg(GPIO_PWD_5150,0);					//cam0 on
		mdelay(1);		
		gpio_cam_pwd_cfg(GPIO_PWD_5150,1);
		mdelay(1);
		gpio_cam_rst_cfg(GPIO_RST_5150,0);						//cam0 on
		mdelay(1);
		gpio_cam_rst_cfg(GPIO_RST_5150,1);						//cam0 on
		printk("5150_poweron \n");
	}
	else											//cam1 not refrect 
	{	
		gpio_cam_rst_cfg(GPIO_RST_5150,0);						//cam0 off
		gpio_cam_pwd_cfg(GPIO_PWD_5150,0);					//cam0 off
		printk("5150_poweroff \n");
	}
	return 0;
}

static struct tqcam_platform_data tvp5150_plat =
{
	.default_width = 720,
	.default_height = 288,
	.pixelformat = V4L2_PIX_FMT_VYUY,
	.freq = 27000000,
	.is_mipi = 0,
};

static struct i2c_board_info  tvp5150_i2c_info = 
{
	I2C_BOARD_INFO("tvp5150", 0xB8>>1),
	.platform_data = &tvp5150_plat,
};

static struct s3c_platform_camera tvp5150 = {
	.id		= CAMERA_PAR_A,	
	.type		= CAM_TYPE_ITU,
	.fmt		= ITU_656_YCBCR422_8BIT,
	.order422	= CAM_ORDER422_8BIT_CBYCRY,
	.i2c_busnum	= IIC_NUM_CAM_USED,
	.info		= &tvp5150_i2c_info,
	.pixelformat	= V4L2_PIX_FMT_VYUY,
	.srclk_name	= "mout_mpll",
	.clk_name	= "sclk_cam0",
	.clk_rate	= 27000000,
	.width		= 720,
	.height		= 288,
	.window		= {
		.left	= 0,
		.top	= 0,
		.width	= 720,
		.height	= 288,
	},

	/* Polarity */
	.inv_pclk	= 0,
	.inv_vsync	= 1,
	.inv_href	= 0,
	.inv_hsync	= 0,

	.initialized	= 0,

	.cam_power	= tvp5150_power,

};
#endif




#ifdef CONFIG_VIDEO_OV3640

static int smdkv210_OV3640_0_power(int onoff)//low_reset GPE1_4
{
	if(onoff != 0)		//on
	{
		gpio_cam_rst_cfg(GPIO_RST_3640,1);						//cam0 on
		gpio_cam_pwd_cfg(GPIO_PWD_3640,0);					//cam0 on
	}
	else											//cam1 not refrect 
	{	
		gpio_cam_rst_cfg(GPIO_RST_3640,0);						//cam0 off
		gpio_cam_pwd_cfg(GPIO_PWD_3640,1);					//cam0 off
	}

	return 0;

}

static struct tqcam_platform_data ov3640_plat =
{
	.default_width = 640,
	.default_height = 480,
	.pixelformat = V4L2_PIX_FMT_VYUY,
	.freq = 40000000,
	.is_mipi = 0,
};

static struct i2c_board_info  ov3640_i2c_info = 
{
	I2C_BOARD_INFO("OV3640", 0x78>>1),
	.platform_data = &ov3640_plat,
};

static struct s3c_platform_camera ov3640_0 = {
	.id		= CAMERA_PAR_A,
	.type		= CAM_TYPE_ITU,
	.fmt		= ITU_601_YCBCR422_8BIT,
	.order422	= CAM_ORDER422_8BIT_YCBYCR,
	.i2c_busnum	= IIC_NUM_CAM_USED,
	.info		= &ov3640_i2c_info,
	.pixelformat	= V4L2_PIX_FMT_VYUY,
	.srclk_name	= "mout_mpll",
	.clk_name	= "sclk_cam0",
	.clk_rate	= 40000000,
	.line_length	= 640,
	.width		= 640,
	.height		= 480,
	.window		= {
		.left	= 0,
		.top	= 0,
		.width	= 640,
		.height	= 480,
	},

	/* Polarity */
	.inv_pclk	= 0,
	.inv_vsync	= 0,
	.inv_href	= 0,
	.inv_hsync	= 0,

	.initialized	= 0,

	.cam_power	= smdkv210_OV3640_0_power,

};



static int smdkv210_OV3640_1_power(int onoff)//low_reset GPE1_4
{
	if(onoff != 0)		//on
	{
		gpio_cam_rst_cfg(GPIO_RST_3640_1,1);						//cam0 on
		gpio_cam_pwd_cfg(GPIO_PWD_3640_1,0);					//cam0 on
	}
	else											//cam1 not refrect 
	{	
		gpio_cam_rst_cfg(GPIO_RST_3640_1,0);						//cam0 off
		gpio_cam_pwd_cfg(GPIO_PWD_3640_1,1);					//cam0 off
	}

	return 0;

}


static struct s3c_platform_camera ov3640_1 = {
	.id		= CAMERA_PAR_A,
	.type		= CAM_TYPE_ITU,
	.fmt		= ITU_601_YCBCR422_8BIT,
	.order422	= CAM_ORDER422_8BIT_YCBYCR,
	.i2c_busnum	= IIC_NUM_CAM_USED,
	.info		= &ov3640_i2c_info,
	.pixelformat	= V4L2_PIX_FMT_VYUY,
	.srclk_name	= "mout_mpll",
	.clk_name	= "sclk_cam0",
	.clk_rate	= 40000000,
	.line_length	= 640,
	.width		= 640,
	.height		= 480,
	.window		= {
		.left	= 0,
		.top	= 0,
		.width	= 640,
		.height	= 480,
	},

	/* Polarity */
	.inv_pclk	= 0,
	.inv_vsync	= 0,
	.inv_href	= 0,
	.inv_hsync	= 0,

	.initialized	= 0,

	.cam_power	= smdkv210_OV3640_1_power,

};

#endif

#ifdef CONFIG_VIDEO_OV9650
static int tqcam_OV9650_power(int onoff)//low_reset GPE1_4
{
	if(onoff != 0)		//on
	{
		gpio_cam_rst_cfg(GPIO_RST_9650,0);						//cam0 on
		gpio_cam_pwd_cfg(GPIO_PWD_9650,0);					//cam0 on
	}
	else											//cam1 not refrect 
	{	
		gpio_cam_rst_cfg(GPIO_RST_9650,1);						//cam0 off
		gpio_cam_pwd_cfg(GPIO_PWD_9650,1);					//cam0 off
	}

	return 0;
}

static struct tqcam_platform_data ov9650_plat =
{
	.default_width = 640,
	.default_height = 480,
	.pixelformat = V4L2_PIX_FMT_VYUY,
	.freq = 24000000,
	.is_mipi = 0,
};

static struct i2c_board_info  ov9650_i2c_info = 
{
	I2C_BOARD_INFO("OV9650", 0x60>>1),
	.platform_data = &ov9650_plat,
};

static struct s3c_platform_camera ov9650 = {
	.id			= CAMERA_PAR_A,
	.type		= CAM_TYPE_ITU,
	.fmt		= ITU_601_YCBCR422_8BIT,
	.order422	= CAM_ORDER422_8BIT_CBYCRY,
	.i2c_busnum	= IIC_NUM_CAM_USED,
	.info		= &ov9650_i2c_info,
	.pixelformat	= V4L2_PIX_FMT_VYUY,
	.srclk_name	= "mout_mpll",
	.clk_name	= "sclk_cam0",
	.clk_rate	= 24000000,
//	.line_length	= 640,
	.line_length	= 1920,
	.width		= 640,
	.height	= 480,
	.window	= {
		.left	= 0,
		.top	= 0,
		.width	= 640,
		.height= 480,
	},

	/* Polarity */
	.inv_pclk	= 0,
	.inv_vsync	= 0,
	.inv_href	= 0,
	.inv_hsync	= 0,

	.initialized	= 0,

//	.cam_power	= smdkv210_OV9650_power,
	.cam_power	= tqcam_OV9650_power,

};
#endif

#ifdef CONFIG_VIDEO_SAA7113

#define SRC_H	720
#define SRC_V	288
#define FREQ 27000000

static int tqcam_SAA7113_power(int onoff)//low_reset GPE1_4
{
	if(onoff != 0)		//on
	{
		gpio_cam_rst_cfg(GPIO_RST_7113,0);						//cam0 on
		gpio_cam_pwd_cfg(GPIO_PWD_7113,1);					//cam0 on
	}
	else											//cam1 not refrect 
	{	
		gpio_cam_rst_cfg(GPIO_RST_7113,1);						//cam0 off
		gpio_cam_pwd_cfg(GPIO_PWD_7113,0);					//cam0 off
	}	
	return 0;
}

static struct tqcam_platform_data saa7113_plat =
{
	.default_width = SRC_H,//
	.default_height = SRC_V,//
	.pixelformat = V4L2_PIX_FMT_VYUY,
	.freq = FREQ,//
	.is_mipi = 0,//
};

static struct i2c_board_info  saa7113_i2c_info = 
{
	I2C_BOARD_INFO("SAA7113", 0x4A>>1),
	.platform_data = &saa7113_plat,
};



static struct s3c_platform_camera saa7113 = {
	.id		= CAMERA_PAR_A,	//					//arch/arm/plat-s5p/include/fimc.h
	.type		= CAM_TYPE_ITU,//
//	.fmt		= ITU_601_YCBCR422_8BIT,//
	.fmt		= ITU_656_YCBCR422_8BIT,//
	.order422	= CAM_ORDER422_8BIT_CBYCRY,//
	.i2c_busnum	= IIC_NUM_CAM_USED,//
	.info		= &saa7113_i2c_info,//
	.pixelformat	= V4L2_PIX_FMT_VYUY,
	.srclk_name	= "mout_mpll",//"mout_mpll", "xusbxti",
	.clk_name	= "sclk_cam0",//"sclk_cam0","sclk_cam",	//arch/arm/mach-s5pv210/clock.c
	.clk_rate	= FREQ,
	.line_length	= SRC_H*2,
	.width		= SRC_H,
	.height	= SRC_V,
	.window	= {
		.left	= 0,
		.top	= 0,
		.width	= 640,
		.height= 480,
	},

	/* Polarity */
	.inv_pclk	= 0,
	.inv_vsync	= 1,
	.inv_href	= 0,
	.inv_hsync	= 0,

	.initialized	= 0,
	.cam_power	= tqcam_SAA7113_power,
};
#endif

/* Interface setting */
static struct s3c_platform_fimc fimc_plat_lsi = {
	.srclk_name	= "mout_mpll",
	.clk_name	= "sclk_fimc",
	.lclk_name		= "fimc",
	.clk_rate	= 166750000,
	.default_cam	= CAMERA_PAR_A,
	.camera		= {


#ifdef CONFIG_VIDEO_OV3640
		&ov3640_0,
//		&ov9650,
#endif
#ifdef CONFIG_VIDEO_OV9650
		&ov9650,
#endif
#ifdef CONFIG_VIDEO_SAA7113
		&saa7113,
#endif
#ifdef CONFIG_VIDEO_TVP5150
		&tvp5150,
#endif
#ifdef CONFIG_VIDEO_OV3640
		&ov3640_1,
#endif
		NULL,
	},
	.hw_ver		= 0x43,
};


#ifdef CONFIG_VIDEO_JPEG_V2
static struct s3c_platform_jpeg jpeg_plat __initdata = {
	.max_main_width		= 800,
	.max_main_height	= 480,
	.max_thumb_width	= 320,
	.max_thumb_height	= 240,
};
#endif

#ifdef CONFIG_DM9000
static void __init tq210_dm9000_init(void)
{
	unsigned int tmp;

	gpio_request(S5PV210_MP01(1), "nCS1");
	s3c_gpio_cfgpin(S5PV210_MP01(1), S3C_GPIO_SFN(2));
	gpio_free(S5PV210_MP01(1));

	tmp = (5 << S5P_SROM_BCX__TACC__SHIFT);
	__raw_writel(tmp, S5P_SROM_BC1);

	tmp = __raw_readl(S5P_SROM_BW);
	tmp &= (S5P_SROM_BW__CS_MASK << S5P_SROM_BW__NCS1__SHIFT);
	tmp |= (1 << S5P_SROM_BW__NCS1__SHIFT);
	__raw_writel(tmp, S5P_SROM_BW);
}
#endif
#if defined(CONFIG_TOUCHSCREEN_S3C_GX801)
void s3c_gx801_cfg_gpio(struct platform_device *dev)
{
	int err;

	s3c_gpio_cfgall_range(S5PV210_GPD1(0), 2,
			      S3C_GPIO_SFN(2), S3C_GPIO_PULL_UP);

	err = gpio_request(S5PV210_GPH1(6), "GPH1");
	if (err)
		printk(KERN_ERR "#### failed to GPH1(6) for gt801 interrupt\n");

	s3c_gpio_setpull(S5PV210_GPH1(6), S3C_GPIO_PULL_NONE);
	s3c_gpio_cfgpin(S5PV210_GPH1(6),S3C_GPIO_SFN(0xf));
	gpio_free(S5PV210_GPH1(6));
	
	err = gpio_request(S5PV210_GPD0(3), "GPD0");
	if (err)
		printk(KERN_ERR "#### failed to GPH1(6) for gt801 interrupt\n");

	s3c_gpio_setpull(S5PV210_GPD0(3), S3C_GPIO_PULL_NONE);
	s3c_gpio_cfgpin(S5PV210_GPD0(3),S3C_GPIO_SFN(0x1));
	gpio_direction_output(S5PV210_GPD0(3), 0);
	gpio_free(S5PV210_GPD0(3));
}

static struct s3c2410_platform_i2c tq210_gt801_pdata __initdata = {
   .flags        = 0,
   .slave_addr    = 0xaa>>1,
   //.slave_addr    = 0x50,
   //.frequency    = 1000*250,//100*1000
   .frequency    = 1000*100,
   .sda_delay    =  10,//10 5
   .cfg_gpio = s3c_gx801_cfg_gpio,
};
#endif

#ifdef CONFIG_MTD_NAND
static struct mtd_partition tq210_partition_info[] = {
	[0] = {
		.name		= "Bootloader",
		.offset		= 0,
		.size		= 0x100000,
		.mask_flags	= MTD_CAP_NANDFLASH,
	},
/*	[1] = {
		.name		= "Param",
		.offset		= 0x80000,
		.size		= 0x80000,
		.mask_flags	= MTD_CAP_NANDFLASH,
	},*/
	[1] = {
		.name		= "LOGO",
		.offset		= 0x100000,
		.size		= 0x200000,
		.mask_flags	= MTD_CAP_NANDFLASH,
	},
	[2] = {
		.name		= "Kernel",
		.offset		= 0x500000,
		.size		= 0x500000,
		.mask_flags	= MTD_CAP_NANDFLASH,
	},
	/*[4] = {
		.name		= "ROOTFS",
		.offset		= 0x700000,//C800000MTDPART_SIZ_FULL
		.size		= 0xc800000,//ÂÜÖÊ†∏Êñá‰ª∂Á≥ªÁªü‰∏çËÉΩÊåÇÊª°
	},*/
	/*[4] = {
		.name		= "RES",
		.offset		= 0x700000,//C800000MTDPART_SIZ_FULL
		.size		= 0x100000,//ÂÜÖÊ†∏Êñá‰ª∂Á≥ªÁªü‰∏çËÉΩÊåÇÊª°		
	},*/
	
	[3] = {
		#if defined(CONFIG_K9F2G08_SIZE)
		.name		= "ROOTFS",
		.offset		= 0x700000,
		.size		= 0xF800000,//F200000 MTDPART_SIZ_FULL F000000(240m) 0xc800000 c9 EB00000(235m) E600000(230m)F500000(245m) 100000(1m)
		//.size		= MTDPART_SIZ_FULL,
		//.size		= size-0x100000,
		#else
		.name		= "ROOTFS",
		.offset		= 0xa00000,
		//.size		= 0xF800000,//F200000 MTDPART_SIZ_FULL F000000(240m) 0xc800000 c9 EB00000(235m) E600000(230m)F500000(245m) 100000(1m)
		.size		= MTDPART_SIZ_FULL,
		//.size		= size-0x100000,

		#endif
	},
};

struct s3c_nand_mtd_info tq210_nand_info = {
	.chip_nr = 1,
	.mtd_part_nr = ARRAY_SIZE(tq210_partition_info),
	.partition = tq210_partition_info
};
#endif

#if defined(CONFIG_MFD_TPS65910)
static struct tps65910_board tq210_tps65910_pdata = {
//	.gpio_base  = S5PV210_GPH1(7),
//	.irq  		= IRQ_EINT(15),
};
#endif
#ifdef CONFIG_SND_SOC_WM8960_TQ210
#include <sound/wm8960.h>
static struct wm8960_data wm8960_pdata = {
	.capless		= 0,
	.dres			= WM8960_DRES_400R,
};
#endif
static struct i2c_board_info tq210_i2c_devs0[] __initdata = {
	{ I2C_BOARD_INFO("24c08", 0x50), },     /* Samsung S524AD0XD1 */
	{ I2C_BOARD_INFO("wm8580", 0x1b), },
#ifdef CONFIG_SND_SOC_WM8960_TQ210//lhh add
	{
		I2C_BOARD_INFO("wm8960", 0x1a),
		.platform_data  = &wm8960_pdata,
	},
#endif
#if defined(CONFIG_MFD_TPS65910) //lhh add
	{ I2C_BOARD_INFO("tps65910", 0x2d>>1),
		.platform_data  = &tq210_tps65910_pdata,	
	 },
#endif

};

static struct i2c_board_info tq210_i2c_devs1[] __initdata = {
#ifdef CONFIG_VIDEO_TV20
	{
		I2C_BOARD_INFO("s5p_ddc", (0x74>>1)),
	},
#endif	
};

static struct i2c_board_info tq210_i2c_devs2[] __initdata = {
#if defined(CONFIG_REGULATOR_MAX8698)
	{
		I2C_BOARD_INFO("max8698", 0xCC >> 1),
		.platform_data  = &tq210_max8698_pdata,
	},
#endif
#if defined(CONFIG_TOUCHSCREEN_S3C_GX801)
	{ I2C_BOARD_INFO("gx801", 0xaa>>1),
	  .irq = IRQ_EINT(14),
	  .platform_data  = &tq210_gt801_pdata,	
	},
	{ I2C_BOARD_INFO("Goodix-TS", 0x5d),
	  .irq = IRQ_EINT(14),
	  .platform_data  = &tq210_gt801_pdata,	
	},
#endif

	{
       I2C_BOARD_INFO("oz8806",0x2F),	//7-bit address of 0x5e
    },

    {
            I2C_BOARD_INFO("seaelf-charger",0x10),	//7-bit address of 0x20
            //.platform_data = &panda_seaelf_pdata,
    },



};

#ifdef CONFIG_SPI_S3C64XX

#define SMDK_MMCSPI_CS 0

static struct s3c64xx_spi_csinfo smdk_spi0_csi[] = {
	[SMDK_MMCSPI_CS] = {
		.line = S5PV210_GPB(1),
		.set_level = gpio_set_value,
		.fb_delay = 0x2,
	},
};

static struct s3c64xx_spi_csinfo smdk_spi1_csi[] = {
	[SMDK_MMCSPI_CS] = {
		.line = S5PV210_GPB(5),
		.set_level = gpio_set_value,
		.fb_delay = 0x2,
	},
};

static struct spi_board_info s3c_spi_devs[] __initdata = {
	{
		.modalias        = "spidev", /* MMC SPI */
		.mode            = SPI_MODE_0,  /* CPOL=0, CPHA=0 */
		.max_speed_hz    = 10000000,
		/* Connected to SPI-0 as 1st Slave */
		.bus_num         = 0,
		.chip_select     = 0,
		.controller_data = &smdk_spi0_csi[SMDK_MMCSPI_CS],
	},
	{
		.modalias        = "spidev", /* MMC SPI */
		.mode            = SPI_MODE_0,  /* CPOL=0, CPHA=0 */
		.max_speed_hz    = 10000000,
		/* Connected to SPI-0 as 1st Slave */
		.bus_num         = 1,
		.chip_select     = 0,
		.controller_data = &smdk_spi1_csi[SMDK_MMCSPI_CS],
	},
};

#endif

#if defined(CONFIG_TOUCHSCREEN_S3C)
static struct s3c_ts_mach_info s3c_ts_platform __initdata = {
	.delay                  = 100000,//20000 10000
	.presc                  = 49,//49
	.oversampling_shift     = 3,
	.resol_bit              = 10,
	.s3c_adc_con            = ADC_TYPE_2,
};

#define S3C_PA_ADC_1 S3C_PA_ADC+0x1000
/* Touch srcreen */
static struct resource s3c_ts_resource[] = {
	[0] = {
		.start = S3C_PA_ADC_1,
		.end   = S3C_PA_ADC_1 + SZ_4K - 1,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = IRQ_PENDN1,
		.end   = IRQ_PENDN1,
		.flags = IORESOURCE_IRQ,
	},
	[2] = {
		.start = IRQ_ADC1,
		.end   = IRQ_ADC1,
		.flags = IORESOURCE_IRQ,
	}
};

struct platform_device s3c_device_ts = {
	.name		  = "s3c_ts",
	.id		  = -1,
	.num_resources	  = ARRAY_SIZE(s3c_ts_resource),
	.resource	  = s3c_ts_resource,
};

void __init s3c_ts_set_platdata(struct s3c_ts_mach_info *pd)
{
	struct s3c_ts_mach_info *npd;

	npd = kmalloc(sizeof(*npd), GFP_KERNEL);
	if (npd) {
		memcpy(npd, pd, sizeof(*npd));
		s3c_device_ts.dev.platform_data = npd;
	} else {
		pr_err("no memory for Touchscreen platform data\n");
	}
}
#endif





static void __init tq210_map_io(void)
{
	s5p_init_io(NULL, 0, S5P_VA_CHIPID); //0xFE300000  ◊ˆ¡À“ª–©”≥…‰π§◊˜
	s3c24xx_init_clocks(24000000);
	s5pv210_gpiolib_init();

	// ‘⁄’‚¿Ô≥ı ºªØ¥Æø⁄µƒ∆ΩÃ®device
	s3c24xx_init_uarts(tq210_uartcfgs, ARRAY_SIZE(tq210_uartcfgs));

//  ’‚∏ˆ∫Í√ª”–∂®“Â
#ifndef CONFIG_S5P_HIGH_RES_TIMERS
	s5p_set_timer_source(S5P_PWM2, S5P_PWM4);
// ’‚¿Ô÷¥––¡À£¨¥”’‚¿Ôø¥”¶∏√ «2∫≈∂® ±∆˜
#endif
	s5p_reserve_bootmem(s5pv210_media_devs, ARRAY_SIZE(s5pv210_media_devs), S5P_RANGE_MFC);
#ifdef CONFIG_MTD_ONENAND
	s5pc110_device_onenand.name = "s5pc110-onenand";
#endif
#ifdef CONFIG_MTD_NAND
	s3c_device_nand.name = "s5pv210-nand";
#endif
}

#ifdef CONFIG_S3C_DEV_HSMMC
static struct s3c_sdhci_platdata smdkc110_hsmmc0_pdata __initdata = {
	.cd_type				= S3C_SDHCI_CD_INTERNAL,
	//.wp_gpio				= S5PV210_GPH0(7),
	//.has_wp_gpio			= true,
#if defined(CONFIG_S5PV210_SD_CH0_8BIT)
	.max_width				= 8,
	.host_caps				= MMC_CAP_8_BIT_DATA,
#endif
};

static struct s3c_sdhci_platdata tq210_hsmmc3_pdata __initdata = {
	.cd_type				= S3C_SDHCI_CD_PERMANENT,
	.cfg_gpio				= s5pv210_setup_sdhci3_cfg_gpio,
};      
#endif
 
static void __init smdkc110_setup_clocks(void)
{
	struct clk *pclk;
	struct clk *clk;

	/* set MMC0 clock */
	clk = clk_get(&s3c_device_hsmmc0.dev, "sclk_mmc");
	pclk = clk_get(NULL, "mout_mpll");
	clk_set_parent(clk, pclk);
	clk_set_rate(clk, 50*MHZ);

	pr_info("%s: %s: source is %s, rate is %ld\n", __func__, clk->name, clk->parent->name, clk_get_rate(clk));
}
/* USB EHCI */
static struct s5p_ehci_platdata tq210_ehci_pdata;
static void __init tq210_ehci_init(void)
{
	struct s5p_ehci_platdata *pdata = &tq210_ehci_pdata;

	s5p_ehci_set_platdata(pdata);
}

/*USB OHCI*/
static struct s5p_ohci_platdata tq210_ohci_pdata;
static void __init tq210_ohci_init(void)
{
	struct s5p_ohci_platdata *pdata = &tq210_ohci_pdata;

	s5p_ohci_set_platdata(pdata);
}

/*USB DEVICE*/
static struct s5p_otg_platdata tq210_otg_pdata;
static void __init tq210_otg_init(void)
{
	struct s5p_otg_platdata *pdata = &tq210_otg_pdata;

	s5p_otg_set_platdata(pdata);
}

static void __init tq210_machine_init(void)
{
#if defined(CONFIG_TOUCHSCREEN_S3C)
	s3c_ts_set_platdata(&s3c_ts_platform);
#endif
#if defined(CONFIG_S3C_ADC)
	s3c_adc_set_platdata(&s3c_adc_platform);
#endif
#ifdef CONFIG_MTD_NAND
	s3c_device_nand.dev.platform_data = &tq210_nand_info;
#endif
	s3c_pm_init();

#ifdef CONFIG_DM9000
	tq210_dm9000_init();
#endif
	platform_add_devices(tq210_devices, ARRAY_SIZE(tq210_devices));
	

#ifdef CONFIG_ANDROID_PMEM
	android_pmem_set_platdata();
#endif

	samsung_keypad_set_platdata(&tq210_keypad_data);
	//s3c24xx_ts_set_platdata(&s3c_ts_platform);
	//s3c24xx_ts_set_platdata(&s3c_ts_platform);


	s3c_i2c0_set_platdata(NULL);
	s3c_i2c1_set_platdata(NULL);
	s3c_i2c2_set_platdata(NULL);
	i2c_register_board_info(0, tq210_i2c_devs0, ARRAY_SIZE(tq210_i2c_devs0));
	i2c_register_board_info(1, tq210_i2c_devs1, ARRAY_SIZE(tq210_i2c_devs1));
	i2c_register_board_info(2, tq210_i2c_devs2, ARRAY_SIZE(tq210_i2c_devs2));
#ifdef CONFIG_TOUCHSCREEN_EGALAX
	i2c_register_board_info(5, i2c_devs5, ARRAY_SIZE(i2c_devs5));
#endif

	s3c_ide_set_platdata(&tq210_ide_pdata);

//	s3c_fb_set_platdata(&tq210_lcd0_pdata);
	s3c_fb_set_platdata(&EmbedSky_LCD_fb_data);

#ifdef CONFIG_S3C_DEV_HSMMC
	s3c_sdhci0_set_platdata(&smdkc110_hsmmc0_pdata);
	s3c_sdhci3_set_platdata(&tq210_hsmmc3_pdata);
#endif

#ifdef CONFIG_VIDEO_FIMC
	/* fimc */
	s3c_fimc0_set_platdata(&fimc_plat_lsi);
	s3c_fimc1_set_platdata(&fimc_plat_lsi);
	s3c_fimc2_set_platdata(&fimc_plat_lsi);
#endif
#ifdef CONFIG_VIDEO_FIMC_MIPI
	s3c_csis_set_platdata(NULL);
#endif

#ifdef CONFIG_VIDEO_JPEG_V2
	s3c_jpeg_set_platdata(&jpeg_plat);
#endif
#ifdef CONFIG_VIDEO_MFC50
        /* mfc */
        s3c_mfc_set_platdata(NULL);
#endif
        /* spi */
#ifdef CONFIG_SPI_S3C64XX
	if (!gpio_request(S5PV210_GPB(1), "SPI_CS0")) {
	    gpio_direction_output(S5PV210_GPB(1), 1);
	    s3c_gpio_cfgpin(S5PV210_GPB(1), S3C_GPIO_SFN(1));
	    s3c_gpio_setpull(S5PV210_GPB(1), S3C_GPIO_PULL_UP);
	    s5pv210_spi_set_info(0, S5PV210_SPI_SRCCLK_PCLK, ARRAY_SIZE(smdk_spi0_csi));
	}
	if (!gpio_request(S5PV210_GPB(5), "SPI_CS1")) {
	    gpio_direction_output(S5PV210_GPB(5), 1);
	    s3c_gpio_cfgpin(S5PV210_GPB(5), S3C_GPIO_SFN(1));
	    s3c_gpio_setpull(S5PV210_GPB(5), S3C_GPIO_PULL_UP);
	    s5pv210_spi_set_info(1, S5PV210_SPI_SRCCLK_PCLK, ARRAY_SIZE(smdk_spi1_csi));
	}
	spi_register_board_info(s3c_spi_devs, ARRAY_SIZE(s3c_spi_devs));
#endif

#ifdef CONFIG_BACKLIGHT_PWM
	smdk_backlight_register();
#endif
	tq210_otg_init();
	tq210_ehci_init();
	tq210_ohci_init();
	clk_xusbxti.rate = 24000000;
	 smdkc110_setup_clocks(); 
}

MACHINE_START(TQ210, "TQ210")
	/* Maintainer: Kukjin Kim <kgene.kim@samsung.com> */
	.boot_params	= S5P_PA_SDRAM + 0x100,
	.init_irq	= s5pv210_init_irq,
	.map_io		= tq210_map_io,   //’‚¿Ô∫√œÒ◊ˆ¡À“ª–©µÿ÷∑µƒ”≥…‰πÿœµ
	.init_machine	= tq210_machine_init,

// ’‚∏ˆ∫Í√ª”–∂®“Â
#ifdef CONFIG_S5P_HIGH_RES_TIMERS
        .timer          = &s5p_systimer,
#else
	.timer		= &s5p_timer,
#endif
MACHINE_END


kfree
i2c_smbus_read_byte_data
irq_desc  