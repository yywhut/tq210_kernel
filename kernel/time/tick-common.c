/*
 * linux/kernel/time/tick-common.c
 *
 * This file contains the base functions to manage periodic tick
 * related events.
 *
 * Copyright(C) 2005-2006, Thomas Gleixner <tglx@linutronix.de>
 * Copyright(C) 2005-2007, Red Hat, Inc., Ingo Molnar
 * Copyright(C) 2006-2007, Timesys Corp., Thomas Gleixner
 *
 * This code is licenced under the GPL version 2. For details see
 * kernel-base/COPYING.
 */
#include <linux/cpu.h>
#include <linux/err.h>
#include <linux/hrtimer.h>
#include <linux/interrupt.h>
#include <linux/percpu.h>
#include <linux/profile.h>
#include <linux/sched.h>

#include <asm/irq_regs.h>

#include "tick-internal.h"

/*
当内核没有配置成支持高精度定时器时，
系统的tick由tick_device产生，tick_device其实是clock_event_device的简单封装，
它内嵌了一个clock_event_device指针和它的工作模式：

在kernel/time/tick-common.c中，定义了一个per-cpu的tick_device全局变量
，tick_cpu_device：

*/


/*
 * Tick devices
 */
 /*
local tick device。在单核系统中，传统的unix都是在tick驱动下进行任务调度、
低精度timer触发等，在多核架构下，系统为每一个cpu建立了一个tick device，如下：

 */
DEFINE_PER_CPU(struct tick_device, tick_cpu_device);
/*
 * Tick next event: keeps track of the tick time
 */
ktime_t tick_next_period;
ktime_t tick_period;
int tick_do_timer_cpu __read_mostly = TICK_DO_TIMER_BOOT;
static DEFINE_RAW_SPINLOCK(tick_device_lock);
/*
有些任务不适合在local tick device中处理，例如更新jiffies，更新系统的wall time，
更新系统的平均负载（不是单一CPU core的负载），这些都是系统级别的任务，
只需要在local tick device中选择一个作为global tick device就OK了。
tick_do_timer_cpu指明哪一个cpu上的local tick作为global tick。

*/

/*
 * Debugging: see timer_list.c
 */
struct tick_device *tick_get_device(int cpu)
{
	return &per_cpu(tick_cpu_device, cpu);
}

/**
 * tick_is_oneshot_available - check for a oneshot capable event device
 */
int tick_is_oneshot_available(void)
{
	struct clock_event_device *dev = __this_cpu_read(tick_cpu_device.evtdev);

	if (!dev || !(dev->features & CLOCK_EVT_FEAT_ONESHOT))
		return 0;
	if (!(dev->features & CLOCK_EVT_FEAT_C3STOP))
		return 1;
	return tick_broadcast_oneshot_available();
}

/*
 * Periodic tick
 */
 /*
	更新jiffies_64变量；
更新墙上时钟；
每10个tick，更新一次cpu的负载信息；

 */
static void tick_periodic(int cpu)
{
	if (tick_do_timer_cpu == cpu) {  //global tick需要进行一些额外处理 
		write_seqlock(&xtime_lock);

		/* Keep track of the next tick event */
		tick_next_period = ktime_add(tick_next_period, tick_period);

		do_timer(1);  //更新jiffies，计算平均负载 更新timekeeper
		write_sequnlock(&xtime_lock);
	}

	/*
		调用update_peocess_times，完成以下事情：
		更新进程的时间统计信息；
		触发TIMER_SOFTIRQ软件中断，以便系统处理传统的低分辨率定时器；
		检查rcu的callback；
		通过scheduler_tick触发调度系统进行进程统计和调度工作；
	*/
	
	update_process_times(user_mode(get_irq_regs()));
	profile_tick(CPU_PROFILING);  //和性能剖析相关，不详述了 
}

/*
 * Event handler for periodic ticks
 */

