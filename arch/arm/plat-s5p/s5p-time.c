/* linux/arch/arm/plat-s5p/s5p-time.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * S5P - Common hr-timer support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/clockchips.h>
#include <linux/platform_device.h>

#include <asm/smp_twd.h>
#include <asm/mach/time.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/sched_clock.h>

#include <mach/map.h>
#include <plat/devs.h>
#include <plat/regs-timer.h>
#include <plat/s5p-time.h>


//  这个文件是有备编译的

static struct clk *tin_event;
static struct clk *tin_source;
static struct clk *tdiv_event;
static struct clk *tdiv_source;
static struct clk *timerclk;
static struct s5p_timer_source timer_source;
static unsigned long clock_count_per_tick;
static void s5p_timer_resume(void);

static void s5p_time_stop(enum s5p_timer_mode mode)
{
	unsigned long tcon;

	tcon = __raw_readl(S3C2410_TCON);

	switch (mode) {
	case S5P_PWM0:
		tcon &= ~S3C2410_TCON_T0START;
		break;

	case S5P_PWM1:
		tcon &= ~S3C2410_TCON_T1START;
		break;

	case S5P_PWM2:
		tcon &= ~S3C2410_TCON_T2START;
		break;

	case S5P_PWM3:
		tcon &= ~S3C2410_TCON_T3START;
		break;

	case S5P_PWM4:
		tcon &= ~S3C2410_TCON_T4START;
		break;

	default:
		printk(KERN_ERR "Invalid Timer %d\n", mode);
		break;
	}
	__raw_writel(tcon, S3C2410_TCON);
}

static void s5p_time_setup(enum s5p_timer_mode mode, unsigned long tcnt)
{
	unsigned long tcon;

	tcon = __raw_readl(S3C2410_TCON);

		//S3C2410_TCON fd300008  这个应该是虚拟地址
	// tcon 54140d

	tcnt--;

	switch (mode) {
	case S5P_PWM0:
		tcon &= ~(0x0f << 0);
		tcon |= S3C2410_TCON_T0MANUALUPD;
		break;

	case S5P_PWM1:
		tcon &= ~(0x0f << 8);
		tcon |= S3C2410_TCON_T1MANUALUPD;
		break;

	case S5P_PWM2:
		tcon &= ~(0x0f << 12);
		tcon |= S3C2410_TCON_T2MANUALUPD;
		break;

	case S5P_PWM3:
		tcon &= ~(0x0f << 16);
		tcon |= S3C2410_TCON_T3MANUALUPD;
		break;

	case S5P_PWM4:
		tcon &= ~(0x07 << 20);
		tcon |= S3C2410_TCON_T4MANUALUPD;
		break;

	default:
		printk(KERN_ERR "Invalid Timer %d\n", mode);
		break;
	}


	//mode 2
		//S3C2410_TCNTB(mode) fd300024
		//S3C2410_TCMPB(mode) fd300028

	//mode 4
			//S3C2410_TCNTB(mode) fd30003c
			//S3C2410_TCMPB(mode) fd300040
	__raw_writel(tcnt, S3C2410_TCNTB(mode));
	__raw_writel(tcnt, S3C2410_TCMPB(mode));
	__raw_writel(tcon, S3C2410_TCON);
}

static void s5p_time_start(enum s5p_timer_mode mode, bool periodic)
{
	unsigned long tcon;

	tcon  = __raw_readl(S3C2410_TCON);

	switch (mode) {
	case S5P_PWM0:
		tcon |= S3C2410_TCON_T0START;
		tcon &= ~S3C2410_TCON_T0MANUALUPD;

		if (periodic)
			tcon |= S3C2410_TCON_T0RELOAD;
		else
			tcon &= ~S3C2410_TCON_T0RELOAD;
		break;

	case S5P_PWM1:
		tcon |= S3C2410_TCON_T1START;
		tcon &= ~S3C2410_TCON_T1MANUALUPD;

		if (periodic)
			tcon |= S3C2410_TCON_T1RELOAD;
		else
			tcon &= ~S3C2410_TCON_T1RELOAD;
		break;

	case S5P_PWM2:
		tcon |= S3C2410_TCON_T2START;
		tcon &= ~S3C2410_TCON_T2MANUALUPD;

		if (periodic)
			tcon |= S3C2410_TCON_T2RELOAD;
		else
			tcon &= ~S3C2410_TCON_T2RELOAD;
		break;

	case S5P_PWM3:
		tcon |= S3C2410_TCON_T3START;
		tcon &= ~S3C2410_TCON_T3MANUALUPD;

		if (periodic)
			tcon |= S3C2410_TCON_T3RELOAD;
		else
			tcon &= ~S3C2410_TCON_T3RELOAD;
		break;

	case S5P_PWM4:
		tcon |= S3C2410_TCON_T4START;
		tcon &= ~S3C2410_TCON_T4MANUALUPD;

		if (periodic)
			tcon |= S3C2410_TCON_T4RELOAD;
		else
			tcon &= ~S3C2410_TCON_T4RELOAD;
		break;

	default:
		printk(KERN_ERR "Invalid Timer %d\n", mode);
		break;
	}
	__raw_writel(tcon, S3C2410_TCON);
}

static int s5p_set_next_event(unsigned long cycles,
				struct clock_event_device *evt)
{
	s5p_time_setup(timer_source.event_id, cycles);
	s5p_time_start(timer_source.event_id, NON_PERIODIC);

	return 0;
}

static void s5p_set_mode(enum clock_event_mode mode,
				struct clock_event_device *evt)
{
	s5p_time_stop(timer_source.event_id);

	switch (mode) {
	case CLOCK_EVT_MODE_PERIODIC:
		s5p_time_setup(timer_source.event_id, clock_count_per_tick);
		s5p_time_start(timer_source.event_id, PERIODIC);
		break;

	case CLOCK_EVT_MODE_ONESHOT:
		break;

	case CLOCK_EVT_MODE_UNUSED:
	case CLOCK_EVT_MODE_SHUTDOWN:
		break;

	case CLOCK_EVT_MODE_RESUME:
		s5p_timer_resume();
		break;
	}
}

static void s5p_timer_resume(void)
{
	/* event timer restart */
	s5p_time_setup(timer_source.event_id, clock_count_per_tick);
	s5p_time_start(timer_source.event_id, PERIODIC);

	/* source timer restart */
	s5p_time_setup(timer_source.source_id, TCNT_MAX);
	s5p_time_start(timer_source.source_id, PERIODIC);
}

