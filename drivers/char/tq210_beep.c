/***********************************************************************************
* drivers/char/tq210_beep.c
* 功能简要： 
*	该驱动注册一个字符设备“/dev/EmbedSky-beep”, 用于控制蜂鸣器。
* 提供的接口：
*       ioctol(struct inode *inode,struct file *file,unsigned int brightness);
*	用于调控蜂鸣器。改蜂鸣器的有效调整值为［1，100］
*	在驱动中是以固定的频率，仅通过调整有效时序宽度来控制蜂鸣器。
* 调用实例：
*	提供控制台，命令式的测试程序。
*	提供QT4界面化的测试程序
*
*************************************************************************************/
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/clk.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <linux/miscdevice.h>

#include <mach/map.h>
#include <mach/gpio.h>
//#include <mach/gpio-bank.h>
#include <mach/regs-gpio.h>
#include <plat/gpio-core.h>
#include <plat/gpio-cfg.h>
#include <plat/gpio-cfg-helpers.h>
#include <plat/regs-timer.h>
#include <plat/clock.h>

#define DEVICE_NAME "beep"


#define PRESCALER (255-1)

#define BEEP_ON  1
#define BEEP_OFF 0

//#define CONFIG_TQ210_DEBUG_BEEP

volatile int *clk = NULL;

#if 0
//#ifdef CONFIG_S3C210_PWM
#define MAX_BEEP   0x64  //100
#define HZ_TO_NANOSECONDS(x) (1000000000UL/(x))
struct pwm_beeper {
	struct pwm_device *pwm;
	int period;
	int step_ns;
};
static struct  pwm_beeper *beeper;
#endif

/* 用来指定LED所用的GPIO引脚 */
static unsigned long gpio_table [] =
{
	S5PV210_GPD0(1),
};

/* 用来指定GPIO引脚的功能：输出 */
static unsigned int gpio_cfg_table [] =
{
	S3C_GPIO_SFN(2),
};


/*
*函数用于在设定好定时器1的PWM模式后，
*开始生效定时器1
*/
static void s3c210_beep_start(void)
{
//#ifdef CONFIG_S3C210_PWM
#if 0
	s3c_gpio_cfgpin(S3C64XX_GPF(15),S3C64XX_GPF15_PWM_TOUT1);
	pwm_enable(beeper->pwm);
#else
	unsigned long tcon;
	unsigned char i;
	
	for (i = 0; i < sizeof(gpio_table)/sizeof(unsigned long); i++)
	{
		s3c_gpio_cfgpin(gpio_table[i], gpio_cfg_table[i]);
		s3c_gpio_setpull(gpio_table[i], S3C_GPIO_PULL_NONE);
	}
	
        tcon = __raw_readl(S3C2410_TCON);//读取时钟控制寄存器
        tcon |= S3C2410_TCON_T1START;//开始位置1,让定时器开始工作 定时器1
        tcon &= ~S3C2410_TCON_T1MANUALUPD;//之前已经设置好了TCNTB1 以及 TCMPB1，这里无须任何操作
        __raw_writel(tcon, S3C2410_TCON);//将设置写回寄存器
#endif

}
static void s3c210_beep_off(void)
{
#if 0
//#ifdef CONFIG_S3C210_PWM
	s3c_gpio_cfgpin(S3C64XX_GPF(15),S3C64XX_GPF15_INPUT);
	pwm_disable(beeper->pwm);
#else
        unsigned long tcon;
        int err;
      
	//将GPD[1]引脚设置为输入
	gpio_free(gpio_table [0]);
	err = gpio_request(gpio_table[0], "GPD0_1");
	if(err)
	{
		printk(KERN_ERR "failed to request GPD0_1 for LVDS PWDN pin\n");
        //return err;
	}
	s3c_gpio_cfgpin(gpio_table [0],S3C_GPIO_SFN(0));
	gpio_direction_output(gpio_table [0],0);
	//s3c_gpio_setpin(gpio_table [0],0);
	//将定时器控制寄存器中的TIMER1开始位设置为0
	tcon = __raw_readl(S3C2410_TCON);
	tcon &= ~S3C2410_TCON_T1START;
	tcon &= ~S3C2410_TCON_T1MANUALUPD;//停止更新功能
	__raw_writel(tcon, S3C2410_TCON); //stop the timer1
#endif
}

