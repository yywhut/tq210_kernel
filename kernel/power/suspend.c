/*
 * kernel/power/suspend.c - Suspend to RAM and standby functionality.
 *
 * Copyright (c) 2003 Patrick Mochel
 * Copyright (c) 2003 Open Source Development Lab
 * Copyright (c) 2009 Rafael J. Wysocki <rjw@sisk.pl>, Novell Inc.
 *
 * This file is released under the GPLv2.
 */

#include <linux/string.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/console.h>
#include <linux/cpu.h>
#include <linux/syscalls.h>
#include <linux/gfp.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/suspend.h>
#include <linux/syscore_ops.h>
#include <trace/events/power.h>

#include "power.h"

const char *const pm_states[PM_SUSPEND_MAX] = {
#ifdef CONFIG_EARLYSUSPEND
	[PM_SUSPEND_ON]		= "on",
#endif
	[PM_SUSPEND_STANDBY]	= "standby",
	[PM_SUSPEND_MEM]	= "mem",
};

static const struct platform_suspend_ops *suspend_ops;
i2c_driver
/**
 *	suspend_set_ops - Set the global suspend method table.
 *	@ops:	Pointer to ops structure.
 */
void suspend_set_ops(const struct platform_suspend_ops *ops)
{
	mutex_lock(&pm_mutex);
	suspend_ops = ops;
	mutex_unlock(&pm_mutex);
}

bool valid_state(suspend_state_t state)
{
	/*
	 * All states need lowlevel support and need to be valid to the lowlevel
	 * implementation, no valid callback implies that none are valid.
	 */
	return suspend_ops && suspend_ops->valid && suspend_ops->valid(state);
}

/**
 * suspend_valid_only_mem - generic memory-only valid callback
 *
 * Platform drivers that implement mem suspend only and only need
 * to check for that in their .valid callback can use this instead
 * of rolling their own .valid callback.
 */
int suspend_valid_only_mem(suspend_state_t state)
{
	return state == PM_SUSPEND_MEM;
}

static int suspend_test(int level)
{
#ifdef CONFIG_PM_DEBUG
	if (pm_test_level == level) {
		printk(KERN_INFO "suspend debug: Waiting for 5 seconds.\n");
		mdelay(5000);
		return 1;
	}
#endif /* !CONFIG_PM_DEBUG */
	return 0;
}

/**
 *	suspend_prepare - Do prep work before entering low-power state.
 *
 *	This is common code that is called for each state that we're entering.
 *	Run suspend notifiers, allocate a console and stop all processes.
 */
static int suspend_prepare(void)
{
	int error;

	if (!suspend_ops || !suspend_ops->enter)
		return -EPERM;

	pm_prepare_console();

	// 通知进行休眠准备 
	error = pm_notifier_call_chain(PM_SUSPEND_PREPARE);  // 准备进入休眠的内核通知链
	if (error)
		goto Finish;

	error = usermodehelper_disable();
	if (error)
		goto Finish;

	error = suspend_freeze_processes();
	if (!error)
		return 0;

	// 解冻所有进程 
	suspend_thaw_processes();
	usermodehelper_enable();
 Finish:
 	 // 通知休眠结束
	pm_notifier_call_chain(PM_POST_SUSPEND);      
	pm_restore_console();
	return error;
}

/* default implementation */
void __attribute__ ((weak)) arch_suspend_disable_irqs(void)
{
	local_irq_disable();
}

/* default implementation */
void __attribute__ ((weak)) arch_suspend_enable_irqs(void)
{
	local_irq_enable();
}

/**
 *	suspend_enter - enter the desired system sleep state.
 *	@state:		state to enter
 *
 *	This function should be called after devices have been suspended.
 */
static int suspend_enter(suspend_state_t state)
{
	int error;

	if (suspend_ops->prepare) {  // 处理器的休眠准备函数  
		error = suspend_ops->prepare();
		if (error)
			goto Platform_finish;
	}

	error = dpm_suspend_noirq(PMSG_SUSPEND); // 执行非sysdev的late suspend函数  
	if (error) {
		printk(KERN_ERR "PM: Some devices failed to power down\n");
		goto Platform_finish;
	}

	if (suspend_ops->prepare_late) {  // 处理器休眠最后的准备  
		error = suspend_ops->prepare_late();
		if (error)
			goto Platform_wake;
	}

	if (suspend_test(TEST_PLATFORM))
		goto Platform_wake;

	error = disable_nonboot_cpus(); // 关闭非启动cpu  
	if (error || suspend_test(TEST_CPUS))
		goto Enable_cpus;

	arch_suspend_disable_irqs();  // 挂起中断  
	BUG_ON(!irqs_disabled());

	error = syscore_suspend(); // 挂起sysdev 
	if (!error) {
		if (!(suspend_test(TEST_CORE) || pm_wakeup_pending())) {
			error = suspend_ops->enter(state);  // 处理器的休眠进入函数,休眠流程运行至此  
			events_check_enabled = false;
		}
		syscore_resume();
	}

	arch_suspend_enable_irqs();
	BUG_ON(irqs_disabled());

 Enable_cpus:
	enable_nonboot_cpus();

 Platform_wake:
	if (suspend_ops->wake)
		suspend_ops->wake();

	dpm_resume_noirq(PMSG_RESUME);

 Platform_finish:
	if (suspend_ops->finish)
		suspend_ops->finish();

	return error;
}

