/* kernel/power/wakelock.c
 *
 * Copyright (C) 2005-2008 Google, Inc.
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

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/rtc.h>
#include <linux/suspend.h>
#include <linux/syscalls.h> /* sys_sync */
#include <linux/wakelock.h>
#ifdef CONFIG_WAKELOCK_STAT
#include <linux/proc_fs.h>
#endif
#include "power.h"

enum {
	DEBUG_EXIT_SUSPEND = 1U << 0,
	DEBUG_WAKEUP = 1U << 1,
	DEBUG_SUSPEND = 1U << 2,
	DEBUG_EXPIRE = 1U << 3,
	DEBUG_WAKE_LOCK = 1U << 4,
};
static int debug_mask = DEBUG_EXIT_SUSPEND | DEBUG_WAKEUP;
module_param_named(debug_mask, debug_mask, int, S_IRUGO | S_IWUSR | S_IWGRP);

#define WAKE_LOCK_TYPE_MASK              (0x0f)
#define WAKE_LOCK_INITIALIZED            (1U << 8)
#define WAKE_LOCK_ACTIVE                 (1U << 9)
#define WAKE_LOCK_AUTO_EXPIRE            (1U << 10)
#define WAKE_LOCK_PREVENTING_SUSPEND     (1U << 11)

static DEFINE_SPINLOCK(list_lock);  // 读写锁链表的自旋锁
static LIST_HEAD(inactive_locks);  //记录所有处于 inactive状态的锁
static struct list_head active_wake_locks[WAKE_LOCK_TYPE_COUNT];
static int current_event_num;// 休眠锁使用计数器
struct workqueue_struct *suspend_work_queue;// 执行系统休眠的工作队列 
struct wake_lock main_wake_lock; // 内核休眠锁 
suspend_state_t requested_suspend_state = PM_SUSPEND_MEM;// 系统休眠状态
static struct wake_lock unknown_wakeup;
static struct wake_lock suspend_backoff_lock;

#define SUSPEND_BACKOFF_THRESHOLD	10
#define SUSPEND_BACKOFF_INTERVAL	10000

static unsigned suspend_short_count;

#ifdef CONFIG_WAKELOCK_STAT
static struct wake_lock deleted_wake_locks;
static ktime_t last_sleep_time_update;
static int wait_for_wakeup;

//proc 节点
// 获取锁的剩余超时时间,通过*expire_time传递  
int get_expired_time(struct wake_lock *lock, ktime_t *expire_time)
{
	struct timespec ts;
	struct timespec kt;
	struct timespec tomono;
	struct timespec delta;
	struct timespec sleep;
	long timeout;

	// 如果不是超时锁则直接返回 
	if (!(lock->flags & WAKE_LOCK_AUTO_EXPIRE))
		return 0;
	get_xtime_and_monotonic_and_sleep_offset(&kt, &tomono, &sleep);
	timeout = lock->expires - jiffies; // 计算超时时间点与当前时间的差值  
	if (timeout > 0) // 如果时间没有到期,返回0 
		return 0;
	jiffies_to_timespec(-timeout, &delta);  // 时间格式转换  
	set_normalized_timespec(&ts, kt.tv_sec + tomono.tv_sec - delta.tv_sec,
				kt.tv_nsec + tomono.tv_nsec - delta.tv_nsec);
	*expire_time = timespec_to_ktime(ts);  // 返回ts值 
	return 1;
}

