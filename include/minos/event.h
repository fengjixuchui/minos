#ifndef __MINOS_EVENT_H__
#define __MINOS_EVENT_H__

#include <minos/preempt.h>
#include <minos/task_def.h>

#define OS_EVENT_TYPE_UNUSED	0
#define OS_EVENT_TYPE_MBOX	1
#define OS_EVENT_TYPE_Q		2
#define OS_EVENT_TYPE_SEM	3
#define OS_EVENT_TYPE_MUTEX	4
#define OS_EVENT_TYPE_FLAG	5

#define OS_PEND_OPT_NONE        0
#define OS_PEND_OPT_BROADCAST   1

#define OS_POST_OPT_NONE        0x00
#define OS_POST_OPT_BROADCAST   0x01
#define OS_POST_OPT_FRONT       0x02
#define OS_POST_OPT_NO_SCHED    0x04

struct event {
	uint16_t type;				/* event type */
	uint16_t owner;				/* event owner the pid */
	uint32_t cnt;				/* event cnt */
	void *data;				/* event pdata for transfer */
	spinlock_t lock;			/* the lock of the event for smp */
	uint8_t wait_grp;			/* realtime task waiting on this event */
	uint8_t wait_tbl[OS_RDY_TBL_SIZE];	/* wait bitmap */
	struct list_head wait_list;		/* non realtime task waitting list */
	struct list_head list;			/* list to the task's owner list */
};

#define to_event(e)	(struct event *)e

int event_task_remove(struct task *task, struct event *ev);
struct task *event_get_waiter(struct event *ev);

struct task *event_highest_task_ready(struct event *ev, void *msg,
		uint32_t msk, int pend_stat);

void event_init(struct event *event, int type, void *pdata);
void event_task_wait(struct task *task, void *ev, int stat, uint32_t to);

static inline int event_has_waiter(struct event *ev)
{
	return ((ev->wait_grp) || (!is_list_empty(&ev->wait_list)));
}

#endif
