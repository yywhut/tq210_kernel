/***********************************************************************************
* drivers/char/tq210_beep.c
* ���ܼ�Ҫ�� 
*	������ע��һ���ַ��豸��/dev/EmbedSky-beep��, ���ڿ��Ʒ�������
* �ṩ�Ľӿڣ�
*       ioctol(struct inode *inode,struct file *file,unsigned int brightness);
*	���ڵ��ط��������ķ���������Ч����ֵΪ��1��100��
*	�����������Թ̶���Ƶ�ʣ���ͨ��������Чʱ���������Ʒ�������
* ����ʵ����
*	�ṩ����̨������ʽ�Ĳ��Գ���
*	�ṩQT4���滯�Ĳ��Գ���
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

/* ����ָ��LED���õ�GPIO���� */
static unsigned long gpio_table [] =
{
	S5PV210_GPD0(1),
};

/* ����ָ��GPIO���ŵĹ��ܣ���� */
static unsigned int gpio_cfg_table [] =
{
	S3C_GPIO_SFN(2),
};


/*
*�����������趨�ö�ʱ��1��PWMģʽ��
*��ʼ��Ч��ʱ��1
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
	
        tcon = __raw_readl(S3C2410_TCON);//��ȡʱ�ӿ��ƼĴ���
        tcon |= S3C2410_TCON_T1START;//��ʼλ��1,�ö�ʱ����ʼ���� ��ʱ��1
        tcon &= ~S3C2410_TCON_T1MANUALUPD;//֮ǰ�Ѿ����ú���TCNTB1 �Լ� TCMPB1�����������κβ���
        __raw_writel(tcon, S3C2410_TCON);//������д�ؼĴ���
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
      
	//��GPD[1]��������Ϊ����
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
	//����ʱ�����ƼĴ����е�TIMER1��ʼλ����Ϊ0
	tcon = __raw_readl(S3C2410_TCON);
	tcon &= ~S3C2410_TCON_T1START;
	tcon &= ~S3C2410_TCON_T1MANUALUPD;//ֹͣ���¹���
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
        unsigned long tcon;//���ڴ��ʱ�ӿ��ƼĴ�������ֵ
        unsigned long tcnt;//���ڴ��TCNTB1����ֵ
        unsigned long tcmp;//���ڴ��TCMPB1����ֵ
        unsigned long tcfg1;//���ڴ�Ŷ�ʱ�����üĴ���1����ֵ
        unsigned long tcfg0;//���ڴ�Ŷ�ʱ�����üĴ���0����ֵ
	unsigned char i;
      
	 tcnt = 0xffffffff;  /* Ĭ�ϵ�TCTB1��ֵ*/

        /* ��ȡTCON��TCFG0�Լ�TCFG1�Ĵ�������ֵ*/
        tcon = __raw_readl(S3C2410_TCON);
        tcfg1 =__raw_readl(S3C2410_TCFG1);
        tcfg0 =__raw_readl(S3C2410_TCFG0);

	/*��ʱ������Ƶ�� = PCLK / ( {Ԥ��Ƶ��ֵ + 1} ) / {�ָ���ֵ}
	*{Ԥ��Ƶ��ֵ} = 1~255����TCFG0���üĴ���������
	*{�ָ���ֵ} = 1, 2, 4, 8, 16, TCLK����TCFG1���üĴ���������
	*/
      
	//����GPF15Ϊ���
	for (i = 0; i < sizeof(gpio_table)/sizeof(unsigned long); i++)
	{
		s3c_gpio_cfgpin(gpio_table[i], gpio_cfg_table[i]);
		s3c_gpio_setpull(gpio_table[i], S3C_GPIO_PULL_NONE);
	}
	//����TCFG1[4:7]
        tcfg1 &= ~S3C2410_TCFG1_MUX1_MASK;
	//���÷ָ�ֵΪ2
        tcfg1 |= S3C2410_TCFG1_MUX1_DIV2;//set [4:7]== 1/2
	//����Ԥ��ƵλTCFG0[0:7]
        tcfg0 &= ~S3C2410_TCFG_PRESCALER0_MASK;
	//����Ԥ��Ƶ��ֵ��������254
        tcfg0 |= (PRESCALER) << 0;
	//����TCON[8:10]
        tcon &= ~(7<<8); //set bit [8:10] to zero
	//���ö�ʱ������ģʽΪ�Զ�����ģʽ(auto-reload)
        tcon |= S3C2410_TCON_T1RELOAD;
	//�����úõ�TCON��TCFG0��TCFG1����ֵд�ؼĴ���
	__raw_writel(tcfg1, S3C2410_TCFG1);
	__raw_writel(tcfg0, S3C2410_TCFG0);
	__raw_writel(tcon, S3C2410_TCON);
	
	//׼������TCMPB1��ֵ������1 TCON[9]
	tcon |= S3C2410_TCON_T1MANUALUPD;
	__raw_writel(tcon, S3C2410_TCON);	
	//TCNTB1==200������ֵ������PWM��Ƶ��
	tcnt = 101;
	 __raw_writel(tcnt, S3C2410_TCNTB(1));
	//�ı�TCMPB1����ֵ������ֵ����PWM��Ƶ��
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
        unsigned long tcon;//���ڴ��ʱ�ӿ��ƼĴ�������ֵ
        unsigned long tcnt;//���ڴ��TCNTB1����ֵ
        unsigned long tcmp;//���ڴ��TCMPB1����ֵ
        unsigned long tcfg1;//���ڴ�Ŷ�ʱ�����üĴ���1����ֵ
        unsigned long tcfg0;//���ڴ�Ŷ�ʱ�����üĴ���0����ֵ
	unsigned char i;
      
	 tcnt = 0xffffffff;  /* Ĭ�ϵ�TCTB1��ֵ*/

        /* ��ȡTCON��TCFG0�Լ�TCFG1�Ĵ�������ֵ*/
        tcon = __raw_readl(S3C2410_TCON);
        tcfg1 =__raw_readl(S3C2410_TCFG1);
        tcfg0 =__raw_readl(S3C2410_TCFG0);

	/*��ʱ������Ƶ�� = PCLK / ( {Ԥ��Ƶ��ֵ + 1} ) / {�ָ���ֵ}
	*{Ԥ��Ƶ��ֵ} = 1~255����TCFG0���üĴ���������
	*{�ָ���ֵ} = 1, 2, 4, 8, 16, TCLK����TCFG1���üĴ���������
	*/
      
	//����GPF15Ϊ���
	for (i = 0; i < sizeof(gpio_table)/sizeof(unsigned long); i++)
	{
		s3c_gpio_cfgpin(gpio_table[i], gpio_cfg_table[i]);
		s3c_gpio_setpull(gpio_table[i], S3C_GPIO_PULL_NONE);
	}
	//����TCFG1[4:7]
        tcfg1 &= ~S3C2410_TCFG1_MUX1_MASK;
	//���÷ָ�ֵΪ2
        tcfg1 |= S3C2410_TCFG1_MUX1_DIV2;//set [4:7]== 1/2
	//����Ԥ��ƵλTCFG0[0:7]
        tcfg0 &= ~S3C2410_TCFG_PRESCALER0_MASK;
	//����Ԥ��Ƶ��ֵ��������254
 //       tcfg0 |= (PRESCALER) << 0;//backlight pwm0
	//����TCON[8:10]
        tcon &= ~(7<<8); //set bit [8:10] to zero
	//���ö�ʱ������ģʽΪ�Զ�����ģʽ(auto-reload)
        tcon |= S3C2410_TCON_T1RELOAD;
	//�����úõ�TCON��TCFG0��TCFG1����ֵд�ؼĴ���
	__raw_writel(tcfg1, S3C2410_TCFG1);
