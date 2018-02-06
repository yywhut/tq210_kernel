/* drivers/rtc/alarm.c
 *
 * Copyright (C) 2007-2009 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <asm/mach/time.h>
#include <linux/android_alarm.h>
#include <linux/device.h>
#include <linux/miscdevice.h>
#include <linux/platform_device.h>
#include <linux/rtc.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/sysdev.h>
#include <linux/wakelock.h>

#define ANDROID_ALARM_PRINT_ERROR (1U << 0)
#define ANDROID_ALARM_PRINT_INIT_STATUS (1U << 1)
#define ANDROID_ALARM_PRINT_TSET (1U << 2)
#define ANDROID_ALARM_PRINT_CALL (1U << 3)
#define ANDROID_ALARM_PRINT_SUSPEND (1U << 4)
#define ANDROID_ALARM_PRINT_INT (1U << 5)
#define ANDROID_ALARM_PRINT_FLOW (1U << 6)

static int debug_mask = ANDROID_ALARM_PRINT_ERROR | \
			ANDROID_ALARM_PRINT_INIT_STATUS;
module_param_named(debug_mask, debug_mask, int, S_IRUGO | S_IWUSR | S_IWGRP);

#define pr_alarm(debug_level_mask, args...) \
	do { \
		if (debug_mask & ANDROID_ALARM_PRINT_##debug_level_mask) { \
			pr_info(args); \
		} \
	} while (0)

#define ANDROID_ALARM_WAKEUP_MASK ( \
	ANDROID_ALARM_RTC_WAKEUP_MASK | \
	ANDROID_ALARM_ELAPSED_REALTIME_WAKEUP_MASK)

/* support old usespace code */
#define ANDROID_ALARM_SET_OLD               _IOW('a', 2, time_t) /* set alarm */
#define ANDROID_ALARM_SET_AND_WAIT_OLD      _IOW('a', 3, time_t)

//这个结构体用于将前面的 struct alarm 表示的设备组织成红黑树。它是基于内核定时器来实现 alarm 的到期闹铃的
struct alarm_queue {
	struct rb_root alarms;  //红黑树的根  	
	struct rb_node *first; //指向第一个 alarm device,即最早到时的	
	struct hrtimer timer;  //内核定时器,android 利用它来确定 alarm 过期时间
	ktime_t delta;  //是一个计算 elasped realtime 的修正值
	bool stopped;
	ktime_t stopped_time;
};

static struct rtc_device *alarm_rtc_dev;
static DEFINE_SPINLOCK(alarm_slock);
static DEFINE_MUTEX(alarm_setrtc_mutex);
static struct wake_lock alarm_rtc_wake_lock;
static struct platform_device *alarm_platform_dev;
struct alarm_queue alarms[ANDROID_ALARM_TYPE_COUNT];
static bool suspended;

// 如果刚刚插入的是时间最早要alarm的，就需要重新设定时间
static void update_timer_locked(struct alarm_queue *base, bool head_removed)
{
	struct alarm *alarm;
	bool is_wakeup = base == &alarms[ANDROID_ALARM_RTC_WAKEUP] ||
			base == &alarms[ANDROID_ALARM_ELAPSED_REALTIME_WAKEUP];

	if (base->stopped) {
		pr_alarm(FLOW, "changed alarm while setting the wall time\n");
		return;
	}

	if (is_wakeup && !suspended && head_removed)
		wake_unlock(&alarm_rtc_wake_lock);

	if (!base->first)
		return;

	// 这个alarm是红黑树上的一个节点，但是我们现在只知道这个quene的第一个节点的地址
	// 通过这个节点地址，找到大结构体的位置
	alarm = container_of(base->first, struct alarm, node);

	pr_alarm(FLOW, "selected alarm, type %d, func %pF at %lld\n",
		alarm->type, alarm->function, ktime_to_ns(alarm->expires));

	if (is_wakeup && suspended) {
		pr_alarm(FLOW, "changed alarm while suspened\n");
		wake_lock_timeout(&alarm_rtc_wake_lock, 1 * HZ);
		return;
	}

	hrtimer_try_to_cancel(&base->timer);
	base->timer.node.expires = ktime_add(base->delta, alarm->expires);
	base->timer._softexpires = ktime_add(base->delta, alarm->softexpires);
	hrtimer_start_expires(&base->timer, HRTIMER_MODE_ABS);
}