// 打印出锁的状态信息
static int print_lock_stat(struct seq_file *m, struct wake_lock *lock)
{
	int lock_count = lock->stat.count;
	int expire_count = lock->stat.expire_count;
	ktime_t active_time = ktime_set(0, 0);
	ktime_t total_time = lock->stat.total_time;
	ktime_t max_time = lock->stat.max_time;

	ktime_t prevent_suspend_time = lock->stat.prevent_suspend_time;

	// 如果锁有效 
	if (lock->flags & WAKE_LOCK_ACTIVE) {
		ktime_t now, add_time;
		int expired = get_expired_time(lock, &now); // 获取超时剩余时间  
		if (!expired)
			now = ktime_get();

		 // 计算当前时间和上次操作时间的差值
		add_time = ktime_sub(now, lock->stat.last_time);
		lock_count++; // 使用计数加1  
		if (!expired)  // 如果没有到期  
			active_time = add_time;
		else // 锁已经到期
			expire_count++;  // 超时计数加1  
		total_time = ktime_add(total_time, add_time);  // 锁使用时间增加  
		if (lock->flags & WAKE_LOCK_PREVENTING_SUSPEND)
			prevent_suspend_time = ktime_add(prevent_suspend_time,
					ktime_sub(now, last_sleep_time_update));
		if (add_time.tv64 > max_time.tv64)
			max_time = add_time;
	}

	return seq_printf(m,
		     "\"%s\"\t%d\t%d\t%d\t%lld\t%lld\t%lld\t%lld\t%lld\n",
		     lock->name, lock_count, expire_count,
		     lock->stat.wakeup_count, ktime_to_ns(active_time),
		     ktime_to_ns(total_time),
		     ktime_to_ns(prevent_suspend_time), ktime_to_ns(max_time),
		     ktime_to_ns(lock->stat.last_time));
}

// 打印锁状态 
static int wakelock_stats_show(struct seq_file *m, void *unused)
{
	unsigned long irqflags;
	struct wake_lock *lock;
	int ret;
	int type;

	spin_lock_irqsave(&list_lock, irqflags);

	// 输出菜单  
	ret = seq_puts(m, "name\tcount\texpire_count\twake_count\tactive_since"
			"\ttotal_time\tsleep_time\tmax_time\tlast_change\n");
	 // 遍历无效锁链表并打印锁的状态信息
	list_for_each_entry(lock, &inactive_locks, link)
		ret = print_lock_stat(m, lock);

	 // 遍历有效锁链表并打印锁的状态信息  
	for (type = 0; type < WAKE_LOCK_TYPE_COUNT; type++) {
		list_for_each_entry(lock, &active_wake_locks[type], link)
			ret = print_lock_stat(m, lock);
	}
	spin_unlock_irqrestore(&list_lock, irqflags);
	return 0;
}

// proc文件打开函数,调用show函数显示当前所有的锁信息 
static void wake_unlock_stat_locked(struct wake_lock *lock, int expired)
{
	ktime_t duration;
	ktime_t now;
	if (!(lock->flags & WAKE_LOCK_ACTIVE))
		return;
	if (get_expired_time(lock, &now))
		expired = 1;
	else
		now = ktime_get();
	lock->stat.count++;
	if (expired)
		lock->stat.expire_count++;
	duration = ktime_sub(now, lock->stat.last_time);
	lock->stat.total_time = ktime_add(lock->stat.total_time, duration);
	if (ktime_to_ns(duration) > ktime_to_ns(lock->stat.max_time))
		lock->stat.max_time = duration;
	lock->stat.last_time = ktime_get();
	if (lock->flags & WAKE_LOCK_PREVENTING_SUSPEND) {
		duration = ktime_sub(now, last_sleep_time_update);
		lock->stat.prevent_suspend_time = ktime_add(
			lock->stat.prevent_suspend_time, duration);
		lock->flags &= ~WAKE_LOCK_PREVENTING_SUSPEND;
	}
}

static void update_sleep_wait_stats_locked(int done)
{
	struct wake_lock *lock;
	ktime_t now, etime, elapsed, add;
	int expired;

	now = ktime_get();
	elapsed = ktime_sub(now, last_sleep_time_update);
	list_for_each_entry(lock, &active_wake_locks[WAKE_LOCK_SUSPEND], link) {
		expired = get_expired_time(lock, &etime);
		if (lock->flags & WAKE_LOCK_PREVENTING_SUSPEND) {
			if (expired)
				add = ktime_sub(etime, last_sleep_time_update);
			else
				add = elapsed;
			lock->stat.prevent_suspend_time = ktime_add(
				lock->stat.prevent_suspend_time, add);
		}
		if (done || expired)
			lock->flags &= ~WAKE_LOCK_PREVENTING_SUSPEND;
		else
			lock->flags |= WAKE_LOCK_PREVENTING_SUSPEND;
	}
	last_sleep_time_update = now;
}
#endif


