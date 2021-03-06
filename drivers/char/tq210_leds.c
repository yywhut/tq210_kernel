/***********************************************************************************
* drivers/char/tq210_leds.c
* 功能简要： 
*	该驱动注册一个字符设备“/dev/led”, 用于2个LED。
* 函数简介：
*	static void tq210_debug_leds(unsigned int cmd,unsigned long arg),用于内核驱动调试
* 提供的外部接口：
*       ioctol(struct inode *inode,struct file *file,unsigned int brightness);
*	用于LED的亮，灭。
* 调用实例：
*	提供控制台，命令式的测试程序。
*
*************************************************************************************/
#include <linux/miscdevice.h>
#include <linux/input.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <asm/io.h>
#include <asm/uaccess.h>

#include <mach/map.h>
#include <mach/gpio.h>
//#include <mach/gpio-bank.h>
#include <mach/regs-gpio.h>
#include <plat/gpio-core.h>
#include <plat/gpio-cfg.h>
#include <plat/gpio-cfg-helpers.h>



#define DEVICE_NAME "led"


/* 用来指定LED所用的GPIO引脚 */
#define IOCTL_GPIO_ON	1
#define IOCTL_GPIO_OFF	0

/* 用来指定GPIO引脚的功能：输出 */
static unsigned long gpio_table [] =
{
	S5PV210_GPC0(3),
	S5PV210_GPC0(4),
};

/* 鐢ㄦ潵鎸囧畾GPIO寮曡剼鐨勫姛鑳斤細杈撳嚭 */
static unsigned int gpio_cfg_table [] =
{
	S3C_GPIO_SFN(1),
	S3C_GPIO_SFN(1),
};

//static char gpio_name[][]={{"GPC0_3"},{"GPC0_4"}};




#ifdef CONFIG_TQ210_DEBUG_LEDS
static void tq210_debug_leds(unsigned int cmd,unsigned long arg)
{
	gpio_direction_output(gpio_table[arg], cmd);
	//s3c_gpio_setpin(gpio_table[arg], cmd);
}
static void toggle_led(unsigned int cmd,unsigned long arg)
{
	int loop=0;
	printk("%s : led %ld toggle now: \n",__func__,arg);
	for(;loop<11;loop++)
	{	cmd = loop%2;
		printk("leds %d %s \n",arg+1,(cmd)?"on":"o   ff");
		tq210_debug_leds(cmd,arg);
		mdelay(1000);
	}

}
#endif

/**
*函数功能：打开/dev/led设备，设备名是：/dev/led
**/
static int tq210_gpio_open(struct inode *inode, struct file *file)
{
	int i;
	int err;
	err = gpio_request(gpio_table[0], "GPC0_3");
	if(err)
	{
		printk(KERN_ERR "failed to request GPC0_3 for LVDS PWDN pin\n");
        return err;
	}
	err = gpio_request(gpio_table[1], "GPC0_4");
	if(err)
	{
		printk(KERN_ERR "failed to request GPC0_4 for LVDS PWDN pin\n");
        return err;
	}
	printk(KERN_INFO " leds opened\n");
	for (i = 0; i < sizeof(gpio_table)/sizeof(unsigned long); i++)
	{
		s3c_gpio_cfgpin(gpio_table[i], gpio_cfg_table[i]);
		gpio_direction_output(gpio_table[i], 0);
		//s3c_gpio_setpin(gpio_table[i], 0);
	}
#ifdef CONFIG_TQ210_DEBUG_LEDS
	for (i = 0; i < sizeof(gpio_table)/sizeof(unsigned long); i++)
	{
		toggle_led(1,i);
	}
#endif
	return 0;

}

/**
*函数功能：用于控制led的亮灭 
*控制字为cmd，arg为控制哪个灯的亮灭取值范围为0-1：cmd为IOCTL_GPIO_ON时亮，cmd为IOCTL_GPIO_OFF为灭
**/
static long tq210_gpio_ioctl(
	struct inode *inode,
	struct file *file, 
	unsigned int cmd, 
	unsigned long arg)
{
	arg -= 1;
	if (arg > sizeof(gpio_table)/sizeof(unsigned long))
	{
		return -EINVAL;
	}

	switch(cmd)
	{
		case IOCTL_GPIO_ON:
			// 璁剧疆鎸囧畾寮曡剼鐨勮緭鍑虹數骞充负1
			gpio_direction_output(gpio_table[arg], 1);
			//s3c_gpio_setpin(gpio_table[arg], 1);
			return 0;

		case IOCTL_GPIO_OFF:
			// 璁剧疆鎸囧畾寮曡剼鐨勮緭鍑虹數骞充负0
			gpio_direction_output(gpio_table[arg], 0);
			//s3c_gpio_setpin(gpio_table[arg], 0);
			return 0;

		default:
			return -EINVAL;
	}
}

static int tq210_gpio_close(struct inode *inode, struct file *file)
{
	gpio_free(gpio_table[0]);
	gpio_free(gpio_table[1]);
	printk(KERN_INFO "TQ210 LEDs driver successfully close\n");
	return 0;
}

/*椹卞姩鎺ュ彛璁剧疆*/
static struct file_operations dev_fops = {
	.owner	=	THIS_MODULE,
	.ioctl	=	tq210_gpio_ioctl,
	.open = tq210_gpio_open,
	.release = tq210_gpio_close,
};
/*璁惧缁撴瀯鐨勮缃�*/
static struct miscdevice misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = DEVICE_NAME,
	.fops = &dev_fops,
};
/*鍒濆鍖栬澶囷紝閰嶇疆瀵瑰簲鐨処O锛屼互鍙婃敞鍐岃澶�*/
static int __init dev_init(void)
{
	int ret;

	int i;
	int err;
	#ifdef CONFIG_TQ210_DEBUG_LEDS
	err = gpio_request(gpio_table[0], "GPC0_3");
	if(err)
	{
		printk(KERN_ERR "failed to request GPC0_3 for LVDS PWDN pin\n");
        return err;
	}
	err = gpio_request(gpio_table[1], "GPC0_4");
	if(err)
	{
		printk(KERN_ERR "failed to request GPC0_4 for LVDS PWDN pin\n");
        return err;
	}
	for (i = 0; i < sizeof(gpio_table)/sizeof(unsigned long); i++)
	{
		//gpio_request(gpio_table[0],gpio_name[i]);
		s3c_gpio_cfgpin(gpio_table[i], gpio_cfg_table[i]);//閰嶇疆绠¤剼涓鸿緭鍑�
		gpio_direction_output(gpio_table[i], 0);
		//s3c_gpio_setpin(gpio_table[i], 0);//璁剧疆绠¤剼涓轰綆鐢靛钩
		s3c_gpio_setpull(gpio_table[i], S3C_GPIO_PULL_NONE);
	}
	#endif

	ret = misc_register(&misc);

	printk(KERN_INFO "TQ210 LEDs driver successfully probed\n");
	
	#ifdef CONFIG_TQ210_DEBUG_LEDS
	for (i = 0; i < sizeof(gpio_table)/sizeof(unsigned long); i++)
	{
		toggle_led(1,i);
	}
	#endif

	return ret;
}
/*娉ㄩ攢璁惧*/
static void __exit dev_exit(void)
{
	misc_deregister(&misc);
	gpio_free(gpio_table[0]);
	gpio_free(gpio_table[1]);
	printk(KERN_INFO "TQ210 LEDs driver successfully exit\n");
}

module_init(dev_init);
module_exit(dev_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("www.embedsky.com");
MODULE_DESCRIPTION("LEDS' Driver");
