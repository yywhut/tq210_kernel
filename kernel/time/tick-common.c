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
���ں�û�����ó�֧�ָ߾��ȶ�ʱ��ʱ��
ϵͳ��tick��tick_device������tick_device��ʵ��clock_event_device�ļ򵥷�װ��
����Ƕ��һ��clock_event_deviceָ������Ĺ���ģʽ��

��kernel/time/tick-common.c�У�������һ��per-cpu��tick_deviceȫ�ֱ���
��tick_cpu_device��

*/


/*
 * Tick devices
 */
 /*
local tick device���ڵ���ϵͳ�У���ͳ��unix������tick�����½���������ȡ�
�;���timer�����ȣ��ڶ�˼ܹ��£�ϵͳΪÿһ��cpu������һ��tick device�����£�

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
��Щ�����ʺ���local tick device�д����������jiffies������ϵͳ��wall time��
����ϵͳ��ƽ�����أ����ǵ�һCPU core�ĸ��أ�����Щ����ϵͳ���������
ֻ��Ҫ��local tick device��ѡ��һ����Ϊglobal tick device��OK�ˡ�
tick_do_timer_cpuָ����һ��cpu�ϵ�local tick��Ϊglobal tick��

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
	����jiffies_64������
����ǽ��ʱ�ӣ�
ÿ10��tick������һ��cpu�ĸ�����Ϣ��

 */
static void tick_periodic(int cpu)
{
	if (tick_do_timer_cpu == cpu) {  //global tick��Ҫ����һЩ���⴦�� 
		write_seqlock(&xtime_lock);

		/* Keep track of the next tick event */
		tick_next_period = ktime_add(tick_next_period, tick_period);

		do_timer(1);  //����jiffies������ƽ������ ����timekeeper
		write_sequnlock(&xtime_lock);
	}

	/*
		����update_peocess_times������������飺
		���½��̵�ʱ��ͳ����Ϣ��
		����TIMER_SOFTIRQ����жϣ��Ա�ϵͳ����ͳ�ĵͷֱ��ʶ�ʱ����
		���rcu��callback��
		ͨ��scheduler_tick��������ϵͳ���н���ͳ�ƺ͵��ȹ�����
	*/
	
	update_process_times(user_mode(get_irq_regs()));
	profile_tick(CPU_PROFILING);  //������������أ��������� 
}

/*
 * Event handler for periodic ticks
 */

/*
���� tick_handle_periodic �Ĵ�����̷ֳ��������������֣�

    1ȫ�ִ�������ϵͳ�е���Ϣ����
    2�ֲ������ֲ��ڱ��� CPU �Ĵ���
    ���ܽ�һ�£�һ��ʱ���жϷ����� OS ��Ҫִ�еĲ����� tick_handle_periodic ����

    ��ȫ�ִ�������һ�� CPU �����У���
    1���� jiffies_64
    2���� xtimer �͵�ǰʱ��Դ��Ϣ��
    3���� tick ���� avenrun ����
     ��ֲ�����ÿ�� CPU ��Ҫ���У���
    1���ݵ�ǰ���û�̬���Ǻ���̬��ͳ�Ƶ�ǰ���̵�ʱ�䣺�û�̬ʱ�仹�Ǻ���̬ʱ��
   2 ���� TIMER_SOFTIRQ ���ж�
    3���� RCU ���ж�
    4���� scheduler_tick �����½���ʱ��Ƭ�ȵȲ������������ݲμ��ο����ף�
    5profile_tick ��������
    ���Ͼͽ�������Ӳ��ʱ�ӵĴ�����̣������������ʱ�ӡ�


*/

