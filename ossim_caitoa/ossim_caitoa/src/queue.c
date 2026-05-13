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
        /* Put a new process to queue [q] */
        if (q == NULL || proc == NULL)
                return;
        if (q->size >= MAX_QUEUE_SIZE)
                return;
        q->proc[q->size] = proc;
        q->size++;
}

struct pcb_t *dequeue(struct queue_t *q)
{
        /* Return the PCB with the highest priority (lowest prio value)
         * and remove it from the queue */
        if (q == NULL || q->size == 0)
                return NULL;

        /* Find index of highest-priority process (smallest prio value) */
        int best_idx = 0;
        int i;
        for (i = 1; i < q->size; i++) {
                if (q->proc[i]->prio < q->proc[best_idx]->prio)
                        best_idx = i;
        }

        struct pcb_t *proc = q->proc[best_idx];

        /* Remove the chosen process by shifting remaining elements */
        for (i = best_idx; i < q->size - 1; i++)
                q->proc[i] = q->proc[i + 1];
        q->size--;

        return proc;
}

struct pcb_t *purgequeue(struct queue_t *q, struct pcb_t *proc)
{
        /* Remove a specific item from queue */
        if (q == NULL || proc == NULL)
                return NULL;
        int i;
        for (i = 0; i < q->size; i++) {
                if (q->proc[i] == proc) {
                        int j;
                        for (j = i; j < q->size - 1; j++)
                                q->proc[j] = q->proc[j + 1];
                        q->size--;
                        return proc;
                }
        }
        return NULL;
}