void __init s5p_set_timer_source(enum s5p_timer_mode event,
				 enum s5p_timer_mode source)
{
	s3c_device_timer[event].dev.bus = &platform_bus_type;
	s3c_device_timer[source].dev.bus = &platform_bus_type;

	timer_source.event_id = event;   //2号定时器
	timer_source.source_id = source;	// 4号定时器
}

/*
set_next_event：设置事件发生的时间
event_handler：事件发生时要采取的动作
feature：时钟事件设备的特性。有两类比较典型的：
			CLOCK_EVT_FEAT_PERIODIC表示支持周期性事件，
			CLOCK_EVT_FEAT_ONESHOT表示支持单触发事件
set_mode：用于设置模式的函数
mult，shift：乘数和位移数，用于在时钟后期和ns之间进行转换
rating：表示其质量
broadcast：广播
*/

/*
随着通用时钟框架的引入，内核需要支持高精度的定时器，为此，
通用时间框架为定时器硬件定义了一个标准的接口：clock_event_device，
machine级的代码只要按这个标准接口实现相应的硬件控制功能，
剩下的与平台无关的特性则统一由通用时间框架层来实现

。在smp系统中，为了减少处理器间的通信开销，基本上每个cpu都会具备一个属于自己的本地clock_event_device，
独立地为该cpu提供时钟事件服务，smp中的每个cpu基于本地的
clock_event_device，建立自己的tick_device，普通定时器和高精度定时器
ick_device是基于clock_event_device的进一步封装，用于代替原有的时钟滴答中断，
给内核提供tick事件，以完成进程的调度和进程信息统计，负载平衡和时间更新等操作。
*/
static struct clock_event_device time_event_device = {
	.name		= "s5p_event_timer",
	.features	= CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT,
	.rating		= 200,
	.set_next_event	= s5p_set_next_event,  //设置事件发生的时间
	.set_mode	= s5p_set_mode,
};