static void expire_wake_lock(struct wake_lock *lock)
{
#ifdef CONFIG_WAKELOCK_STAT
	wake_unlock_stat_locked(lock, 1);
#endif
	lock->flags &= ~(WAKE_LOCK_ACTIVE | WAKE_LOCK_AUTO_EXPIRE);
	list_del(&lock->link);
	list_add(&lock->link, &inactive_locks);
	if (debug_mask & (DEBUG_WAKE_LOCK | DEBUG_EXPIRE))
		pr_info("expired wake lock %s\n", lock->name);
}

/* Caller must acquire the list_lock spinlock */
static void print_active_locks(int type)
{
	struct wake_lock *lock;
	bool print_expired = true;

	BUG_ON(type >= WAKE_LOCK_TYPE_COUNT);
	list_for_each_entry(lock, &active_wake_locks[type], link) {
		if (lock->flags & WAKE_LOCK_AUTO_EXPIRE) {
			long timeout = lock->expires - jiffies;
			if (timeout > 0)
				pr_info("active wake lock %s, time left %ld\n",
					lock->name, timeout);
			else if (print_expired)
				pr_info("wake lock %s, expired\n", lock->name);
		} else {
			pr_info("active wake lock %s\n", lock->name);
			if (!(debug_mask & DEBUG_EXPIRE))
				print_expired = false;
		}
	}
}


/*
has_wake_lock()用于判断系统中是否还存在有效的type型锁，如果存在超时锁则返回最长的一个锁的超时时间，
如果存在永久锁则返回-1，如果系统中不存在有效锁则返回0。
*/

static long has_wake_lock_locked(int type)
{
	struct wake_lock *lock, *n;
	long max_timeout = 0;

	BUG_ON(type >= WAKE_LOCK_TYPE_COUNT);
	list_for_each_entry_safe(lock, n, &active_wake_locks[type], link) {
		if (lock->flags & WAKE_LOCK_AUTO_EXPIRE) {
			long timeout = lock->expires - jiffies;   // 计算超时剩余时间 
			if (timeout <= 0)
				expire_wake_lock(lock);   // 解除超时锁 // 移除过期锁  
			else if (timeout > max_timeout)
				max_timeout = timeout;   // 得到最长的一个超时时间 
		} else
			return -1;  // // 如果不是超时锁则返回-1  ，表明是永久锁
	}
	return max_timeout;   // 返回最长的一个锁的超时时间，或者返回0表示没有锁
}

long has_wake_lock(int type)
{
	long ret;
	unsigned long irqflags;
	spin_lock_irqsave(&list_lock, irqflags);
	ret = has_wake_lock_locked(type);
	if (ret && (debug_mask & DEBUG_WAKEUP) && type == WAKE_LOCK_SUSPEND)
		print_active_locks(type);
	spin_unlock_irqrestore(&list_lock, irqflags);
	return ret;
}

static void suspend_backoff(void)
{
	pr_info("suspend: too many immediate wakeups, back off\n");
	wake_lock_timeout(&suspend_backoff_lock,
			  msecs_to_jiffies(SUSPEND_BACKOFF_INTERVAL));
}