static void alarm_enqueue_locked(struct alarm *alarm)
{
	struct alarm_queue *base = &alarms[alarm->type];
	struct rb_node **link = &base->alarms.rb_node;
	struct rb_node *parent = NULL;
	struct alarm *entry;
	int leftmost = 1;
	bool was_first = false;

	pr_alarm(FLOW, "added alarm, type %d, func %pF at %lld\n",
		alarm->type, alarm->function, ktime_to_ns(alarm->expires));

	if (base->first == &alarm->node) { //判断是否为base中最先触发的一个
		base->first = rb_next(&alarm->node);
		was_first = true;
	}
	if (!RB_EMPTY_NODE(&alarm->node)) {
		rb_erase(&alarm->node, &base->alarms);
		RB_CLEAR_NODE(&alarm->node);
	}

	while (*link) {
		parent = *link;
		entry = rb_entry(parent, struct alarm, node);
		/*
		* We dont care about collisions. Nodes with
		* the same expiry time stay together.
		*/
		if (alarm->expires.tv64 < entry->expires.tv64) {
			link = &(*link)->rb_left;
		} else {
			link = &(*link)->rb_right;
			leftmost = 0;  //判断是否在最左端  
		}
	}
	// 如果上面代码都没有跑，那肯定是在第一个
	if (leftmost)
		base->first = &alarm->node;
	if (leftmost || was_first)
		update_timer_locked(base, was_first); //如果插入的是在最左边，或是第一个要被触发，
												//则需要更新base timer  

	rb_link_node(&alarm->node, parent, link);  //将节点插入到既有的红黑树中。  
	rb_insert_color(&alarm->node, &base->alarms);
}

/**
 * alarm_init - initialize an alarm
 * @alarm:	the alarm to be initialized
 * @type:	the alarm type to be used
 * @function:	alarm callback function
 */
void alarm_init(struct alarm *alarm,
	enum android_alarm_type type, void (*function)(struct alarm *))
{
	RB_CLEAR_NODE(&alarm->node);
	alarm->type = type;
	alarm->function = function;

	pr_alarm(FLOW, "created alarm, type %d, func %pF\n", type, function);
}


/**
 * alarm_start_range - (re)start an alarm
 * @alarm:	the alarm to be added
 * @start:	earliest expiry time
 * @end:	expiry time
 */
void alarm_start_range(struct alarm *alarm, ktime_t start, ktime_t end)
{
	unsigned long flags;

	spin_lock_irqsave(&alarm_slock, flags);
	alarm->softexpires = start;
	alarm->expires = end;
	alarm_enqueue_locked(alarm);   // 使能 hr timer
	spin_unlock_irqrestore(&alarm_slock, flags);
}

/**
 * alarm_try_to_cancel - try to deactivate an alarm
 * @alarm:	alarm to stop
 *
 * Returns:
 *  0 when the alarm was not active
 *  1 when the alarm was active
 * -1 when the alarm may currently be excuting the callback function and
 *    cannot be stopped (it may also be inactive)
 */
int alarm_try_to_cancel(struct alarm *alarm)
{
	struct alarm_queue *base = &alarms[alarm->type];
	unsigned long flags;
	bool first = false;
	int ret = 0;

	spin_lock_irqsave(&alarm_slock, flags);
	if (!RB_EMPTY_NODE(&alarm->node)) {
		pr_alarm(FLOW, "canceled alarm, type %d, func %pF at %lld\n",
			alarm->type, alarm->function,
			ktime_to_ns(alarm->expires));
		ret = 1;
		if (base->first == &alarm->node) {
			base->first = rb_next(&alarm->node);
			first = true;
		}
		rb_erase(&alarm->node, &base->alarms);
		RB_CLEAR_NODE(&alarm->node);
		if (first)
			update_timer_locked(base, true);
	} else
		pr_alarm(FLOW, "tried to cancel alarm, type %d, func %pF\n",
			alarm->type, alarm->function);
	spin_unlock_irqrestore(&alarm_slock, flags);
	if (!ret && hrtimer_callback_running(&base->timer))
		ret = -1;
	return ret;
}

/**
 * alarm_cancel - cancel an alarm and wait for the handler to finish.
 * @alarm:	the alarm to be cancelled
 *
 * Returns:
 *  0 when the alarm was not active
 *  1 when the alarm was active
 */
int alarm_cancel(struct alarm *alarm)
{
	for (;;) {
		int ret = alarm_try_to_cancel(alarm);
		if (ret >= 0)
			return ret;
		cpu_relax();
	}
}

/**
 * alarm_set_rtc - set the kernel and rtc walltime
 * @new_time:	timespec value containing the new time
 */
