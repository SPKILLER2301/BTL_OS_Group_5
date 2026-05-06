#include <stdio.h>
#include <stdlib.h>
#include "queue.h"

int empty(struct queue_t *q)
{
        if (q == NULL)
                return 1;
        return (q->size == 0);
}

void enqueue(struct queue_t *q, struct pcb_t *proc)
{
    if (q == NULL || proc == NULL) return;
    if (q->size == MAX_QUEUE_SIZE) return;

    /* Insert in sorted order by prio (ascending = higher priority first) */
    int i = q->size - 1;

#ifdef MLQ_SCHED
    while (i >= 0 && q->proc[i]->prio > proc->prio) {
#else
    while (i >= 0 && q->proc[i]->priority > proc->priority) {
#endif
        q->proc[i + 1] = q->proc[i];
        i--;
    }

    q->proc[i + 1] = proc;
    q->size++;
}

struct pcb_t *dequeue(struct queue_t *q)
{
    if (q == NULL || q->size == 0) return NULL;

    /* Head is always highest priority (smallest prio value) */
    struct pcb_t *proc = q->proc[0];

    /* Shift left */
    for (int i = 0; i < q->size - 1; i++)
        q->proc[i] = q->proc[i + 1];

    q->size--;
    return proc;
}

struct pcb_t *purgequeue(struct queue_t *q, struct pcb_t *proc)
{
    if (q == NULL || proc == NULL) return NULL;

    for (int i = 0; i < q->size; i++) {
        if (q->proc[i] == proc) {
            for (int j = i; j < q->size - 1; j++)
                q->proc[j] = q->proc[j + 1];
            q->size--;
            return proc;
        }
    }
    return NULL;
}
