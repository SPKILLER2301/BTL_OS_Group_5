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
}

struct pcb_t *dequeue(struct queue_t *q)
{

}

struct pcb_t *purgequeue(struct queue_t *q, struct pcb_t *proc)
{
    return NULL;
}