/*
�ú������ȵ���tick_periodic������
���tick�¼������д�����������ڴ���ģʽ��
�����������������ڵ�����ģʽ������㲢������һ�εĴ���ʱ�̣�
��������һ��ѭ������Ϊ�˷�ֹ���ú���������ʱ��
clock_event_device�еļ�ʱʵ�����Ѿ������˲�ֹһ��tick���ڣ�
��ʱ��tick_periodic���ܱ���ε��ã�ʹ��jiffies��ʱ����Ա���ȷ�ظ��¡�
tick_periodic�Ĵ������£�

�������û�г�ʼ���ø߾��ȶ�ʱ����ʱ��� �жϵ��ã�����߾��ȶ�ʱ����ʼ������ˡ�
Ҳ��ģ������Ĺ���



*/
void tick_handle_periodic(struct clock_event_device *dev)
{
	int cpu = smp_processor_id();
	ktime_t next;

	tick_periodic(cpu);  //  ����xtime

	// �տ�ʼ tq210��һֱ�������Ȼ�󷵻أ�ֱ��
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
	 //������һ��������tick������ʱ�� 
	next = ktime_add(dev->next_event, tick_period);
	for (;;) {
		if (!clockevents_program_event(dev, next, ktime_get()))//�趨��һ��clock event������ʱ�� 
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
	��a������ײ��clock event device֧��periodicģʽ��
	��ôֱ�ӵ���clockevents_set_mode�趨ģʽ��OK��
    ��b������ײ��clock event device��֧��periodicģʽ��
    ��tick deviceĿǰ��������tick mode����ôҪ��΢����һЩ��
    ��Ҫ��clock event device��one shotģʽ��ʵ��������tick��


	*/
	if ((dev->features & CLOCK_EVT_FEAT_PERIODIC) &&
	    !tick_broadcast_oneshot_active()) {
		clockevents_set_mode(dev, CLOCK_EVT_MODE_PERIODIC);
		//  tq210 ���������ˣ�
		
	} else {
		unsigned long seq;
		ktime_t next;

		do {
			seq = read_seqbegin(&xtime_lock);
			next = tick_next_period;   //��ȡ��һ��������tick������ʱ�� 
		} while (read_seqretry(&xtime_lock, seq));

		//ģʽ�趨
		clockevents_set_mode(dev, CLOCK_EVT_MODE_ONESHOT);

		for (;;) {
			if (!clockevents_program_event(dev, next, ktime_get()))
				return;
			next = ktime_add(next, tick_period);
			//������һ��������tick������ʱ�� 
		}
	}
}

/*
 * Setup the tick device
 */
 /*
��νsetupһ��tick device���Ƕ�tick device���ǵ�clock event�豸��������
��������tick device��evtdevָ����ע������clock event device��
����������£�

 */


// �������ִֻ����һ��

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
	 ��multi core�Ļ����£�ÿһ��CPU core���Լ���tick device
	 �����Գ�֮local tick device������Щtick device����һ����ѡ����
	 global tick device������ά������ϵͳ��jiffies��
	 �����tick device���ǵ�һ���趨������Ŀǰϵͳ��û��global tick
	 �豸����ô���Կ���ѡ���tick�豸��Ϊglobal�豸������ϵͳʱ���
	 jiffies�ĸ��¡���ϸ�ڵ�������ο�timekeeping�ĵ���
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
			//[    0.000000] tick_period 5000000   Ӧ������ô�� n�룬Ҳ����5ms
			//1s = 1000 000 000 n��
		}

		/*
		 * Startup in periodic mode first.
		 */
		 /*
		������趨tick device��ʱ��ȱʡ���趨Ϊ�����Ե�tick��
		��Ȼ��������ǳ�ʼ�趨��ʵ����������һ���������£�
		���ʵ���ʱ�䣬tick device�ǿ����л�������ģʽ�ģ�
		��������������
		 */
		td->mode = TICKDEV_MODE_PERIODIC;
	} else {
		handler = td->evtdev->event_handler;  // tq210����û��ִ��
		next_event = td->evtdev->next_event;
		td->evtdev->event_handler = clockevents_handle_noop;
		//�ɵ�clockevent�豸��Ҫ�˾Ӷ����ˣ�
		//����handler�޸�Ϊclockevents_handle_noop
	}

	td->evtdev = newdev;

	/*
	 * When the device is not per cpu, pin the interrupt to the
	 * current cpu:
	 �������local timer����ô����Ҫ����irq_set_affinity������
	 ����clockevent���жϣ����򵽱�CPU��
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

	//   �����趨�� periond ���� one shot
	//tick_setup_deviceyyyyyyyyyy is 0
	// �������ֻ��ִ��һ��
	if (td->mode == TICKDEV_MODE_PERIODIC)
		tick_setup_periodic(newdev, 0);
	else
		tick_setup_oneshot(newdev, handler, next_event);
}

/*
 * Check, if the new registered device should be used.
 */


