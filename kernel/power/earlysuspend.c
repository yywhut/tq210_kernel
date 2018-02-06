/* kernel/power/earlysuspend.c
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

#include <linux/earlysuspend.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/rtc.h>
#include <linux/syscalls.h> /* sys_sync */
#include <linux/wakelock.h>
#include <linux/workqueue.h>

#include "power.h"

enum {
	DEBUG_USER_STATE = 1U << 0,
	DEBUG_SUSPEND = 1U << 2,
	DEBUG_VERBOSE = 1U << 3,
};
static int debug_mask = DEBUG_USER_STATE;
module_param_named(debug_mask, debug_mask, int, S_IRUGO | S_IWUSR | S_IWGRP);

static DEFINE_MUTEX(early_suspend_lock);
static LIST_HEAD(early_suspend_handlers);  // ³õÊ¼»¯Ç³¶ÈÐÝÃßÁ´±í  
static void early_suspend(struct work_struct *work);
static void late_resume(struct work_struct *work);
static DECLARE_WORK(early_suspend_work, early_suspend);
static DECLARE_WORK(late_resume_work, late_resume);
static DEFINE_SPINLOCK(state_lock);
enum {
	SUSPEND_REQUESTED = 0x1,  // µ±Ç°ÕýÔÚÇëÇóÇ³¶ÈÐÝÃß  
	SUSPENDED = 0x2,     // Ç³¶ÈÐÝÃßÍê³É  
	SUSPEND_REQUESTED_AND_SUSPENDED = SUSPEND_REQUESTED | SUSPENDED,
};
static int state;

/*
¿ÉÒÔ¿´µ½early_suspendÓÉÁ½¸öº¯ÊýÖ¸Õë¡¢Á´±í½Úµã¡¢ÓÅÏÈµÈ¼¶×é³É£»ÄÚºËÄ¬ÈÏ¶¨ÒåÁË3¸öÓÅÏÈµÈ¼¶£¬
ÔÚsuspendµÄÊ±ºòÏÈÖ´ÐÐÓÅÏÈµÈ¼¶µÍµÄhandler£¬ÔÚresumeµÄÊ±ºòÔòÏÈÖ´ÐÐµÈ¼¶¸ßµÄhandler£¬
ÓÃ»§¿ÉÒÔ¶¨Òå×Ô¼ºµÄÓÅÏÈµÈ¼¶£»
*/



/*
×¢²áµÄÁ÷³Ì±È½Ï¼òµ¥£¬Ê×ÏÈ±éÀúÁ´±í£¬ÒÀ´Î±È½ÏÃ¿¸ö½ÚµãµÄÓÅÏÈµÈ¼¶£¬Èç¹ûÓöµ½ÓÅÏÈµÈ¼¶±ÈÐÂ½ÚµãÓÅÏÈµÈ¼¶¸ßÔòÌø³ö£¬
È»ºó½«ÐÂ½Úµã¼ÓÈëÓÅÏÈµÈ¼¶½Ï¸ßµÄ½ÚµãÇ°Ãæ£¬ÕâÑù¾ÍÈ·±£ÁËÁ´±íÊÇÓÅÏÈµÈ¼¶µÍÔÚÇ°¸ßÔÚºóµÄË³Ðò£»
ÔÚ½«½Úµã¼ÓÈëÁ´±íºó²é¿´µ±Ç°×´Ì¬ÊÇ·ñÎªÇ³¶ÈÐÝÃßÍê³É×´Ì¬£¬Èç¹ûÊÇÔòÖ´ÐÐhandlerµÄsuspendº¯Êý¡£

*/
void register_early_suspend(struct early_suspend *handler)
{
	struct list_head *pos;

	mutex_lock(&early_suspend_lock);
	list_for_each(pos, &early_suspend_handlers) {
		struct early_suspend *e;

		// ÅÐ¶Ïµ±Ç°½ÚµãµÄÓÅÏÈµÈ¼¶ÊÇ·ñ´óÓÚhandlerµÄÓÅÏÈµÈ¼¶  
        // ÒÔ´Ë¾ö¶¨handlerÔÚÁ´±íÖÐµÄË³Ðò  
		e = list_entry(pos, struct early_suspend, link);
		if (e->level > handler->level)
			break;
	}

	// ½«handler¼ÓÈëµ±Ç°½ÚµãÖ®Ç°,ÓÅÏÈµÈ¼¶Ô½µÍÔ½¿¿Ç°  
	list_add_tail(&handler->link, pos);
	if ((state & SUSPENDED) && handler->suspend)
		handler->suspend(handler);
	mutex_unlock(&early_suspend_lock);
}
EXPORT_SYMBOL(register_early_suspend);