// 这里就是timer2的中断处，频率为200hz
// 可以看到 在这里 jiffies is    4294907561  在不停的加1.
// 具体的handler会动态的 到底是 period 还是one shot

static irqreturn_t s5p_clock_event_isr(int irq, void *dev_id)
{
	struct clock_event_device *evt = dev_id;
	//printk("jiffies is    %lu \n",jiffies);
	evt->event_handler(evt);    //这里究竟指向谁会动态变化

	return IRQ_HANDLED;
}

static struct irqaction s5p_clock_event_irq = {
	.name		= "s5p_time_irq",
	.flags		= IRQF_DISABLED | IRQF_TIMER | IRQF_IRQPOLL,
	.handler	= s5p_clock_event_isr,
	.dev_id		= &time_event_device,
};

static void __init s5p_clockevent_init(void)
{
	unsigned long pclk;
	unsigned long clock_rate;
	unsigned int irq_number;
	struct clk *tscaler;

	pclk = clk_get_rate(timerclk);
	// pclk  66700000 
	tscaler = clk_get_parent(tdiv_event);

	clk_set_rate(tscaler, pclk / 2);
	clk_set_rate(tdiv_event, pclk / 2);
	clk_set_parent(tin_event, tdiv_event);

	clock_rate = clk_get_rate(tin_event);
	// clock_rate  33350000 
	clock_count_per_tick = clock_rate / HZ;
//clock_count_per_tick  166750 
//  也就是 166750次clcok是一个tick
//HZ 是200  每s钟200个周期 也就是200个tick


	clockevents_calc_mult_shift(&time_event_device,
				    clock_rate, S5PTIMER_MIN_RANGE);
	time_event_device.max_delta_ns =
		clockevent_delta2ns(-1, &time_event_device);
	time_event_device.min_delta_ns =
		clockevent_delta2ns(1, &time_event_device);

	time_event_device.cpumask = cpumask_of(0);

	//用于向系统添加一个时钟事件设备。
	//所有注册的时钟事件设备都会被存放在链表clockevent_devices中。
	// 不但注册了evend还发了一个通知，增加了tick device
	clockevents_register_device(&time_event_device);

	// IRQ_TIMER0 是11
	// 也就是用的是timer 2 来做时钟
	//timer_source.event_id 是2
	irq_number = timer_source.event_id + IRQ_TIMER0;

	// 整个系统的时钟中断的终端号在这里注册
	//irq_number  13 
	setup_irq(irq_number, &s5p_clock_event_irq);
	//注意运行到这里的时候 真正的 handler还没有产生
}

static void __iomem *s5p_timer_reg(void)
{
	unsigned long offset = 0;

	switch (timer_source.source_id) {
	case S5P_PWM0:
	case S5P_PWM1:
	case S5P_PWM2:
	case S5P_PWM3:
		offset = (timer_source.source_id * 0x0c) + 0x14;
		break;

	case S5P_PWM4:
		offset = 0x40;
		break;

	default:
		printk(KERN_ERR "Invalid Timer %d\n", timer_source.source_id);
		return NULL;
	}

	return S3C_TIMERREG(offset);
}

static cycle_t s5p_timer_read(struct clocksource *cs)
{
	void __iomem *reg = s5p_timer_reg();

	return (cycle_t) (reg ? ~__raw_readl(reg) : 0);
}

/*
 * Override the global weak sched_clock symbol with this
 * local implementation which uses the clocksource to get some
 * better resolution when scheduling the kernel. We accept that
 * this wraps around for now, since it is just a relative time
 * stamp. (Inspired by U300 implementation.)
 */
static DEFINE_CLOCK_DATA(cd);

unsigned long long notrace sched_clock(void)
{
	void __iomem *reg = s5p_timer_reg();

	if (!reg)
		return 0;

	return cyc_to_sched_clock(&cd, ~__raw_readl(reg), (u32)~0);
}