/*
函数 tick_handle_periodic 的处理过程分成了以下两个部分：

    1全局处理：整个系统中的信息处理
    2局部处理：局部于本地 CPU 的处理
    ●总结一下，一次时钟中断发生后， OS 主要执行的操作（ tick_handle_periodic ）：

    ●全局处理（仅在一个 CPU 上运行）：
    1更新 jiffies_64
    2更新 xtimer 和当前时钟源信息等
    3根据 tick 计算 avenrun 负载
     ●局部处理（每个 CPU 都要运行）：
    1根据当前在用户态还是核心态，统计当前进程的时间：用户态时间还是核心态时间
   2 唤醒 TIMER_SOFTIRQ 软中断
    3唤醒 RCU 软中断
    4调用 scheduler_tick （更新进程时间片等等操作，更多内容参见参考文献）
    5profile_tick 函数调用
    以上就介绍完了硬件时钟的处理过程，下面来看软件时钟。


*/

/*
该函数首先调用tick_periodic函数，
完成tick事件的所有处理，如果是周期触发模式，
处理结束，如果工作在单触发模式，则计算并设置下一次的触发时刻，
这里用了一个循环，是为了防止当该函数被调用时，
clock_event_device中的计时实际上已经经过了不止一个tick周期，
这时候，tick_periodic可能被多次调用，使得jiffies和时间可以被正确地更新。
tick_periodic的代码如下：

这里就是没有初始化好高精度定时器的时候的 中断调用，如果高精度定时器初始化完成了。
也会模拟这里的工作



*/
void tick_handle_periodic(struct clock_event_device *dev)
{
	int cpu = smp_processor_id();
	ktime_t next;

	tick_periodic(cpu);  //  跟新xtime

	// 刚开始 tq210会一直进入这里，然后返回，直到
	/*
		tick_init_highres: yyyyyyyy
		yyyyyyy  tick_switch_to_oneshot  
		Switched to NOHz mode on CPU #0
	*/

	if (dev->mode != CLOCK_EVT_MODE_ONESHOT)
		return;
	/*
	 * Setup the next period for devices, which do not have
	 * periodic mode:
	 */
	 //计算下一个周期性tick触发的时间 
	next = ktime_add(dev->next_event, tick_period);
	for (;;) {
		if (!clockevents_program_event(dev, next, ktime_get()))//设定下一个clock event触发的时间 
			return;
		/*
		 * Have to be careful here. If we're in oneshot mode,
		 * before we call tick_periodic() in a loop, we need
		 * to be sure we're using a real hardware clocksource.
		 * Otherwise we could get trapped in an infinite
		 * loop, as the tick_periodic() increments jiffies,
		 * when then will increment time, posibly causing
		 * the loop to trigger again and again.
		 */
		if (timekeeping_valid_for_hres())
			tick_periodic(cpu);
		next = ktime_add(next, tick_period);
	}
}

/*
 * Setup the device for a periodic tick
 */
void tick_setup_periodic(struct clock_event_device *dev, int broadcast)
{
	tick_set_periodic_handler(dev, broadcast);

	/* Broadcast setup ? */
	if (!tick_device_is_functional(dev))
		return;

	/*
	（a）如果底层的clock event device支持periodic模式，
	那么直接调用clockevents_set_mode设定模式就OK了
    （b）如果底层的clock event device不支持periodic模式，
    而tick device目前是周期性tick mode，那么要稍微复杂一些，
    需要用clock event device的one shot模式来实现周期性tick。


	*/
	if ((dev->features & CLOCK_EVT_FEAT_PERIODIC) &&
	    !tick_broadcast_oneshot_active()) {
		clockevents_set_mode(dev, CLOCK_EVT_MODE_PERIODIC);
		//  tq210 进入这里了，
		
	} else {
		unsigned long seq;
		ktime_t next;

		do {
			seq = read_seqbegin(&xtime_lock);
			next = tick_next_period;   //获取下一个周期性tick触发的时间 
		} while (read_seqretry(&xtime_lock, seq));

		//模式设定
		clockevents_set_mode(dev, CLOCK_EVT_MODE_ONESHOT);

		for (;;) {
			if (!clockevents_program_event(dev, next, ktime_get()))
				return;
			next = ktime_add(next, tick_period);
			//计算下一个周期性tick触发的时间 
		}
	}
}