void unregister_early_suspend(struct early_suspend *handler)
{
	mutex_lock(&early_suspend_lock);
	list_del(&handler->link);
	mutex_unlock(&early_suspend_lock);
}
EXPORT_SYMBOL(unregister_early_suspend);


/*
ÔÚsuspendÁ÷³ÌÖÐÊ×ÏÈÅÐ¶Ïµ±Ç°×´Ì¬ÊÇ·ñÎªSUSPEND_REQUESTED£¬Èç¹ûÊÇÔòÖÃÎ»SUSPENDED±êÖ¾£¬Èç¹û²»ÊÇÔòÈ¡ÏûsuspendÁ÷³Ì£
»È»ºó±éÀúÇ³¶ÈÐÝÃßÁ´±í£¬´ÓÁ´±íÍ·²¿µ½Î²²¿ÒÀ´Îµ÷ÓÃ¸÷½ÚµãµÄsuspend()º¯Êý£¬Ö´ÐÐÍêºóÅÐ¶Ïµ±Ç°×´Ì¬ÊÇ·ñÎª
SUSPEND_REQUESTED_AND_SUSPENDED£¬Èç¹ûÊÇÔòÊÍ·Åmain_wake_lock£¬µ±Ç°ÏµÍ³ÖÐÈç¹ûÖ»´æÔÚmain_wake_lockÕâ¸öÓÐÐ§Ëø£¬
Ôò»áÔÚwake_unlock()ÀïÃæÆô¶¯Éî¶ÈÐÝÃßÏß³Ì£¬Èç¹û»¹ÓÐÆäËûÆäËûwake_lockÔò±£³Öµ±Ç°×´Ì¬¡£

*/


static void early_suspend(struct work_struct *work)
{
	struct early_suspend *pos;
	unsigned long irqflags;
	int abort = 0;

	mutex_lock(&early_suspend_lock);
	spin_lock_irqsave(&state_lock, irqflags);
	if (state == SUSPEND_REQUESTED)   // ÅÐ¶Ïµ±Ç°×´Ì¬ÊÇ·ñÔÚÇëÇóÇ³¶ÈÐÝÃß  
		state |= SUSPENDED;  // Èç¹ûÊÇÔòÖÃÎ»SUSPENDED  
	else 
		abort = 1;
	spin_unlock_irqrestore(&state_lock, irqflags);

	if (abort) {  // È¡Ïûearly_suspend	
		if (debug_mask & DEBUG_SUSPEND)
			pr_info("early_suspend: abort, state %d\n", state);
		mutex_unlock(&early_suspend_lock);
		goto abort;
	}

	if (debug_mask & DEBUG_SUSPEND)
		pr_info("early_suspend: call handlers\n");

	// ±éÀúÇ³¶ÈÐÝÃßÁ´±í²¢Ö´ÐÐÆäÖÐËùÓÐsuspendº¯Êý  
    // Ö´ÐÐË³Ðò¸ù¾ÝÓÅÏÈµÈ¼¶¶ø¶¨,µÈ¼¶Ô½µÍÔ½ÏÈÖ´ÐÐ  
	list_for_each_entry(pos, &early_suspend_handlers, link) {
		if (pos->suspend != NULL) {
			if (debug_mask & DEBUG_VERBOSE)
				pr_info("early_suspend: calling %pf\n", pos->suspend);
			pos->suspend(pos);
		}
	}
	mutex_unlock(&early_suspend_lock);

	if (debug_mask & DEBUG_SUSPEND)
		pr_info("early_suspend: sync\n");

	sys_sync();
abort:
	spin_lock_irqsave(&state_lock, irqflags);
	if (state == SUSPEND_REQUESTED_AND_SUSPENDED)
		wake_unlock(&main_wake_lock);
	spin_unlock_irqrestore(&state_lock, irqflags);
}