//	__raw_writel(tcfg0, S3C2410_TCFG0);//backlight pwm0 div
	__raw_writel(tcon, S3C2410_TCON);
	
	//׼������TCMPB1��ֵ������1 TCON[9]
	tcon |= S3C2410_TCON_T1MANUALUPD;
	__raw_writel(tcon, S3C2410_TCON);	
	//TCNTB1==200������ֵ������PWM��Ƶ��
	tcnt = 101*255;
	 __raw_writel(tcnt, S3C2410_TCNTB(1));
	//�ı�TCMPB1����ֵ������ֵ����PWM��Ƶ��
	tcmp = Val*255;
	__raw_writel(tcmp, S3C2410_TCMPB(1));
#endif
}

#endif
/*
*���������ڸ��¶�ʱ��1��TCTB1�Լ�TCMPB1����ֵ��
*ͨ������
*/
static long tq210_beep_ioctl(struct file *file,unsigned int CMD_ON_OFF, unsigned long Val)
{

	if(CMD_ON_OFF<=0)
	{
		s3c210_beep_off();
	}
	else//����TCTB1��TCMPB1����ֵ
	{
		s3c210_set_timer1(Val);
		//���¿�ʼ������ʱ��
		s3c210_beep_start();
	}
	return 0;
}
#ifdef CONFIG_TQ210_DEBUG_BEEP
/*
*���������ڸ��¶�ʱ��1��TCTB1�Լ�TCMPB1����ֵ��
*ͨ������
*/
static int tq210_beep_debug(unsigned int CMD_ON_OFF, unsigned long Val)
{
	
	if(CMD_ON_OFF<=0)
	{
		s3c210_beep_off();
	}
	else//����TCTB1��TCMPB1����ֵ
	{
		s3c210_set_timer1(Val);
		//��ʼ������ʱ��
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
	pclk  = clk_get_rate(clk_p);//�õ�ʱ��Ƶ��
	printk("%s : pclk=0x%lx\n", __func__,pclk);

	return 0;

}
/*�ر��豸�Ľӿ�*/
static int tq210_adc_close(struct inode *inode, struct file *file)
{
	s3c210_beep_off();
	return 0;
}
/*�ӿ�ע��*/
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
/*�豸��ʼ������*/
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

/*ж�غ���*/
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