/*
声明工作队列用于内核深度休眠，可以看到一个正常的休眠流程会三次调用sys_sync()用于同步缓存（之前一次在浅度休眠，
之后一次在深度休眠），然后调用pm_suspend()开始执行深度休眠流程。
*/
static void suspend(struct work_struct *work)
{
	int ret;
	int entry_event_num;
	struct timespec ts_entry, ts_exit;

	// 判断系统是否还持有有效锁,如果有则直接返回  
	if (has_wake_lock(WAKE_LOCK_SUSPEND)) {
		if (debug_mask & DEBUG_SUSPEND)
			pr_info("suspend: abort suspend\n");
		return;
	}

	 // 记录函数进入时休眠锁的使用次数  
	entry_event_num = current_event_num;
	sys_sync();  // 将缓存中的数据写入磁盘
	
	if (debug_mask & DEBUG_SUSPEND)
		pr_info("suspend: enter suspend\n");
	getnstimeofday(&ts_entry);
	ret = pm_suspend(requested_suspend_state);   // 进入休眠
	getnstimeofday(&ts_exit);

	if (debug_mask & DEBUG_EXIT_SUSPEND) {
		struct rtc_time tm;
		rtc_time_to_tm(ts_exit.tv_sec, &tm);
		pr_info("suspend: exit suspend, ret = %d "
			"(%d-%02d-%02d %02d:%02d:%02d.%09lu UTC)\n", ret,
			tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
			tm.tm_hour, tm.tm_min, tm.tm_sec, ts_exit.tv_nsec);
	}

	if (ts_exit.tv_sec - ts_entry.tv_sec <= 1) {
		++suspend_short_count;

		if (suspend_short_count == SUSPEND_BACKOFF_THRESHOLD) {
			suspend_backoff();
			suspend_short_count = 0;
		}
	} else {
		suspend_short_count = 0;
	}

	// 如果深度休眠前和深度休眠后锁的使用次数一致，即唤醒过程中没有激活新的锁
	if (current_event_num == entry_event_num) {
		if (debug_mask & DEBUG_SUSPEND)
			pr_info("suspend: pm_suspend returned with no event\n");
		wake_lock_timeout(&unknown_wakeup, HZ / 2);
	}
}
static DECLARE_WORK(suspend_work, suspend);


// 看看还有没有锁，没有锁就开始休眠
static void expire_wake_locks(unsigned long data)
{
	long has_lock;
	unsigned long irqflags;
	if (debug_mask & DEBUG_EXPIRE)
		pr_info("expire_wake_locks: start\n");
	spin_lock_irqsave(&list_lock, irqflags);
	if (debug_mask & DEBUG_SUSPEND)
		print_active_locks(WAKE_LOCK_SUSPEND);
	has_lock = has_wake_lock_locked(WAKE_LOCK_SUSPEND);
	if (debug_mask & DEBUG_EXPIRE)
		pr_info("expire_wake_locks: done, has_lock %ld\n", has_lock);
	if (has_lock == 0)
		queue_work(suspend_work_queue, &suspend_work);   // 启动休眠的工作队列
	spin_unlock_irqrestore(&list_lock, irqflags);
}
static DEFINE_TIMER(expire_timer, expire_wake_locks, 0, 0);

static int power_suspend_late(struct device *dev)
{
	int ret = has_wake_lock(WAKE_LOCK_SUSPEND) ? -EAGAIN : 0;
#ifdef CONFIG_WAKELOCK_STAT
	wait_for_wakeup = !ret;
#endif
	if (debug_mask & DEBUG_SUSPEND)
		pr_info("power_suspend_late return %d\n", ret);
	return ret;
}

static struct dev_pm_ops power_driver_pm_ops = {
	.suspend_noirq = power_suspend_late,
};

static struct platform_driver power_driver = {
	.driver.name = "power",
	.driver.pm = &power_driver_pm_ops,
};
static struct platform_device power_device = {
	.name = "power",
};

void wake_lock_init(struct wake_lock *lock, int type, const char *name)
{
	unsigned long irqflags = 0;

	if (name)
		lock->name = name;
	BUG_ON(!lock->name);

	if (debug_mask & DEBUG_WAKE_LOCK)
		pr_info("wake_lock_init name=%s\n", lock->name);
#ifdef CONFIG_WAKELOCK_STAT
	lock->stat.count = 0;
	lock->stat.expire_count = 0;
	lock->stat.wakeup_count = 0;
	lock->stat.total_time = ktime_set(0, 0);
	lock->stat.prevent_suspend_time = ktime_set(0, 0);
	lock->stat.max_time = ktime_set(0, 0);
	lock->stat.last_time = ktime_set(0, 0);
#endif
	lock->flags = (type & WAKE_LOCK_TYPE_MASK) | WAKE_LOCK_INITIALIZED;

	INIT_LIST_HEAD(&lock->link);
	spin_lock_irqsave(&list_lock, irqflags);
	list_add(&lock->link, &inactive_locks);
	spin_unlock_irqrestore(&list_lock, irqflags);
}
EXPORT_SYMBOL(wake_lock_init);