// 回调函数，不停的读timer 4的 寄存器
static void notrace s5p_update_sched_clock(void)
{
	void __iomem *reg = s5p_timer_reg();

	
	if (!reg)
		return;

	update_sched_clock(&cd, ~__raw_readl(reg), (u32)~0);
}

//时钟源本身不会产生中断，要获得时钟源的当前计数，
//只能通过主动调用它的read回调函数来获得当前的计数值，
//注意这里只能获得计数值，也就是所谓的cycle，
//要获得相应的时间，必须要借助clocksource的mult和shift字段进行转换计算。


HZ


struct clocksource time_clocksource = {
	.name		= "s5p_clocksource_timer",
	.rating		= 250,   //  4ms
	.read		= s5p_timer_read,      
	.mask		= CLOCKSOURCE_MASK(32),
	.flags		= CLOCK_SOURCE_IS_CONTINUOUS,
};

static void __init s5p_clocksource_init(void)
{
	unsigned long pclk;
	unsigned long clock_rate;

	//pclk  66700000 
	pclk = clk_get_rate(timerclk);

	clk_set_rate(tdiv_source, pclk / 2);
	clk_set_parent(tin_source, tdiv_source);

	// clock_rate  33350000 
	clock_rate = clk_get_rate(tin_source);



	// 这里应该是4,这个4只设定了一次

	s5p_time_setup(timer_source.source_id, TCNT_MAX);
	s5p_time_start(timer_source.source_id, PERIODIC);

	//#define DEFINE_CLOCK_DATA(name)	struct clock_data name
	//static DEFINE_CLOCK_DATA(cd);
	// 初始化了一个poll的 timer，也就是 sched_clock中的 timer
	
	init_sched_clock(&cd, s5p_update_sched_clock, 32, clock_rate);


	//   clocksource_register_hz  这里?
	// 这里注册了第一个clock source
	if (clocksource_register_hz(&time_clocksource, clock_rate))
		panic("%s: can't register clocksource\n", time_clocksource.name);
}

/*
linux的时间子系统需要两种时间相关的硬件：
一个是free running的counter（system counter），
抽象为clock source device，另外一个就是能够产生中断的能力的
timer（per cpu timer），抽象为clock event device。
对于ARM generic timer driver而言，
我们需要定义linux kernel时间子系统的clock source和
clock event device并注册到系统。


*/

static void __init s5p_timer_resources(void)
{

	unsigned long event_id = timer_source.event_id;
	unsigned long source_id = timer_source.source_id;

	// 这里就是找出各个时间的 clock，但是怎么找我还没有看明白

	//  应该是在cpu相关代码李曼初始化的这个timer
	// 在 clock.c中  /arch/arm/mach-s5pv210
	timerclk = clk_get(NULL, "timers");
	if (IS_ERR(timerclk))
		panic("failed to get timers clock for timer");

	clk_enable(timerclk);  // 这里到时候跟踪一下

	tin_event = clk_get(&s3c_device_timer[event_id].dev, "pwm-tin");
	if (IS_ERR(tin_event))
		panic("failed to get pwm-tin clock for event timer");

	tdiv_event = clk_get(&s3c_device_timer[event_id].dev, "pwm-tdiv");
	if (IS_ERR(tdiv_event))
		panic("failed to get pwm-tdiv clock for event timer");

	clk_enable(tin_event);

	tin_source = clk_get(&s3c_device_timer[source_id].dev, "pwm-tin");
	if (IS_ERR(tin_source))
		panic("failed to get pwm-tin clock for source timer");

	tdiv_source = clk_get(&s3c_device_timer[source_id].dev, "pwm-tdiv");
	if (IS_ERR(tdiv_source))
		panic("failed to get pwm-tdiv clock for source timer");

	clk_enable(tin_source);
}

static void __init s5p_timer_init(void)
{
	s5p_timer_resources();   // 把之前注册的clk找出来
	s5p_clockevent_init();  // event 的注册，
	s5p_clocksource_init();	// source 的注册
}

struct sys_timer s5p_timer = {
	.init		= s5p_timer_init,
};