/**
 *	suspend_devices_and_enter - suspend devices and enter the desired system
 *				    sleep state.
 *	@state:		  state to enter
 */
int suspend_devices_and_enter(suspend_state_t state)
{
	int error;

	if (!suspend_ops)
		return -ENOSYS;

	//suspend_set_ops(&s3c_pm_ops);
	trace_machine_suspend(state);

	// 处理器的休眠开始函数 
	if (suspend_ops->begin) {  // 调用特定平台实现的suspend_begin函数，
		error = suspend_ops->begin(state);
		if (error)
			goto Close;
	}
	suspend_console();
	suspend_test_start(); 
	error = dpm_suspend_start(PMSG_SUSPEND);// 设备休眠 
	if (error) {
		printk(KERN_ERR "PM: Some devices failed to suspend\n");
		goto Recover_platform;
	}
	suspend_test_finish("suspend devices");
	if (suspend_test(TEST_DEVICES))
		goto Recover_platform;

	error = suspend_enter(state);  // 处理器休眠  

 Resume_devices:
	suspend_test_start();
	dpm_resume_end(PMSG_RESUME); // 设备唤醒  
	suspend_test_finish("resume devices");
	resume_console();
 Close:
	if (suspend_ops->end)
		suspend_ops->end();   // 处理器的休眠结束函数
	trace_machine_suspend(PWR_EVENT_EXIT);
	return error;

 Recover_platform:
	if (suspend_ops->recover)
		suspend_ops->recover();
	goto Resume_devices;
}

/**
 *	suspend_finish - Do final work before exiting suspend sequence.
 *
 *	Call platform code to clean up, restart processes, and free the
 *	console that we've allocated. This is not called for suspend-to-disk.
 */
static void suspend_finish(void)
{
	suspend_thaw_processes();
	usermodehelper_enable();
	pm_notifier_call_chain(PM_POST_SUSPEND);
	pm_restore_console();
}

/**
 *	enter_state - Do common work of entering low-power state.
 *	@state:		pm_state structure for state we're entering.
 *
 *	Make sure we're the only ones trying to enter a sleep state. Fail
 *	if someone has beat us to it, since we don't want anything weird to
 *	happen when we wake up.
 *	Then, do the setup for suspend, enter the state, and cleaup (after
 *	we've woken up).
 */

/*
前面我们分析了休眠的第一个阶段即浅度休眠，现在我们继续看休眠的第二个阶段 — 深度休眠。在深度休眠的过程中
系统会首先冻结所有可以冻结的进程，然后依次挂起所有设备的电源，挂起顺序与设备注册的顺序相反，这样保证了设备
之间电源的依赖性；直至最后进入省电模式，等待用户或者RTC唤醒；在唤醒过程中则会按照设备注册的顺序依次恢复每
个设备的电源进入正常工作状态，解冻相关的进程，然后再进行浅度休眠的唤醒流程。
*/
int enter_state(suspend_state_t state)
{
	int error;

	// 判断平台是否支持该状态  
	if (!valid_state(state))
		return -ENODEV;

	if (!mutex_trylock(&pm_mutex))
		return -EBUSY;

	  // 同步缓存 
	printk(KERN_INFO "PM: Syncing filesystems ... ");
	sys_sync();
	printk("done.\n");

	pr_debug("PM: Preparing system for %s sleep\n", pm_states[state]);

	// 做好休眠准备  冻结大部分进程
	error = suspend_prepare();
	if (error)
		goto Unlock;

	if (suspend_test(TEST_FREEZER))
		goto Finish;

	pr_debug("PM: Entering %s sleep\n", pm_states[state]);
	pm_restrict_gfp_mask();
	error = suspend_devices_and_enter(state);    // 设备休眠 
	pm_restore_gfp_mask();

 Finish:
	pr_debug("PM: Finishing wakeup.\n");
	suspend_finish();
 Unlock:
	mutex_unlock(&pm_mutex);
	return error;
}

/**
 *	pm_suspend - Externally visible function for suspending system.
 *	@state:		Enumerated value of state to enter.
 *
 *	Determine whether or not value is within range, get state
 *	structure, and enter (above).
 */
int pm_suspend(suspend_state_t state)
{
	if (state > PM_SUSPEND_ON && state <= PM_SUSPEND_MAX)
		return enter_state(state);
	return -EINVAL;
}
EXPORT_SYMBOL(pm_suspend);