int alarm_set_rtc(struct timespec new_time)
{
	int i;
	int ret;
	unsigned long flags;
	struct rtc_time rtc_new_rtc_time;
	struct timespec tmp_time;

	rtc_time_to_tm(new_time.tv_sec, &rtc_new_rtc_time);

	pr_alarm(TSET, "set rtc %ld %ld - rtc %02d:%02d:%02d %02d/%02d/%04d\n",
		new_time.tv_sec, new_time.tv_nsec,
		rtc_new_rtc_time.tm_hour, rtc_new_rtc_time.tm_min,
		rtc_new_rtc_time.tm_sec, rtc_new_rtc_time.tm_mon + 1,
		rtc_new_rtc_time.tm_mday,
		rtc_new_rtc_time.tm_year + 1900);

	mutex_lock(&alarm_setrtc_mutex);
	spin_lock_irqsave(&alarm_slock, flags);
	wake_lock(&alarm_rtc_wake_lock);
	getnstimeofday(&tmp_time);
	for (i = 0; i < ANDROID_ALARM_SYSTEMTIME; i++) {
		hrtimer_try_to_cancel(&alarms[i].timer);
		alarms[i].stopped = true;
		alarms[i].stopped_time = timespec_to_ktime(tmp_time);
	}
	alarms[ANDROID_ALARM_ELAPSED_REALTIME_WAKEUP].delta =
		alarms[ANDROID_ALARM_ELAPSED_REALTIME].delta =
		ktime_sub(alarms[ANDROID_ALARM_ELAPSED_REALTIME].delta,
			timespec_to_ktime(timespec_sub(tmp_time, new_time)));
	spin_unlock_irqrestore(&alarm_slock, flags);
	ret = do_settimeofday(&new_time);
	spin_lock_irqsave(&alarm_slock, flags);
	for (i = 0; i < ANDROID_ALARM_SYSTEMTIME; i++) {
		alarms[i].stopped = false;
		update_timer_locked(&alarms[i], false);
	}
	spin_unlock_irqrestore(&alarm_slock, flags);
	if (ret < 0) {
		pr_alarm(ERROR, "alarm_set_rtc: Failed to set time\n");
		goto err;
	}
	if (!alarm_rtc_dev) {
		pr_alarm(ERROR,
			"alarm_set_rtc: no RTC, time will be lost on reboot\n");
		goto err;
	}
	ret = rtc_set_time(alarm_rtc_dev, &rtc_new_rtc_time);
	if (ret < 0)
		pr_alarm(ERROR, "alarm_set_rtc: "
			"Failed to set RTC, time will be lost on reboot\n");
err:
	wake_unlock(&alarm_rtc_wake_lock);
	mutex_unlock(&alarm_setrtc_mutex);
	return ret;
}

/**
 * alarm_get_elapsed_realtime - get the elapsed real time in ktime_t format
 *
 * returns the time in ktime_t format
 */
ktime_t alarm_get_elapsed_realtime(void)
{
	ktime_t now;
	unsigned long flags;
	struct alarm_queue *base = &alarms[ANDROID_ALARM_ELAPSED_REALTIME];

	spin_lock_irqsave(&alarm_slock, flags);
	now = base->stopped ? base->stopped_time : ktime_get_real();
	now = ktime_sub(now, base->delta);
	spin_unlock_irqrestore(&alarm_slock, flags);
	return now;
}

static enum hrtimer_restart alarm_timer_triggered(struct hrtimer *timer)
{
	struct alarm_queue *base;
	struct alarm *alarm;
	unsigned long flags;
	ktime_t now;

	spin_lock_irqsave(&alarm_slock, flags);

	base = container_of(timer, struct alarm_queue, timer);
	now = base->stopped ? base->stopped_time : hrtimer_cb_get_time(timer);
	now = ktime_sub(now, base->delta);

	pr_alarm(INT, "alarm_timer_triggered type %d at %lld\n",
		base - alarms, ktime_to_ns(now));