//该函数用于注销wake_lock，首先清除 WAKE_LOCK_INITIALIZED 标志位，然后更新统计信息，最后将锁从链表中删除
void wake_lock_destroy(struct wake_lock *lock)
{
	unsigned long irqflags;
	if (debug_mask & DEBUG_WAKE_LOCK)
		pr_info("wake_lock_destroy name=%s\n", lock->name);
	spin_lock_irqsave(&list_lock, irqflags);
	lock->flags &= ~WAKE_LOCK_INITIALIZED;
#ifdef CONFIG_WAKELOCK_STAT
	if (lock->stat.count) {
		deleted_wake_locks.stat.count += lock->stat.count;
		deleted_wake_locks.stat.expire_count += lock->stat.expire_count;
		deleted_wake_locks.stat.total_time =
			ktime_add(deleted_wake_locks.stat.total_time,
				  lock->stat.total_time);
		deleted_wake_locks.stat.prevent_suspend_time =
			ktime_add(deleted_wake_locks.stat.prevent_suspend_time,
				  lock->stat.prevent_suspend_time);
		deleted_wake_locks.stat.max_time =
			ktime_add(deleted_wake_locks.stat.max_time,
				  lock->stat.max_time);
	}
#endif
	list_del(&lock->link);
	spin_unlock_irqrestore(&list_lock, irqflags);
}
EXPORT_SYMBOL(wake_lock_destroy);

static void wake_lock_internal(
	struct wake_lock *lock, long timeout, int has_timeout)
{
	int type;
	unsigned long irqflags;
	long expire_in;

	　//上锁前，　检查锁类型和有效性  // 获取锁的类型   
	spin_lock_irqsave(&list_lock, irqflags);
	type = lock->flags & WAKE_LOCK_TYPE_MASK;
	BUG_ON(type >= WAKE_LOCK_TYPE_COUNT);
	BUG_ON(!(lock->flags & WAKE_LOCK_INITIALIZED));
#ifdef CONFIG_WAKELOCK_STAT
	　//检查　ｗａｉｔ_ｆｏｒ_ｗａｋｅｕｐ　标志  
	if (type == WAKE_LOCK_SUSPEND && wait_for_wakeup) {
		if (debug_mask & DEBUG_WAKEUP)
			pr_info("wakeup wake lock: %s\n", lock->name);
		wait_for_wakeup = 0;
		lock->stat.wakeup_count++;
	}
	　//检查超时锁 
	if ((lock->flags & WAKE_LOCK_AUTO_EXPIRE) &&
	    (long)(lock->expires - jiffies) <= 0) {
		wake_unlock_stat_locked(lock, 0);
		lock->stat.last_time = ktime_get();
	}
#endif
	　//上锁 // 设置锁有效的标志位 
	if (!(lock->flags & WAKE_LOCK_ACTIVE)) {
		lock->flags |= WAKE_LOCK_ACTIVE;
#ifdef CONFIG_WAKELOCK_STAT
		lock->stat.last_time = ktime_get();
#endif
	}