static void late_resume(struct work_struct *work)
{
	struct early_suspend *pos;
	unsigned long irqflags;
	int abort = 0;

	mutex_lock(&early_suspend_lock);
	spin_lock_irqsave(&state_lock, irqflags);
	if (state == SUSPENDED)  // Çå³ýÇ³¶ÈÐÝÃßÍê³É±êÖ¾ 
		state &= ~SUSPENDED;
	else
		abort = 1;
	spin_unlock_irqrestore(&state_lock, irqflags);

	if (abort) {
		if (debug_mask & DEBUG_SUSPEND)
			pr_info("late_resume: abort, state %d\n", state);
		goto abort;
	}
	if (debug_mask & DEBUG_SUSPEND)
		pr_info("late_resume: call handlers\n");

	// ·´Ïò±éÀúÇ³¶ÈÐÝÃßÁ´±í²¢Ö´ÐÐÆäÖÐËùÓÐresumeº¯Êý  
    // Ö´ÐÐË³Ðò¸ù¾ÝÓÅÏÈµÈ¼¶¶ø¶¨,µÈ¼¶Ô½¸ßÔ½ÏÈÖ´ÐÐ  
	list_for_each_entry_reverse(pos, &early_suspend_handlers, link) {
		if (pos->resume != NULL) {
			if (debug_mask & DEBUG_VERBOSE)
				pr_info("late_resume: calling %pf\n", pos->resume);

			pos->resume(pos);
		}
	}
	if (debug_mask & DEBUG_SUSPEND)
		pr_info("late_resume: done\n");
abort:
	mutex_unlock(&early_suspend_lock);
}

void request_suspend_state(suspend_state_t new_state)
{
	unsigned long irqflags;
	int old_sleep;

	spin_lock_irqsave(&state_lock, irqflags);
	old_sleep = state & SUSPEND_REQUESTED;
	if (debug_mask & DEBUG_USER_STATE) {
		struct timespec ts;
		struct rtc_time tm;
		getnstimeofday(&ts);
		rtc_time_to_tm(ts.tv_sec, &tm);
		pr_info("request_suspend_state: %s (%d->%d) at %lld "
			"(%d-%02d-%02d %02d:%02d:%02d.%09lu UTC)\n",
			new_state != PM_SUSPEND_ON ? "sleep" : "wakeup",
			requested_suspend_state, new_state,
			ktime_to_ns(ktime_get()),          //»ñÈ¡Æô¶¯ÒÔÀ´¾­¹ýµÄcÊ±¼ä£¬²»°üÀ¨ÐÝÃßÊ±¼ä
			tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
			tm.tm_hour, tm.tm_min, tm.tm_sec, ts.tv_nsec);
	}

	 // Èç¹ûÐÂ×´Ì¬ÊÇÐÝÃß×´Ì¬  
	if (!old_sleep && new_state != PM_SUSPEND_ON) {
		state |= SUSPEND_REQUESTED;
		queue_work(suspend_work_queue, &early_suspend_work);
	} else if (old_sleep && new_state == PM_SUSPEND_ON) {  // Èç¹ûÐÂ×´Ì¬ÊÇ»½ÐÑ×´Ì¬ 
		state &= ~SUSPEND_REQUESTED;
		wake_lock(&main_wake_lock);  // ¼¤»îÄÚºËËø 
		queue_work(suspend_work_queue, &late_resume_work);  // Ö´ÐÐÇ³¶È»½ÐÑµÄ¹¤×÷¶ÓÁÐ 
	}

	// ¸üÐÂÈ«¾Ö×´Ì¬  
	requested_suspend_state = new_state;
	spin_unlock_irqrestore(&state_lock, irqflags);
}

suspend_state_t get_suspend_state(void)
{
	return requested_suspend_state;
}

schedule_work