	while (base->first) {
		alarm = container_of(base->first, struct alarm, node);
		if (alarm->softexpires.tv64 > now.tv64) {
			pr_alarm(FLOW, "don't call alarm, %pF, %lld (s %lld)\n",
				alarm->function, ktime_to_ns(alarm->expires),
				ktime_to_ns(alarm->softexpires));
			break;
		}
		base->first = rb_next(&alarm->node);
		rb_erase(&alarm->node, &base->alarms);
		RB_CLEAR_NODE(&alarm->node);
		pr_alarm(CALL, "call alarm, type %d, func %pF, %lld (s %lld)\n",
			alarm->type, alarm->function,
			ktime_to_ns(alarm->expires),
			ktime_to_ns(alarm->softexpires));
		spin_unlock_irqrestore(&alarm_slock, flags);
		alarm->function(alarm);
		spin_lock_irqsave(&alarm_slock, flags);
	}
	if (!base->first)
		pr_alarm(FLOW, "no more alarms of type %d\n", base - alarms);
	update_timer_locked(base, true);
	spin_unlock_irqrestore(&alarm_slock, flags);
	return HRTIMER_NORESTART;
}

static void alarm_triggered_func(void *p)
{
	struct rtc_device *rtc = alarm_rtc_dev;
	if (!(rtc->irq_data & RTC_AF))
		return;
	pr_alarm(INT, "rtc alarm triggered\n");
	wake_lock_timeout(&alarm_rtc_wake_lock, 1 * HZ);  //wake_lock_timeout锁住alarm_rtc_wake_lock 1秒。
	//因为这时，alarm会进入alarm_resume，lock住alarm_rtc_wake_lock以防止alarm在此时进入suspend。
}

static int alarm_suspend(struct platform_device *pdev, pm_message_t state)
{
	int                 err = 0;
	unsigned long       flags;
	struct rtc_wkalrm   rtc_alarm;
	struct rtc_time     rtc_current_rtc_time;
	unsigned long       rtc_current_time;
	unsigned long       rtc_alarm_time;
	struct timespec     rtc_delta;
	struct timespec     wall_time;
	struct alarm_queue *wakeup_queue = NULL;
	struct alarm_queue *tmp_queue = NULL;

	pr_alarm(SUSPEND, "alarm_suspend(%p, %d)\n", pdev, state.event);

	spin_lock_irqsave(&alarm_slock, flags);
	suspended = true;
	spin_unlock_irqrestore(&alarm_slock, flags);

	hrtimer_cancel(&alarms[ANDROID_ALARM_RTC_WAKEUP].timer);
	hrtimer_cancel(&alarms[
			ANDROID_ALARM_ELAPSED_REALTIME_WAKEUP].timer);

	tmp_queue = &alarms[ANDROID_ALARM_RTC_WAKEUP];
	if (tmp_queue->first)
		wakeup_queue = tmp_queue;  // 这里执行了
	tmp_queue = &alarms[ANDROID_ALARM_ELAPSED_REALTIME_WAKEUP];
	if (tmp_queue->first && (!wakeup_queue ||
				hrtimer_get_expires(&tmp_queue->timer).tv64 <
				hrtimer_get_expires(&wakeup_queue->timer).tv64))
		wakeup_queue = tmp_queue; // 这里也执行了
	if (wakeup_queue) {
		rtc_read_time(alarm_rtc_dev, &rtc_current_rtc_time);
		getnstimeofday(&wall_time);
		rtc_tm_to_time(&rtc_current_rtc_time, &rtc_current_time);
		set_normalized_timespec(&rtc_delta,
					wall_time.tv_sec - rtc_current_time,
					wall_time.tv_nsec);

	/*
			[  130.884054] rtc_read_time rtc: 2000,1,1,  6:26:47
	[  130.884061] wall_time 946708006 ,826530793
	[  130.884071] wall_time to  rtc: 2000,1,1, 6:26:47
	[  130.884078] rtc_current_time 946708007
	[  130.884084] rtc_delta -1	
	[  130.884094] hrtimer_get_expires rtc: 2000,1,1,  6:32:47
	[  130.884109] s3c_rtc_setalarm: 1, 2000.01.01 06:32:46
	[  130.884118] setting S3C2410_RTCALM to 00000047
	[  130.884124] s3c_rtc_setaie: aie=1
	[  130.884134] rtc alarm set at 946708366, now 946708007, rtc delta -1.826530793
	[  130.884143] rtc alarm set at: 2000,1,1,  6:32:46
	*/

		rtc_alarm_time = timespec_sub(ktime_to_timespec(
			hrtimer_get_expires(&wakeup_queue->timer)),
			rtc_delta).tv_sec;
		// 这里就是设定要唤醒的时间


		//eason change to test
		//rtc_alarm_time = rtc_current_time + 10;  // this can suspend and wake up every 10s

		rtc_time_to_tm(rtc_alarm_time, &rtc_alarm.time);
		rtc_alarm.enabled = 1;
		rtc_set_alarm(alarm_rtc_dev, &rtc_alarm);
		rtc_read_time(alarm_rtc_dev, &rtc_current_rtc_time);
		rtc_tm_to_time(&rtc_current_rtc_time, &rtc_current_time);
		pr_alarm(SUSPEND,
			"rtc alarm set at %ld, now %ld, rtc delta %ld.%09ld\n",
			rtc_alarm_time, rtc_current_time,
			rtc_delta.tv_sec, rtc_delta.tv_nsec);
		if (rtc_current_time + 1 >= rtc_alarm_time) {
			pr_alarm(SUSPEND, "alarm about to go off\n");
			memset(&rtc_alarm, 0, sizeof(rtc_alarm));
			rtc_alarm.enabled = 0;
			rtc_set_alarm(alarm_rtc_dev, &rtc_alarm);

			spin_lock_irqsave(&alarm_slock, flags);
			suspended = false;
			wake_lock_timeout(&alarm_rtc_wake_lock, 2 * HZ);
			update_timer_locked(&alarms[ANDROID_ALARM_RTC_WAKEUP],
									false);
			update_timer_locked(&alarms[
				ANDROID_ALARM_ELAPSED_REALTIME_WAKEUP], false);
			err = -EBUSY;
			spin_unlock_irqrestore(&alarm_slock, flags);
		}
	}
	return err;
}