/*
 * Setup the tick device
 */
 /*
所谓setup一个tick device就是对tick device心仪的clock event设备进行设置
，并将该tick device的evtdev指向新注册的这个clock event device，
具体代码如下：

 */


// 这个代码只执行了一次

static void tick_setup_device(struct tick_device *td,
			      struct clock_event_device *newdev, int cpu,
			      const struct cpumask *cpumask)
{
	ktime_t next_event;
	void (*handler)(struct clock_event_device *) = NULL;

	/*
	 * First device setup ?
	 */
	 /*
	 在multi core的环境下，每一个CPU core都自己的tick device
	 （可以称之local tick device），这些tick device中有一个被选择做
	 global tick device，负责维护整个系统的jiffies。
	 如果该tick device的是第一次设定，并且目前系统中没有global tick
	 设备，那么可以考虑选择该tick设备作为global设备，进行系统时间和
	 jiffies的更新。更细节的内容请参考timekeeping文档。
	 */
	if (!td->evtdev) {
		/*
		 * If no cpu took the do_timer update, assign it to
		 * this cpu:
		 */
		if (tick_do_timer_cpu == TICK_DO_TIMER_BOOT) {
			tick_do_timer_cpu = cpu;
			tick_next_period = ktime_get();
			tick_period = ktime_set(0, NSEC_PER_SEC / HZ); 
			//[    0.000000] NSEC_PER_SEC 1000000000 
			//[    0.000000] HZ 200 
			//[    0.000000] tick_period 5000000   应该是这么多 n秒，也就死5ms
			//1s = 1000 000 000 n秒
		}

		/*
		 * Startup in periodic mode first.
		 */
		 /*
		在最初设定tick device的时候，缺省被设定为周期性的tick。
		当然，这仅仅是初始设定，实际上在满足一定的条件下，
		在适当的时间，tick device是可以切换到其他模式的，
		下面会具体描述。
		 */
		td->mode = TICKDEV_MODE_PERIODIC;
	} else {
		handler = td->evtdev->event_handler;  // tq210这里没有执行
		next_event = td->evtdev->next_event;
		td->evtdev->event_handler = clockevents_handle_noop;
		//旧的clockevent设备就要退居二线了，
		//将其handler修改为clockevents_handle_noop
	}

	td->evtdev = newdev;

	/*
	 * When the device is not per cpu, pin the interrupt to the
	 * current cpu:
	 如果不是local timer，那么还需要调用irq_set_affinity函数，
	 将该clockevent的中断，定向到本CPU。
	 */
	 
	if (!cpumask_equal(newdev->cpumask, cpumask))
		irq_set_affinity(newdev->irq, cpumask);

	/*
	 * When global broadcasting is active, check if the current
	 * device is registered as a placeholder for broadcast mode.
	 * This allows us to handle this x86 misfeature in a generic
	 * way.
	 */
	if (tick_device_uses_broadcast(newdev, cpu))
		return;

	//   到底设定是 periond 还是 one shot
	//tick_setup_deviceyyyyyyyyyy is 0
	// 这里好像只会执行一次
	if (td->mode == TICKDEV_MODE_PERIODIC)
		tick_setup_periodic(newdev, 0);
	else
		tick_setup_oneshot(newdev, handler, next_event);
}

/*
 * Check, if the new registered device should be used.
 */


/*
第二个关卡是per cpu的检查。如果检查不通过，那么说明这个新注册的clock event device
和该CPU不来电，不能用于该cpu的local tick。如果注册的hw timer都是cpu local的
（仅仅属于一个cpu，这时候该clock event device的cpumask只有一个bit被set），
那么事情会比较简单。然而，事情往往没有那么简单，一个hw timer可以服务多个cpu。
我们这里说HW timer服务于某个cpu其实最重要的是irq是否可以分发到指定的cpu上。
我们可以看看tick_check_percpu的实现

*/

/*
前面曾经说过，当machine的代码为每个cpu注册clock_event_device时
，通知回调函数tick_notify会被调用，进而进入tick_check_new_device函数
，下面让我们看看该函数如何工作，首先
，该函数先判断注册的clock_event_device是否可用于本cpu
，然后从per-cpu变量中取出本cpu的tick_device：
*/

