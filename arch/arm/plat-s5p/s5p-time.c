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


//  ����ļ����б������

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

		//S3C2410_TCON fd300008  ���Ӧ���������ַ
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

	timer_source.event_id = event;   //2�Ŷ�ʱ��
	timer_source.source_id = source;	// 4�Ŷ�ʱ��
}

/*
set_next_event�������¼�������ʱ��
event_handler���¼�����ʱҪ��ȡ�Ķ���
feature��ʱ���¼��豸�����ԡ�������Ƚϵ��͵ģ�
			CLOCK_EVT_FEAT_PERIODIC��ʾ֧���������¼���
			CLOCK_EVT_FEAT_ONESHOT��ʾ֧�ֵ������¼�
set_mode����������ģʽ�ĺ���
mult��shift��������λ������������ʱ�Ӻ��ں�ns֮�����ת��
rating����ʾ������
broadcast���㲥
*/

/*
����ͨ��ʱ�ӿ�ܵ����룬�ں���Ҫ֧�ָ߾��ȵĶ�ʱ����Ϊ�ˣ�
ͨ��ʱ����Ϊ��ʱ��Ӳ��������һ����׼�Ľӿڣ�clock_event_device��
machine���Ĵ���ֻҪ�������׼�ӿ�ʵ����Ӧ��Ӳ�����ƹ��ܣ�
ʣ�µ���ƽ̨�޹ص�������ͳһ��ͨ��ʱ���ܲ���ʵ��

����smpϵͳ�У�Ϊ�˼��ٴ��������ͨ�ſ�����������ÿ��cpu����߱�һ�������Լ��ı���clock_event_device��
������Ϊ��cpu�ṩʱ���¼�����smp�е�ÿ��cpu���ڱ��ص�
clock_event_device�������Լ���tick_device����ͨ��ʱ���͸߾��ȶ�ʱ��
ick_device�ǻ���clock_event_device�Ľ�һ����װ�����ڴ���ԭ�е�ʱ�ӵδ��жϣ�
���ں��ṩtick�¼�������ɽ��̵ĵ��Ⱥͽ�����Ϣͳ�ƣ�����ƽ���ʱ����µȲ�����
*/
static struct clock_event_device time_event_device = {
	.name		= "s5p_event_timer",
	.features	= CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT,
	.rating		= 200,
	.set_next_event	= s5p_set_next_event,  //�����¼�������ʱ��
	.set_mode	= s5p_set_mode,
};

// �������timer2���жϴ���Ƶ��Ϊ200hz
// ���Կ��� ������ jiffies is    4294907561  �ڲ�ͣ�ļ�1.
// �����handler�ᶯ̬�� ������ period ����one shot

static irqreturn_t s5p_clock_event_isr(int irq, void *dev_id)
{
	struct clock_event_device *evt = dev_id;
	//printk("jiffies is    %lu \n",jiffies);
	evt->event_handler(evt);    //���﾿��ָ��˭�ᶯ̬�仯

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
//  Ҳ���� 166750��clcok��һ��tick
//HZ ��200  ÿs��200������ Ҳ����200��tick


	clockevents_calc_mult_shift(&time_event_device,
				    clock_rate, S5PTIMER_MIN_RANGE);
	time_event_device.max_delta_ns =
		clockevent_delta2ns(-1, &time_event_device);
	time_event_device.min_delta_ns =
		clockevent_delta2ns(1, &time_event_device);

	time_event_device.cpumask = cpumask_of(0);

	//������ϵͳ���һ��ʱ���¼��豸��
	//����ע���ʱ���¼��豸���ᱻ���������clockevent_devices�С�
	// ����ע����evend������һ��֪ͨ��������tick device
	clockevents_register_device(&time_event_device);

	// IRQ_TIMER0 ��11
	// Ҳ�����õ���timer 2 ����ʱ��
	//timer_source.event_id ��2
	irq_number = timer_source.event_id + IRQ_TIMER0;

	// ����ϵͳ��ʱ���жϵ��ն˺�������ע��
	//irq_number  13 
	setup_irq(irq_number, &s5p_clock_event_irq);
	//ע�����е������ʱ�� ������ handler��û�в���
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


// �ص���������ͣ�Ķ�timer 4�� �Ĵ���
static void notrace s5p_update_sched_clock(void)
{
	void __iomem *reg = s5p_timer_reg();

	
	if (!reg)
		return;

	update_sched_clock(&cd, ~__raw_readl(reg), (u32)~0);
}

//ʱ��Դ����������жϣ�Ҫ���ʱ��Դ�ĵ�ǰ������
//ֻ��ͨ��������������read�ص���������õ�ǰ�ļ���ֵ��
//ע������ֻ�ܻ�ü���ֵ��Ҳ������ν��cycle��
//Ҫ�����Ӧ��ʱ�䣬����Ҫ����clocksource��mult��shift�ֶν���ת�����㡣


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



	// ����Ӧ����4,���4ֻ�趨��һ��

	s5p_time_setup(timer_source.source_id, TCNT_MAX);
	s5p_time_start(timer_source.source_id, PERIODIC);

	//#define DEFINE_CLOCK_DATA(name)	struct clock_data name
	//static DEFINE_CLOCK_DATA(cd);
	// ��ʼ����һ��poll�� timer��Ҳ���� sched_clock�е� timer
	
	init_sched_clock(&cd, s5p_update_sched_clock, 32, clock_rate);


	//   clocksource_register_hz  ����?
	// ����ע���˵�һ��clock source
	if (clocksource_register_hz(&time_clocksource, clock_rate))
		panic("%s: can't register clocksource\n", time_clocksource.name);
}

/*
linux��ʱ����ϵͳ��Ҫ����ʱ����ص�Ӳ����
һ����free running��counter��system counter����
����Ϊclock source device������һ�������ܹ������жϵ�������
timer��per cpu timer��������Ϊclock event device��
����ARM generic timer driver���ԣ�
������Ҫ����linux kernelʱ����ϵͳ��clock source��
clock event device��ע�ᵽϵͳ��


*/

static void __init s5p_timer_resources(void)
{

	unsigned long event_id = timer_source.event_id;
	unsigned long source_id = timer_source.source_id;

	// ��������ҳ�����ʱ��� clock��������ô���һ�û�п�����

	//  Ӧ������cpu��ش���������ʼ�������timer
	// �� clock.c��  /arch/arm/mach-s5pv210
	timerclk = clk_get(NULL, "timers");
	if (IS_ERR(timerclk))
		panic("failed to get timers clock for timer");

	clk_enable(timerclk);  // ���ﵽʱ�����һ��

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
	s5p_timer_resources();   // ��֮ǰע���clk�ҳ���
	s5p_clockevent_init();  // event ��ע�ᣬ
	s5p_clocksource_init();	// source ��ע��
}

struct sys_timer s5p_timer = {
	.init		= s5p_timer_init,
};