static int alarm_resume(struct platform_device *pdev)
{
	struct rtc_wkalrm alarm;
	unsigned long       flags;

	pr_alarm(SUSPEND, "alarm_resume(%p)\n", pdev);

	memset(&alarm, 0, sizeof(alarm));
	alarm.enabled = 0;
	rtc_set_alarm(alarm_rtc_dev, &alarm);

	spin_lock_irqsave(&alarm_slock, flags);
	suspended = false;
	update_timer_locked(&alarms[ANDROID_ALARM_RTC_WAKEUP], false);
	update_timer_locked(&alarms[ANDROID_ALARM_ELAPSED_REALTIME_WAKEUP],
									false);
	spin_unlock_irqrestore(&alarm_slock, flags);

	return 0;
}

static struct rtc_task alarm_rtc_task = {
	.func = alarm_triggered_func
};

static int rtc_alarm_add_device(struct device *dev,
				struct class_interface *class_intf)
{
	int err;
	struct rtc_device *rtc = to_rtc_device(dev);

	mutex_lock(&alarm_setrtc_mutex);

	if (alarm_rtc_dev) {
		err = -EBUSY;
		goto err1;
	}

	alarm_platform_dev =
		platform_device_register_simple("alarm", -1, NULL, 0);  //并且将其注册为platform设备，支持suspend和resume。
	if (IS_ERR(alarm_platform_dev)) {
		err = PTR_ERR(alarm_platform_dev);
		goto err2;
	}
	err = rtc_irq_register(rtc, &alarm_rtc_task);  // 注册RTC 中断回调函数
	if (err)
		goto err3;
	alarm_rtc_dev = rtc;
	pr_alarm(INIT_STATUS, "using rtc device, %s, for alarms", rtc->name);
	mutex_unlock(&alarm_setrtc_mutex);

	return 0;

err3:
	platform_device_unregister(alarm_platform_dev);
err2:
err1:
	mutex_unlock(&alarm_setrtc_mutex);
	return err;
}

static void rtc_alarm_remove_device(struct device *dev,
				    struct class_interface *class_intf)
{
	if (dev == &alarm_rtc_dev->dev) {
		pr_alarm(INIT_STATUS, "lost rtc device for alarms");
		rtc_irq_unregister(alarm_rtc_dev, &alarm_rtc_task);
		platform_device_unregister(alarm_platform_dev);
		alarm_rtc_dev = NULL;
	}
}

static struct class_interface rtc_alarm_interface = {
	.add_dev = &rtc_alarm_add_device,
	.remove_dev = &rtc_alarm_remove_device,
};

/*
static struct miscdevice alarm_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "alarm",
	.fops = &alarm_fops,
};
dev 在 alarm_dev.c中定义
*/




static struct platform_driver alarm_driver = {
	.suspend = alarm_suspend,
	.resume = alarm_resume,
	.driver = {
		.name = "alarm"
	}
};