/*
在clock event device的文章中，我们知道：底层的timer硬件驱动在初始化的时候会注册
clock event device，在注册过程中就会调用tick_check_new_device函数来看看是否需要
进行tick device的初始化，如果已经已经初始化OK的tick device是否有更换更高精度
clock event device的需求。代码如下
*/
static int tick_check_new_device(struct clock_event_device *newdev)
{
	struct clock_event_device *curdev;
	struct tick_device *td;
	int cpu, ret = NOTIFY_OK;
	unsigned long flags;

	raw_spin_lock_irqsave(&tick_device_lock, flags);

	cpu = smp_processor_id();
	if (!cpumask_test_cpu(cpu, newdev->cpumask))
		goto out_bc;

	td = &per_cpu(tick_cpu_device, cpu); //获取当前cpu的tick device 
	curdev = td->evtdev;  //目前tick device正在使用的clock event device

	/*
	如果不是本地clock_event_device，
	会做进一步的判断：如果不能把irq绑定到本cpu，
	则放弃处理，如果本cpu已经有了一个本地clock_event_device，也放弃处理：

	*/
	/* cpu local device ? */  // 这里没进
	if (!cpumask_equal(newdev->cpumask, cpumask_of(cpu))) {

		/*
		 * If the cpu affinity of the device interrupt can not
		 * be set, ignore it.
		 */
		if (!irq_can_set_affinity(newdev->irq))
			goto out_bc;

		/*
		 * If we have a cpu local device already, do not replace it
		 * by a non cpu local device
		 */
		if (curdev && cpumask_equal(curdev->cpumask, cpumask_of(cpu)))
			goto out_bc;
	}

	/*
	 * If we have an active device, then check the rating and the oneshot
	 * feature.
	 */

	/*
	反之，如果本cpu已经有了一个clock_event_device，
	则根据是否支持单触发模式和它的rating值，
	决定是否替换原来旧的clock_event_device：
	*/
	
	if (curdev) {   //  这里也没进
		/*
		 * Prefer one shot capable devices !
		 */
		if ((curdev->features & CLOCK_EVT_FEAT_ONESHOT) &&
		    !(newdev->features & CLOCK_EVT_FEAT_ONESHOT))
			goto out_bc;  // 新的不支持单触发，但旧的支持，所以不能替换  
		/*
		 * Check the rating
		 */
		if (curdev->rating >= newdev->rating)
			goto out_bc;  // 旧的比新的精度高，不能替换  
	}

	/*
	 * Replace the eventually existing device by the new
	 * device. If the current device is the broadcast device, do
	 * not give it back to the clockevents layer !
	 在这些判断都通过之后，说明或者来cpu还没有绑定tick_device，
	 或者是新的更好，需要替换：
	 */
	if (tick_is_broadcast_device(curdev)) {  // 没进
		clockevents_shutdown(curdev);
		curdev = NULL;
	}
	clockevents_exchange_device(curdev, newdev); //通知clockevent layer 

	//  绑定clockevent device 与 tick device
	tick_setup_device(td, newdev, cpu, cpumask_of(cpu));
	if (newdev->features & CLOCK_EVT_FEAT_ONESHOT)
		tick_oneshot_notify();   //  这里执行了

	/*
		上面的tick_setup_device函数负责重新绑定当前cpu的tick_device
		和新注册的clock_event_device，如果发现是当前cpu第一次
		注册tick_device，就把它设置为TICKDEV_MODE_PERIODIC模式，
		如果是替换旧的tick_device，则根据新的tick_device的特性，
		设置为TICKDEV_MODE_PERIODIC或TICKDEV_MODE_ONESHOT模式。
		可见，在系统的启动阶段，tick_device是工作在周期触发模式的，
		直到框架层在合适的时机，才会开启单触发模式，以便支持NO_HZ和HRTIMER。

	
	*/

	raw_spin_unlock_irqrestore(&tick_device_lock, flags);
	return NOTIFY_STOP;

out_bc:
	/*
	 * Can the new device be used as a broadcast device ?
	 */
	if (tick_check_broadcast_device(newdev))
		ret = NOTIFY_STOP;

	raw_spin_unlock_irqrestore(&tick_device_lock, flags);

	return ret;
}