#if 0
static void s3c210_set_timer1(unsigned long Val)
{
#if 0
//#ifdef CONFIG_S3C210_PWM
	s3c_gpio_cfgpin(S3C64XX_GPF(15),S3C64XX_GPF15_PWM_TOUT1);
	pwm_config(beeper->pwm, beeper->step_ns*Val, beeper->period);
#else
        unsigned long tcon;//用于存放时钟控制寄存器的数值
        unsigned long tcnt;//用于存放TCNTB1的数值
        unsigned long tcmp;//用于存放TCMPB1的数值
        unsigned long tcfg1;//用于存放定时器配置寄存器1的数值
        unsigned long tcfg0;//用于存放定时器配置寄存器0的数值
	unsigned char i;
      
	 tcnt = 0xffffffff;  /* 默认的TCTB1数值*/

        /* 读取TCON，TCFG0以及TCFG1寄存器的数值*/
        tcon = __raw_readl(S3C2410_TCON);
        tcfg1 =__raw_readl(S3C2410_TCFG1);
        tcfg0 =__raw_readl(S3C2410_TCFG0);

	/*定时器输入频率 = PCLK / ( {预分频数值 + 1} ) / {分割数值}
	*{预分频数值} = 1~255，由TCFG0配置寄存器来配置
	*{分割数值} = 1, 2, 4, 8, 16, TCLK，由TCFG1配置寄存器来配置
	*/
      
	//配置GPF15为输出
	for (i = 0; i < sizeof(gpio_table)/sizeof(unsigned long); i++)
	{
		s3c_gpio_cfgpin(gpio_table[i], gpio_cfg_table[i]);
		s3c_gpio_setpull(gpio_table[i], S3C_GPIO_PULL_NONE);
	}
	//清零TCFG1[4:7]
        tcfg1 &= ~S3C2410_TCFG1_MUX1_MASK;
	//设置分割值为2
        tcfg1 |= S3C2410_TCFG1_MUX1_DIV2;//set [4:7]== 1/2
	//清零预分频位TCFG0[0:7]
        tcfg0 &= ~S3C2410_TCFG_PRESCALER0_MASK;
	//设置预分频数值，这里是254
        tcfg0 |= (PRESCALER) << 0;
	//清零TCON[8:10]
        tcon &= ~(7<<8); //set bit [8:10] to zero
	//设置定时器工作模式为自动加载模式(auto-reload)
        tcon |= S3C2410_TCON_T1RELOAD;
	//将配置好的TCON，TCFG0，TCFG1的数值写回寄存器
	__raw_writel(tcfg1, S3C2410_TCFG1);
	__raw_writel(tcfg0, S3C2410_TCFG0);
	__raw_writel(tcon, S3C2410_TCON);
	
	//准备更新TCMPB1数值，先置1 TCON[9]
	tcon |= S3C2410_TCON_T1MANUALUPD;
	__raw_writel(tcon, S3C2410_TCON);	
	//TCNTB1==200，改数值决定了PWM的频率
	tcnt = 101;
	 __raw_writel(tcnt, S3C2410_TCNTB(1));
	//改变TCMPB1的数值，改数值决定PWM的频宽
	tcmp = Val;
	__raw_writel(tcmp, S3C2410_TCMPB(1));
#endif
}
#elif 1
static void s3c210_set_timer1(unsigned long Val)
{
#if 0
//#ifdef CONFIG_S3C210_PWM
	s3c_gpio_cfgpin(S3C64XX_GPF(15),S3C64XX_GPF15_PWM_TOUT1);
	pwm_config(beeper->pwm, beeper->step_ns*Val, beeper->period);
#else
        unsigned long tcon;//用于存放时钟控制寄存器的数值
        unsigned long tcnt;//用于存放TCNTB1的数值
        unsigned long tcmp;//用于存放TCMPB1的数值
        unsigned long tcfg1;//用于存放定时器配置寄存器1的数值
        unsigned long tcfg0;//用于存放定时器配置寄存器0的数值
	unsigned char i;
      
	 tcnt = 0xffffffff;  /* 默认的TCTB1数值*/

        /* 读取TCON，TCFG0以及TCFG1寄存器的数值*/
        tcon = __raw_readl(S3C2410_TCON);
        tcfg1 =__raw_readl(S3C2410_TCFG1);
        tcfg0 =__raw_readl(S3C2410_TCFG0);

	/*定时器输入频率 = PCLK / ( {预分频数值 + 1} ) / {分割数值}
	*{预分频数值} = 1~255，由TCFG0配置寄存器来配置
	*{分割数值} = 1, 2, 4, 8, 16, TCLK，由TCFG1配置寄存器来配置
	*/
      
	//配置GPF15为输出
	for (i = 0; i < sizeof(gpio_table)/sizeof(unsigned long); i++)
	{
		s3c_gpio_cfgpin(gpio_table[i], gpio_cfg_table[i]);
		s3c_gpio_setpull(gpio_table[i], S3C_GPIO_PULL_NONE);
	}
	//清零TCFG1[4:7]
        tcfg1 &= ~S3C2410_TCFG1_MUX1_MASK;
	//设置分割值为2
        tcfg1 |= S3C2410_TCFG1_MUX1_DIV2;//set [4:7]== 1/2
	//清零预分频位TCFG0[0:7]
        tcfg0 &= ~S3C2410_TCFG_PRESCALER0_MASK;
	//设置预分频数值，这里是254
 //       tcfg0 |= (PRESCALER) << 0;//backlight pwm0
	//清零TCON[8:10]
        tcon &= ~(7<<8); //set bit [8:10] to zero
	//设置定时器工作模式为自动加载模式(auto-reload)
        tcon |= S3C2410_TCON_T1RELOAD;
	//将配置好的TCON，TCFG0，TCFG1的数值写回寄存器
	__raw_writel(tcfg1, S3C2410_TCFG1);
//	__raw_writel(tcfg0, S3C2410_TCFG0);//backlight pwm0 div
	__raw_writel(tcon, S3C2410_TCON);
	
	//准备更新TCMPB1数值，先置1 TCON[9]
	tcon |= S3C2410_TCON_T1MANUALUPD;
	__raw_writel(tcon, S3C2410_TCON);	
	//TCNTB1==200，改数值决定了PWM的频率
	tcnt = 101*255;
	 __raw_writel(tcnt, S3C2410_TCNTB(1));
	//改变TCMPB1的数值，改数值决定PWM的频宽
	tcmp = Val*255;
	__raw_writel(tcmp, S3C2410_TCMPB(1));
#endif
}