static int __init alarm_late_init(void)
{
	unsigned long   flags;
	struct timespec tmp_time, system_time;

	/* this needs to run after the rtc is read at boot */
	spin_lock_irqsave(&alarm_slock, flags);
	/* We read the current rtc and system time so we can later calulate
	 * elasped realtime to be (boot_systemtime + rtc - boot_rtc) ==
	 * (rtc - (boot_rtc - boot_systemtime))
	 */
	getnstimeofday(&tmp_time); // 获取当前时间，返回timespec结构
	ktime_get_ts(&system_time);  // 获取系统启动以来所经过的c时间，不包含休眠时间，返回timespec结构
//[    5.965162] s3c-rtc s3c64xx-rtc: setting system clock to 2000-01-01 00:00:01 UTC (946684801)
//[    5.943484] tem_time :946697338   ,  508331873  
//[    5.948034] system_time :5   ,  946234934 
//[    5.952106] alarms[ANDROID_ALARM_ELAPSED_REALTIME_WAKEUP].delta:946697332562096939 

//当前 系统时间为 946697338。这个是从rtc中读出，并且已经被转换成UTC 时间了，这个时间再减去当前
// 系统启动时间，就是定时的偏移， 也就是 946697338 - 5 ,假如你的alarm设定时间也是  946697338，
//  那就表明第0s的时候开始响铃，如果是946697339，那就是第1s开始响铃。
// 但是软件刚好是个反的写过程，软件写的时候会直接写 10到hrtimer中去，表明第10s开始响铃，
// 内核得到这个10 会加上delat 算出rtc的时间。

	alarms[ANDROID_ALARM_ELAPSED_REALTIME_WAKEUP].delta =
		alarms[ANDROID_ALARM_ELAPSED_REALTIME].delta =
			timespec_to_ktime(timespec_sub(tmp_time, system_time));

	spin_unlock_irqrestore(&alarm_slock, flags);
	return 0;
}

static int __init alarm_driver_init(void)
{
	int err;
	int i;

//初始化了5个hrtimer高精度定时器

//在初始化hrtimer定时器时，前四个注册的是CLOCK_REALTIME,
//而最后一种类型ANDROID_ALARM_SYSTEMTIME注册的是CLOCK_MONOTONIC类型。

/*
CLOCK_REALTIME：这种类型的时钟可以反映wall clock time，用的是绝对时间，当系统的时钟源被改变，或者系统管理员
//重置了系统时间之后，这种类型的时钟可以
得到相应的调整，也就是说，系统时间影响这种类型的timer。
CLOCK_MONOTONIC：用的是相对时间，他的时间是通过jiffies值来计算的。该时钟不受系统时钟源的影响，
//只受jiffies值的影响。

建议使用：
CLOCK_MONOTONIC这种时钟更加稳定，不受系统时钟的影响。如果想反映wall clock time，就使用CLOCK_REALTIME。



*/
	for (i = 0; i < ANDROID_ALARM_SYSTEMTIME; i++) {
		hrtimer_init(&alarms[i].timer,
				CLOCK_REALTIME, HRTIMER_MODE_ABS);
		alarms[i].timer.function = alarm_timer_triggered;
	}
	hrtimer_init(&alarms[ANDROID_ALARM_SYSTEMTIME].timer,
		     CLOCK_MONOTONIC, HRTIMER_MODE_ABS);
	alarms[ANDROID_ALARM_SYSTEMTIME].timer.function = alarm_timer_triggered;
	err = platform_driver_register(&alarm_driver);
	if (err < 0)
		goto err1;
	wake_lock_init(&alarm_rtc_wake_lock, WAKE_LOCK_SUSPEND, "alarm_rtc");
	rtc_alarm_interface.class = rtc_class;
	err = class_interface_register(&rtc_alarm_interface);
	if (err < 0)
		goto err2;

	return 0;

err2:
	wake_lock_destroy(&alarm_rtc_wake_lock);
	platform_driver_unregister(&alarm_driver);
err1:
	return err;
}

static void  __exit alarm_exit(void)
{
	class_interface_unregister(&rtc_alarm_interface);
	wake_lock_destroy(&alarm_rtc_wake_lock);
	platform_driver_unregister(&alarm_driver);
}

/*
链接顺序为:	
rtc_hctosys
rtc_init
rtc_dev_init
rtc_sysfs_init
alarm_late_init
alarm_driver_init
alarm_dev_init
s3c_rtc_init


因为 alarm跟s3c_rtc_init 优先级相同，所以
按照连接顺序执行，也就是 s3c_rtc_init
后执行
所以class_interface_register 中才没有添加class_intf->add_dev(dev, class_intf); // 这里确实没有执行
*/






late_initcall(alarm_late_init);
module_init(alarm_driver_init);
module_exit(alarm_exit);

