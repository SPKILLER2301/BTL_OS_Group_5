/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* LamiaAtrium release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

#include "queue.h"
#include "sched.h"
#include <pthread.h>

#include <stdlib.h>
#include <stdio.h>
static struct queue_t ready_queue;
static struct queue_t run_queue;
static pthread_mutex_t queue_lock;

static struct queue_t running_list;
#ifdef MLQ_SCHED
static struct queue_t mlq_ready_queue[MAX_PRIO];
static int slot[MAX_PRIO];
#endif

int queue_empty(void) {
#ifdef MLQ_SCHED
	unsigned long prio;
	for (prio = 0; prio < MAX_PRIO; prio++)
		if(!empty(&mlq_ready_queue[prio])) 
			return -1;
#endif
	return (empty(&ready_queue) && empty(&run_queue));
}

void init_scheduler(void) {
#ifdef MLQ_SCHED
    int i ;

	for (i = 0; i < MAX_PRIO; i ++) {
		mlq_ready_queue[i].size = 0;
		slot[i] = MAX_PRIO - i; 
	}
#endif
	ready_queue.size = 0;
	run_queue.size = 0;
	running_list.size = 0;
	pthread_mutex_init(&queue_lock, NULL);
}

#ifdef MLQ_SCHED
/* 
 *  Stateful design for routine calling
 *  based on the priority and our MLQ policy
 *  We implement stateful here using transition technique
 *  State representation   prio = 0 .. MAX_PRIO, curr_slot = 0..(MAX_PRIO - prio)
 */
struct pcb_t * get_mlq_proc(void) {
	struct pcb_t * proc = NULL;
	static int curr_prio = 0;
	static int curr_slot = MAX_PRIO; /* slot[0] = MAX_PRIO */

	pthread_mutex_lock(&queue_lock);

	int i;
	for (i = 0; i < MAX_PRIO; i++) {
		/*
		 * FIX: Separated slot exhaustion check from queue-empty check.
		 * Old code mixed both in one nested if-else, causing curr_slot
		 * to potentially become 0 (when curr_prio = MAX_PRIO-1 wraps to 0)
		 * and then skipping valid queues or dequeuing with slot=0.
		 */

		/* If current priority's slot is exhausted, advance to next */
		if (curr_slot <= 0) {
			curr_prio = (curr_prio + 1) % MAX_PRIO;
			curr_slot = MAX_PRIO - curr_prio;
			if (curr_slot <= 0)
				curr_slot = 1; /* guard: minimum 1 slot per priority */
		}

		if (!empty(&mlq_ready_queue[curr_prio])) {
			proc = dequeue(&mlq_ready_queue[curr_prio]);
			if (proc != NULL) {
				curr_slot--;
				/* Pre-advance priority if this priority is now exhausted */
				if (curr_slot <= 0) {
					curr_prio = (curr_prio + 1) % MAX_PRIO;
					curr_slot = MAX_PRIO - curr_prio;
					if (curr_slot <= 0)
						curr_slot = 1;
				}
				enqueue(&running_list, proc);
			}
			break;
		} else {
			/* This priority queue is empty, move to next */
			curr_prio = (curr_prio + 1) % MAX_PRIO;
			curr_slot = MAX_PRIO - curr_prio;
			if (curr_slot <= 0)
				curr_slot = 1;
		}
	}

	pthread_mutex_unlock(&queue_lock);
	return proc;
}

void put_mlq_proc(struct pcb_t * proc) {
	/*
	 * FIX: Only update the scheduler queue pointers on krnl that are
	 * safe to write. Do NOT assign mlq_ready_queue (an array) directly
	 * to krnl->mlq_ready_queue (a pointer) — they are compatible here
	 * since mlq_ready_queue decays to a pointer to its first element,
	 * but we keep it explicit and correct.
	 *
	 * Also remove proc from running_list before re-queuing.
	 */
	pthread_mutex_lock(&queue_lock);
	purgequeue(&running_list, proc);
	enqueue(&mlq_ready_queue[proc->prio], proc);
	pthread_mutex_unlock(&queue_lock);
}

void add_mlq_proc(struct pcb_t * proc) {
	/*
	 * FIX: Update krnl scheduler pointers so sys_mem can find this
	 * process via running_list. The mlq_ready_queue pointer assignment
	 * is intentional: krnl->mlq_ready_queue points to the base of the
	 * static mlq_ready_queue array defined in this file.
	 */
	proc->krnl->ready_queue     = &ready_queue;
	proc->krnl->mlq_ready_queue = mlq_ready_queue;
	proc->krnl->running_list    = &running_list;

	pthread_mutex_lock(&queue_lock);
	enqueue(&mlq_ready_queue[proc->prio], proc);
	pthread_mutex_unlock(&queue_lock);
}

struct pcb_t * get_proc(void) {
	return get_mlq_proc();
}

void put_proc(struct pcb_t * proc) {
	return put_mlq_proc(proc);
}

void add_proc(struct pcb_t * proc) {
	return add_mlq_proc(proc);
}
#else
struct pcb_t * get_proc(void) {
	struct pcb_t * proc = NULL;

	pthread_mutex_lock(&queue_lock);
	/*TODO: get a process from [ready_queue].
	 *       It worth to protect by a mechanism.
	 *
	 */

	pthread_mutex_unlock(&queue_lock);

	return proc;
}

void put_proc(struct pcb_t * proc) {
	proc->krnl->ready_queue = &ready_queue;
	proc->krnl->running_list = &running_list;

	/* TODO: put running proc to running_list
	 *       It worth to protect by a mechanism.
	 *
	 */

	pthread_mutex_lock(&queue_lock);
	enqueue(&run_queue, proc);
	pthread_mutex_unlock(&queue_lock);
}

void add_proc(struct pcb_t * proc) {
	proc->krnl->ready_queue = &ready_queue;
	proc->krnl->running_list = &running_list;

	/* TODO: put running proc to running_list
	 *       It worth to protect by a mechanism.
	 *
	 */

	pthread_mutex_lock(&queue_lock);
	enqueue(&ready_queue, proc);
	pthread_mutex_unlock(&queue_lock);
}
#endif
void finish_proc(struct pcb_t *proc)
{
	if (proc == NULL)
		return;

	pthread_mutex_lock(&queue_lock);
	purgequeue(&running_list, proc);
	pthread_mutex_unlock(&queue_lock);
}

void finish_scheduler(void)
{
	pthread_mutex_destroy(&queue_lock);
}