#endif
/*
*函数用于在更新定时器1的TCTB1以及TCMPB1的数值，
*通过更新
*/
static long tq210_beep_ioctl(struct file *file,unsigned int CMD_ON_OFF, unsigned long Val)
{

	if(CMD_ON_OFF<=0)
	{
		s3c210_beep_off();
	}
	else//更新TCTB1，TCMPB1的数值
	{
		s3c210_set_timer1(Val);
		//重新开始启动定时器
		s3c210_beep_start();
	}
	return 0;
}
#ifdef CONFIG_TQ210_DEBUG_BEEP
/*
*函数用于在更新定时器1的TCTB1以及TCMPB1的数值，
*通过更新
*/
static int tq210_beep_debug(unsigned int CMD_ON_OFF, unsigned long Val)
{
	
	if(CMD_ON_OFF<=0)
	{
		s3c210_beep_off();
	}
	else//更新TCTB1，TCMPB1的数值
	{
		s3c210_set_timer1(Val);
		//开始启动定时器
		s3c210_beep_start();
	}
	return 0;
}
#endif
//when open beep device, this function will be called
static int tq210_beep_open(struct inode *inode, struct file *file)
{
	unsigned long pclk;//
	struct clk *clk_p;
	printk(KERN_INFO " beep opened\n");
#ifdef CONFIG_TQ210_DEBUG_BEEP
	printk(" beep value is : 20.....\n\n");	
	tq210_beep_debug(BEEP_ON, 20);
	mdelay(1000);
	printk(" beep value is : 40.....\n\n");
	tq210_beep_debug(BEEP_ON, 40);
	mdelay(1000);
	printk(" beep value is : 80.....\n\n");
	tq210_beep_debug(BEEP_ON, 80);
	mdelay(1000);
	printk(" beep value is : 100.....\n\n");
	tq210_beep_debug(BEEP_ON, 100);
	mdelay(2000);
	printk(" beep off.....\n\n");
	tq210_beep_debug(BEEP_OFF, 100);//off
	printk(KERN_INFO " beep opened done.....\n\n");
#endif
	clk_p = clk_get(NULL, "pclk");
	pclk  = clk_get_rate(clk_p);//得到时钟频率
	printk("%s : pclk=0x%lx\n", __func__,pclk);

	return 0;

}
/*关闭设备的接口*/
static int tq210_adc_close(struct inode *inode, struct file *file)
{
	s3c210_beep_off();
	return 0;
}
/*接口注册*/
static struct file_operations s3c210_beep_fops=
{
        .owner=THIS_MODULE,
        .unlocked_ioctl=tq210_beep_ioctl,
	.open = tq210_beep_open,
	.release 	= tq210_adc_close,
};
static struct miscdevice tq210_beep_miscdev = {
	.minor		= MISC_DYNAMIC_MINOR,
	.name		=  DEVICE_NAME,
	.fops		= &s3c210_beep_fops,
};
/*设备初始化函数*/
static int s3c210_beep_init(void)
{
	int ret,error;
#if 0
//#ifdef CONFIG_S3C64XX_PWM
	beeper = kzalloc(sizeof(*beeper), GFP_KERNEL);
	if (!beeper)
		return -ENOMEM;
	beeper->pwm = pwm_request(1, "pwm-beeper");

	if (IS_ERR(beeper->pwm)) {
		error = PTR_ERR(beeper->pwm);
		printk("Failed to request pwm device for Beeper: %d\n", error);
		goto err_free;
	}
	beeper->period = 1000000;
	beeper->step_ns= (beeper->period/MAX_BEEP);
#endif
	
	clk = (int *)ioremap(0xE010046C, 4);
	//printk(" pre:%x\n ",*clk);
	*clk |= (1<<23);
	//printk(" last:%x\n ",*clk);
	ret = misc_register(&tq210_beep_miscdev);
	

	if(ret<0)
	{
		printk(DEVICE_NAME "can't register major number\n");
		return ret;
	}
	printk(KERN_INFO "TQ210 Beep driver successfully probed\n");
	
	#ifdef CONFIG_TQ210_DEBUG_BEEP
	printk(" beep value is : 20.....\n\n");	
	tq210_beep_debug(BEEP_ON, 20);
	mdelay(1000);
	printk(" beep value is : 40.....\n\n");
	tq210_beep_debug(BEEP_ON, 40);
	mdelay(1000);
	printk(" beep value is : 80.....\n\n");
	tq210_beep_debug(BEEP_ON, 80);
	mdelay(1000);
	printk(" beep value is : 100.....\n\n");
	tq210_beep_debug(BEEP_ON, 100);
	mdelay(2000);
	printk(" beep off.....\n\n");
	tq210_beep_debug(BEEP_OFF, 100);//off
	printk(KERN_INFO " beep opened done.....\n\n");
#endif

	return 0;
#if 0
//#ifdef CONFIG_S3C64XX_PWM
err_free:
	kfree(beeper);
#endif
	//return error;
}

/*卸载函数*/
static void s3c210_beep_exit(void)
{
	misc_deregister(&tq210_beep_miscdev);
	printk("Goodbye EmbedSky-beep module !\n");
} 



module_init(s3c210_beep_init);
module_exit(s3c210_beep_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Paul <shufexiu@163.com>");
MODULE_DESCRIPTION("PWM based beep Driver");