	//先从链表中摘除该锁 
	list_del(&lock->link);
	if (has_timeout) {
		if (debug_mask & DEBUG_WAKE_LOCK)
			pr_info("wake_lock: %s, type %d, timeout %ld.%03lu\n",
				lock->name, type, timeout / HZ,
				(timeout % HZ) * MSEC_PER_SEC / HZ);
		lock->expires = jiffies + timeout;
		lock->flags |= WAKE_LOCK_AUTO_EXPIRE;
		//超时锁重新加入链表队尾
		list_add_tail(&lock->link, &active_wake_locks[type]);
	} else {
		if (debug_mask & DEBUG_WAKE_LOCK)
			pr_info("wake_lock: %s, type %d\n", lock->name, type);
		lock->expires = LONG_MAX;  // 设置超时时间为极限  
		lock->flags &= ~WAKE_LOCK_AUTO_EXPIRE;
		　//非超时锁直接加入队列
		list_add(&lock->link, &active_wake_locks[type]);
	}
	if (type == WAKE_LOCK_SUSPEND) {
		current_event_num++;  // 休眠锁使用计数器加1  
#ifdef CONFIG_WAKELOCK_STAT
		//针对主锁的检查  
		if (lock == &main_wake_lock)
			update_sleep_wait_stats_locked(1);
		else if (!wake_lock_active(&main_wake_lock))
			update_sleep_wait_stats_locked(0);
#endif
		//检查并遍历更新队列中所有超时锁。
		if (has_timeout)
			expire_in = has_wake_lock_locked(type);
		else
			expire_in = -1; 　//非超时锁
		if (expire_in > 0) {
			if (debug_mask & DEBUG_EXPIRE)
				pr_info("wake_lock: %s, start expire timer, "
					"%ld\n", lock->name, expire_in);
			mod_timer(&expire_timer, jiffies + expire_in);   // 重新修改超时时间为 最长的那个锁的时间
		} else { //无任何超时锁
			if (del_timer(&expire_timer))
				if (debug_mask & DEBUG_EXPIRE)
					pr_info("wake_lock: %s, stop expire timer\n",
						lock->name);
			if (expire_in == 0)
				//如果超时锁为０，　且无非超时锁时，　ｅｘｐｉｒｅ_ｉｎ为０,　启动ｓｕｓｐｅｎｄ流程  
				queue_work(suspend_work_queue, &suspend_work);
		}
	}
	spin_unlock_irqrestore(&list_lock, irqflags);
}

void wake_lock(struct wake_lock *lock)
{
	wake_lock_internal(lock, 0, 0);
}
EXPORT_SYMBOL(wake_lock);

void wake_lock_timeout(struct wake_lock *lock, long timeout)
{
	wake_lock_internal(lock, timeout, 1);
}
EXPORT_SYMBOL(wake_lock_timeout);


//该函数用于释放一个锁，首先将锁从有效锁链表中移除并加入无效锁链表，并判断系统是否还持有有效锁，
//如果没有则进入深度休眠流程。
void wake_unlock(struct wake_lock *lock)
{
	int type;
	unsigned long irqflags;
	spin_lock_irqsave(&list_lock, irqflags);
	type = lock->flags & WAKE_LOCK_TYPE_MASK;
#ifdef CONFIG_WAKELOCK_STAT
	wake_unlock_stat_locked(lock, 0);  // 更新锁的状态 
#endif
	if (debug_mask & DEBUG_WAKE_LOCK)
		pr_info("wake_unlock: %s\n", lock->name);
	lock->flags &= ~(WAKE_LOCK_ACTIVE | WAKE_LOCK_AUTO_EXPIRE);  // 清楚有效锁和超时锁标志 
	list_del(&lock->link);
	list_add(&lock->link, &inactive_locks);   // 将锁从有效锁链表中移除加入无效锁链表

	 // 如果是休眠锁
	if (type == WAKE_LOCK_SUSPEND) {
		 // 判断系统当前是否还持有锁
		long has_lock = has_wake_lock_locked(type);
		// 如果还持有锁,设置timer到超时时间点触发 
		if (has_lock > 0) {
			if (debug_mask & DEBUG_EXPIRE)
				pr_info("wake_unlock: %s, start expire timer, "
					"%ld\n", lock->name, has_lock);
			mod_timer(&expire_timer, jiffies + has_lock);
		} else {
			if (del_timer(&expire_timer))
				if (debug_mask & DEBUG_EXPIRE)
					pr_info("wake_unlock: %s, stop expire "
						"timer\n", lock->name);
			if (has_lock == 0)  // 启动深度休眠工作队列  
				queue_work(suspend_work_queue, &suspend_work);
		}
		 // 如果是内核锁  
		if (lock == &main_wake_lock) {
			if (debug_mask & DEBUG_SUSPEND)
				print_active_locks(WAKE_LOCK_SUSPEND);  // 打印当前有效锁信息
#ifdef CONFIG_WAKELOCK_STAT
			update_sleep_wait_stats_locked(0);
#endif
		}
	}
	spin_unlock_irqrestore(&list_lock, irqflags);
}
EXPORT_SYMBOL(wake_unlock);


