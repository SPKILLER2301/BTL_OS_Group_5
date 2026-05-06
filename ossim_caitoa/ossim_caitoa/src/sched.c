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
                if (!empty(&mlq_ready_queue[prio]))
                        return -1;
#endif
        return (empty(&ready_queue) && empty(&run_queue));
}

void init_scheduler(void) {
#ifdef MLQ_SCHED
        int i;
        for (i = 0; i < MAX_PRIO; i++) {
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
 * Stateful design for routine calling
 * based on the priority and our MLQ policy.
 *
 * State: cur_prio tracks which priority queue we are currently serving.
 *        cur_slot  tracks how many more slots the current queue can use.
 *
 * Policy:
 *   slot[prio] = MAX_PRIO - prio
 *   => priority 0 gets 140 slots (most CPU time)
 *   => priority 139 gets 1 slot  (least CPU time)
 *
 *   We dequeue from cur_prio while cur_slot > 0.
 *   When cur_slot hits 0 (or the queue is empty), advance to the next
 *   non-empty queue and reset its slot counter.
 *   After a full round (all queues exhausted), reset slot[] for every queue.
 */
struct pcb_t *get_mlq_proc(void)
{
        struct pcb_t *proc = NULL;

        /*
         * Static state persists across calls — this is the "transition
         * technique" mentioned in the spec comment.
         */
        static int cur_prio = 0;
        static int cur_slot = MAX_PRIO; /* slot[0] = MAX_PRIO - 0 = 140 */

        pthread_mutex_lock(&queue_lock);

        /*
         * Try up to MAX_PRIO queues before giving up (one full round).
         * We stop as soon as we successfully dequeue a process.
         */
        int checked = 0;
        while (checked < MAX_PRIO) {
                /* Current queue has processes AND still has slots remaining */
                if (!empty(&mlq_ready_queue[cur_prio]) && cur_slot > 0) {
                        proc = dequeue(&mlq_ready_queue[cur_prio]);
                        cur_slot--;
                        break;
                }

                /*
                 * Either the queue is empty or slots are used up.
                 * Advance to the next priority level and reset its slot budget.
                 */
                cur_prio = (cur_prio + 1) % MAX_PRIO;
                cur_slot = slot[cur_prio]; /* = MAX_PRIO - cur_prio */
                checked++;
        }

        if (proc != NULL)
                enqueue(&running_list, proc);

        pthread_mutex_unlock(&queue_lock);
        return proc;
}

void put_mlq_proc(struct pcb_t *proc) {
        proc->krnl->ready_queue     = &ready_queue;
        proc->krnl->mlq_ready_queue = mlq_ready_queue;
        proc->krnl->running_list    = &running_list;

        /*
         * Remove proc from running_list, then re-enqueue it into
         * its priority queue so it can be scheduled again.
         */
        pthread_mutex_lock(&queue_lock);
        purgequeue(&running_list, proc);
        enqueue(&mlq_ready_queue[proc->prio], proc);
        pthread_mutex_unlock(&queue_lock);
}

void add_mlq_proc(struct pcb_t *proc) {
        proc->krnl->ready_queue     = &ready_queue;
        proc->krnl->mlq_ready_queue = mlq_ready_queue;
        proc->krnl->running_list    = &running_list;

        /*
         * New process arriving for the first time — just enqueue it
         * into the appropriate priority-level ready queue.
         */
        pthread_mutex_lock(&queue_lock);
        enqueue(&mlq_ready_queue[proc->prio], proc);
        pthread_mutex_unlock(&queue_lock);
}

struct pcb_t *get_proc(void)  { return get_mlq_proc(); }
void          put_proc(struct pcb_t *proc) { put_mlq_proc(proc); }
void          add_proc(struct pcb_t *proc) { add_mlq_proc(proc); }

#else  /* !MLQ_SCHED — simple single ready-queue, FCFS/priority */

struct pcb_t *get_proc(void)
{
        struct pcb_t *proc = NULL;

        pthread_mutex_lock(&queue_lock);
        proc = dequeue(&ready_queue);
        if (proc != NULL)
                enqueue(&running_list, proc);
        pthread_mutex_unlock(&queue_lock);

        return proc;
}

void put_proc(struct pcb_t *proc) {
        proc->krnl->ready_queue  = &ready_queue;
        proc->krnl->running_list = &running_list;

        pthread_mutex_lock(&queue_lock);
        purgequeue(&running_list, proc);
        enqueue(&run_queue, proc);
        pthread_mutex_unlock(&queue_lock);
}

void add_proc(struct pcb_t *proc) {
        proc->krnl->ready_queue  = &ready_queue;
        proc->krnl->running_list = &running_list;

        pthread_mutex_lock(&queue_lock);
        enqueue(&ready_queue, proc);
        pthread_mutex_unlock(&queue_lock);
}
#endif /* MLQ_SCHED */