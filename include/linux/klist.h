/*
 *	klist.h - Some generic list helpers, extending struct list_head a bit.
 *
 *	Implementations are found in lib/klist.c
 *
 *
 *	Copyright (C) 2005 Patrick Mochel
 *
 *	This file is rleased under the GPL v2.
 */

#ifndef _LINUX_KLIST_H
#define _LINUX_KLIST_H

#include <linux/spinlock.h>
#include <linux/kref.h>
#include <linux/list.h>

struct klist_node;
struct klist {
	spinlock_t		k_lock;
	struct list_head	k_list;
	void			(*get)(struct klist_node *);
	void			(*put)(struct klist_node *);
} __attribute__ ((aligned (sizeof(void *))));

#define KLIST_INIT(_name, _get, _put)					\
	{ .k_lock	= __SPIN_LOCK_UNLOCKED(_name.k_lock),		\
	  .k_list	= LIST_HEAD_INIT(_name.k_list),			\
	  .get		= _get,						\
	  .put		= _put, }

#define DEFINE_KLIST(_name, _get, _put)					\
	struct klist _name = KLIST_INIT(_name, _get, _put)

extern void klist_init(struct klist *k, void (*get)(struct klist_node *),
		       void (*put)(struct klist_node *));

//比较特殊的指针n_klist，n_klist是指向链表头struct klist的，但它的第0位用来表示
//是否该节点已被请求删除，如果已被请求删除则在链表循环时是看不到这一节点的，循环
//函数将其略过
struct klist_node {
	void			*n_klist;	/* never access directly */
	struct list_head	n_node;
	struct kref		n_ref;
};


//把klist_node  这个节点加入 k链表
extern void klist_add_tail(struct klist_node *n, struct klist *k);
extern void klist_add_head(struct klist_node *n, struct klist *k);
extern void klist_add_after(struct klist_node *n, struct klist_node *pos);
extern void klist_add_before(struct klist_node *n, struct klist_node *pos);

extern void klist_del(struct klist_node *n);
extern void klist_remove(struct klist_node *n);

extern int klist_node_attached(struct klist_node *n);


struct klist_iter {
	struct klist		*i_klist;
	struct klist_node	*i_cur;
};


/*
klist_iter_init_node()是从klist中的某个节点开始遍历，而klist_iter_init()是从链表头开始遍历的。

但你又要注意，klist_iter_init()和klist_iter_init_node()的用法又不同。klist_iter_init_node()
可以在其后直接对当前节点进行访问，也可以调用klist_next()访问下一节点。而klist_iter_init()只能
调用klist_next()访问下一节点。或许klist_iter_init_node()的本意不是从当前节点开始，而是从当前
节点的下一节点开始。


*/

extern void klist_iter_init(struct klist *k, struct klist_iter *i);
extern void klist_iter_init_node(struct klist *k, struct klist_iter *i,
				 struct klist_node *n);
extern void klist_iter_exit(struct klist_iter *i);
extern struct klist_node *klist_next(struct klist_iter *i);

/*
klist_next()是将循环进行到下一节点。实现中需要注意两点问题：1、加锁，根据经验，单纯对某
个节点操作不需要加锁，但对影响整个链表的操作需要加自旋锁。比如之前klist_iter_init_node()中对
节点增加引用计数，就不需要加锁，因为只有已经拥有节点引用计数的线程才会特别地从那个节点开始。而
之后klist_next()中则需要加锁，因为当前线程很可能没有引用计数，所以需要加锁，让情况固定下来。这
既是保护链表，也是保护节点有效。符合kref引用计数的使用原则。2、要注意，虽然在节点切换的过程中
是加锁的，但切换完访问当前节点时是解锁的，中间可能有节点被删除（这个通过spin_lock就可以搞定），
也可能有节点被请求删除，这就需要注意。首先要忽略链表中已被请求删除的节点，然后在减少前一个节点引
用计数时，可能就把前一个节点删除了。这里之所以不调用klist_put()，是因为本身已处于加锁状态，但仍
要有它的实现。这里的实现和klist_put()中类似，代码不介意在加锁状态下唤醒另一个线程，但却不希望在
加锁状态下调用put()函数，那可能会涉及释放另一个更大的结构。




*/

#endif