/*
�ڶ����ؿ���per cpu�ļ�顣�����鲻ͨ������ô˵�������ע���clock event device
�͸�CPU�����磬�������ڸ�cpu��local tick�����ע���hw timer����cpu local��
����������һ��cpu����ʱ���clock event device��cpumaskֻ��һ��bit��set����
��ô�����Ƚϼ򵥡�Ȼ������������û����ô�򵥣�һ��hw timer���Է�����cpu��
��������˵HW timer������ĳ��cpu��ʵ����Ҫ����irq�Ƿ���Էַ���ָ����cpu�ϡ�
���ǿ��Կ���tick_check_percpu��ʵ��

*/

/*
ǰ������˵������machine�Ĵ���Ϊÿ��cpuע��clock_event_deviceʱ
��֪ͨ�ص�����tick_notify�ᱻ���ã���������tick_check_new_device����
�����������ǿ����ú�����ι���������
���ú������ж�ע���clock_event_device�Ƿ�����ڱ�cpu
��Ȼ���per-cpu������ȡ����cpu��tick_device��
*/

/*
��clock event device�������У�����֪�����ײ��timerӲ�������ڳ�ʼ����ʱ���ע��
clock event device����ע������оͻ����tick_check_new_device�����������Ƿ���Ҫ
����tick device�ĳ�ʼ��������Ѿ��Ѿ���ʼ��OK��tick device�Ƿ��и������߾���
clock event device�����󡣴�������
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

	td = &per_cpu(tick_cpu_device, cpu); //��ȡ��ǰcpu��tick device 
	curdev = td->evtdev;  //Ŀǰtick device����ʹ�õ�clock event device

	/*
	������Ǳ���clock_event_device��
	������һ�����жϣ�������ܰ�irq�󶨵���cpu��
	��������������cpu�Ѿ�����һ������clock_event_device��Ҳ��������

	*/
	/* cpu local device ? */  // ����û��
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
	��֮�������cpu�Ѿ�����һ��clock_event_device��
	������Ƿ�֧�ֵ�����ģʽ������ratingֵ��
	�����Ƿ��滻ԭ���ɵ�clock_event_device��
	*/
	
	if (curdev) {   //  ����Ҳû��
		/*
		 * Prefer one shot capable devices !
		 */
		if ((curdev->features & CLOCK_EVT_FEAT_ONESHOT) &&
		    !(newdev->features & CLOCK_EVT_FEAT_ONESHOT))
			goto out_bc;  // �µĲ�֧�ֵ����������ɵ�֧�֣����Բ����滻  
		/*
		 * Check the rating
		 */
		if (curdev->rating >= newdev->rating)
			goto out_bc;  // �ɵı��µľ��ȸߣ������滻  
	}

	/*
	 * Replace the eventually existing device by the new
	 * device. If the current device is the broadcast device, do
	 * not give it back to the clockevents layer !
	 ����Щ�ж϶�ͨ��֮��˵��������cpu��û�а�tick_device��
	 �������µĸ��ã���Ҫ�滻��
	 */
	if (tick_is_broadcast_device(curdev)) {  // û��
		clockevents_shutdown(curdev);
		curdev = NULL;
	}
	clockevents_exchange_device(curdev, newdev); //֪ͨclockevent layer 

	//  ��clockevent device �� tick device
	tick_setup_device(td, newdev, cpu, cpumask_of(cpu));
	if (newdev->features & CLOCK_EVT_FEAT_ONESHOT)
		tick_oneshot_notify();   //  ����ִ����

	/*
		�����tick_setup_device�����������°󶨵�ǰcpu��tick_device
		����ע���clock_event_device����������ǵ�ǰcpu��һ��
		ע��tick_device���Ͱ�������ΪTICKDEV_MODE_PERIODICģʽ��
		������滻�ɵ�tick_device��������µ�tick_device�����ԣ�
		����ΪTICKDEV_MODE_PERIODIC��TICKDEV_MODE_ONESHOTģʽ��
		�ɼ�����ϵͳ�������׶Σ�tick_device�ǹ��������ڴ���ģʽ�ģ�
		ֱ����ܲ��ں��ʵ�ʱ�����ŻῪ��������ģʽ���Ա�֧��NO_HZ��HRTIMER��

	
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