// 判断锁是否有效  
int wake_lock_active(struct wake_lock *lock)
{
	return !!(lock->flags & WAKE_LOCK_ACTIVE);
}
EXPORT_SYMBOL(wake_lock_active);

static int wakelock_stats_open(struct inode *inode, struct file *file)
{
	return single_open(file, wakelock_stats_show, NULL);
}

static const struct file_operations wakelock_stats_fops = {
	.owner = THIS_MODULE,
	.open = wakelock_stats_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int __init wakelocks_init(void)
{
	int ret;
	int i;

	// 初始化有效锁链表,内核维护了2个有效锁链表  
    // WAKE_LOCK_SUSPEND 用于阻止进入深度休眠模式  
    // WAKE_LOCK_IDLE    用于阻止进入空闲模式 

	for (i = 0; i < ARRAY_SIZE(active_wake_locks); i++)
		INIT_LIST_HEAD(&active_wake_locks[i]);

#ifdef CONFIG_WAKELOCK_STAT
	wake_lock_init(&deleted_wake_locks, WAKE_LOCK_SUSPEND,
			"deleted_wake_locks");
#endif
	wake_lock_init(&main_wake_lock, WAKE_LOCK_SUSPEND, "main");
	wake_lock(&main_wake_lock);

	//unknown_wakeup用于唤醒时延迟0.5s进入下一次可能的深度休眠；
	wake_lock_init(&unknown_wakeup, WAKE_LOCK_SUSPEND, "unknown_wakeups");
	wake_lock_init(&suspend_backoff_lock, WAKE_LOCK_SUSPEND,
		       "suspend_backoff");

	//还注册了一个platform_device用于深度休眠阶段检测是否存在有效锁；
	ret = platform_device_register(&power_device);
	if (ret) {
		pr_err("wakelocks_init: platform_device_register failed\n");
		goto err_platform_device_register;
	}
	ret = platform_driver_register(&power_driver);
	if (ret) {
		pr_err("wakelocks_init: platform_driver_register failed\n");
		goto err_platform_driver_register;
	}

	suspend_work_queue = create_singlethread_workqueue("suspend");
	if (suspend_work_queue == NULL) {
		ret = -ENOMEM;
		goto err_suspend_work_queue;
	}

#ifdef CONFIG_WAKELOCK_STAT
	proc_create("wakelocks", S_IRUGO, NULL, &wakelock_stats_fops);
#endif

	return 0;

err_suspend_work_queue:
	platform_driver_unregister(&power_driver);
err_platform_driver_register:
	platform_device_unregister(&power_device);
err_platform_device_register:
	wake_lock_destroy(&suspend_backoff_lock);
	wake_lock_destroy(&unknown_wakeup);
	wake_lock_destroy(&main_wake_lock);
#ifdef CONFIG_WAKELOCK_STAT
	wake_lock_destroy(&deleted_wake_locks);
#endif
	return ret;
}

static void  __exit wakelocks_exit(void)
{
#ifdef CONFIG_WAKELOCK_STAT
	remove_proc_entry("wakelocks", NULL);
#endif
	destroy_workqueue(suspend_work_queue);
	platform_driver_unregister(&power_driver);
	platform_device_unregister(&power_device);
	wake_lock_destroy(&suspend_backoff_lock);
	wake_lock_destroy(&unknown_wakeup);
	wake_lock_destroy(&main_wake_lock);
#ifdef CONFIG_WAKELOCK_STAT
	wake_lock_destroy(&deleted_wake_locks);
#endif
}

core_initcall(wakelocks_init);
module_exit(wakelocks_exit);