/*
 * Transfer the do_timer job away from a dying cpu.
 *
 * Called with interrupts disabled.
 */
static void tick_handover_do_timer(int *cpup)
{
	if (*cpup == tick_do_timer_cpu) {
		int cpu = cpumask_first(cpu_online_mask);

		tick_do_timer_cpu = (cpu < nr_cpu_ids) ? cpu :
			TICK_DO_TIMER_NONE;
	}
}

/*
 * Shutdown an event device on a given cpu:
 *
 * This is called on a life CPU, when a CPU is dead. So we cannot
 * access the hardware device itself.
 * We just set the mode and remove it from the lists.
 */
static void tick_shutdown(unsigned int *cpup)
{
	struct tick_device *td = &per_cpu(tick_cpu_device, *cpup);
	struct clock_event_device *dev = td->evtdev;
	unsigned long flags;

	raw_spin_lock_irqsave(&tick_device_lock, flags);
	td->mode = TICKDEV_MODE_PERIODIC;
	if (dev) {
		/*
		 * Prevent that the clock events layer tries to call
		 * the set mode function!
		 */
		dev->mode = CLOCK_EVT_MODE_UNUSED;
		clockevents_exchange_device(dev, NULL);
		td->evtdev = NULL;
	}
	raw_spin_unlock_irqrestore(&tick_device_lock, flags);
}

static void tick_suspend(void)
{
	struct tick_device *td = &__get_cpu_var(tick_cpu_device);
	unsigned long flags;

	raw_spin_lock_irqsave(&tick_device_lock, flags);
	clockevents_shutdown(td->evtdev);
	raw_spin_unlock_irqrestore(&tick_device_lock, flags);
}

static void tick_resume(void)
{
	struct tick_device *td = &__get_cpu_var(tick_cpu_device);
	unsigned long flags;
	int broadcast = tick_resume_broadcast();

	raw_spin_lock_irqsave(&tick_device_lock, flags);
	clockevents_set_mode(td->evtdev, CLOCK_EVT_MODE_RESUME);

	if (!broadcast) {
		if (td->mode == TICKDEV_MODE_PERIODIC)
			tick_setup_periodic(td->evtdev, 0);
		else
			tick_resume_oneshot();
	}
	raw_spin_unlock_irqrestore(&tick_device_lock, flags);
}

/*
 * Notification about clock event devices
 */
static int tick_notify(struct notifier_block *nb, unsigned long reason,
			       void *dev)
{
	switch (reason) {

	case CLOCK_EVT_NOTIFY_ADD:
		return tick_check_new_device(dev);

	case CLOCK_EVT_NOTIFY_BROADCAST_ON:
	case CLOCK_EVT_NOTIFY_BROADCAST_OFF:
	case CLOCK_EVT_NOTIFY_BROADCAST_FORCE:
		tick_broadcast_on_off(reason, dev);
		break;

	case CLOCK_EVT_NOTIFY_BROADCAST_ENTER:
	case CLOCK_EVT_NOTIFY_BROADCAST_EXIT:
		tick_broadcast_oneshot_control(reason);
		break;

	case CLOCK_EVT_NOTIFY_CPU_DYING:
		tick_handover_do_timer(dev);
		break;

	case CLOCK_EVT_NOTIFY_CPU_DEAD:
		tick_shutdown_broadcast_oneshot(dev);
		tick_shutdown_broadcast(dev);
		tick_shutdown(dev);
		break;

	case CLOCK_EVT_NOTIFY_SUSPEND:
		tick_suspend();
		tick_suspend_broadcast();
		break;

	case CLOCK_EVT_NOTIFY_RESUME:
		tick_resume();
		break;

	default:
		break;
	}

	return NOTIFY_OK;
}

static struct notifier_block tick_notifier = {
	.notifier_call = tick_notify,
};

/**
 * tick_init - initialize the tick control
 *
 * Register the notifier with the clockevents framework
 */
void __init tick_init(void)
{
	clockevents_register_notifier(&tick_notifier);
}
