/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* Caitoa release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

#include "syscall.h"
#include "common.h"

#include <stdlib.h>
#include "queue.h"
#include "mm.h"
#include "os-mm.h"

#define __SYSCALL(nr, sym) extern int __##sym(struct krnl_t*, uint32_t,struct sc_regs*);
#include "syscalltbl.lst"
#undef  __SYSCALL

/*
 * The sys_call_table[] is used for system calls, but to know the system
 * call address.
 */
#define __SYSCALL(nr, sym) #nr "-" #sym,
const char* sys_call_table[] = {
#include "syscalltbl.lst"
};
#undef  __SYSCALL
const int syscall_table_size = sizeof(sys_call_table)/sizeof(char*);

int __sys_ni_syscall(struct krnl_t *krnl, struct sc_regs *regs)
{
   /*
    * DUMMY systemcall
    */

   return 0;
}

#define __SYSCALL(nr, sym) case nr: return __##sym(krnl,pid,regs);
int _syscall(struct krnl_t *krnl, uint32_t pid, uint32_t nr, struct sc_regs* regs)
{
	switch (nr) {
	#include "syscalltbl.lst"
	default: return __sys_ni_syscall(krnl, regs);
	}
};


#include <stdlib.h>

static struct pcb_t* get_pcb_by_pid(struct krnl_t *krnl, uint32_t pid) {
    if (krnl == NULL || krnl->running_list == NULL) return NULL;
    
    struct queue_t *q = krnl->running_list;
    
    for (int i = 0; i < q->size; i++) {
        if (q->proc[i] != NULL && q->proc[i]->pid == pid) {
            return q->proc[i];
        }
    }
    return NULL;
}

int __sys_kmalloc(struct krnl_t *krnl, uint32_t pid, struct sc_regs *regs) {
    int size = regs->a1;
    int reg_idx = regs->a2;
    int num_frames = (size + PAGING_PAGESZ - 1) / PAGING_PAGESZ;
    addr_t fpn;
    
    if (MEMPHY_get_contiguous_freefp(krnl->mram, num_frames, &fpn) == 0) {
        addr_t physical_addr = fpn * PAGING_PAGESZ;
        struct pcb_t *caller = get_pcb_by_pid(krnl, pid);
        if(caller) caller->regs[reg_idx] = physical_addr;
        return 0;
    }
    return -1;
}

int __sys_kmem_cache_create(struct krnl_t *krnl, uint32_t pid, struct sc_regs *regs) {
    int size = regs->a1;
    int align = regs->a2;
    int pool_id = regs->a3;
    int capacity = 10;
    int total_bytes = (size + align) * capacity;
    int num_frames = (total_bytes + PAGING_PAGESZ - 1) / PAGING_PAGESZ;
    addr_t fpn;
    
    if (MEMPHY_get_contiguous_freefp(krnl->mram, num_frames, &fpn) == 0) {
        struct kcache_pool_struct *new_pool = malloc(sizeof(struct kcache_pool_struct));
        new_pool->cache_pool_id = pool_id;
        new_pool->size = size;
        new_pool->align = align;
        new_pool->capacity = capacity;
        new_pool->storage = fpn * PAGING_PAGESZ;
        new_pool->free_map = calloc(capacity, sizeof(char));
        
        struct pcb_t *caller = get_pcb_by_pid(krnl, pid);
        if (caller && caller->mm) {
            new_pool->next = caller->krnl->mm->kcpooltbl;
            caller->krnl->mm->kcpooltbl = new_pool;
        }
        return 0;
    }
    return -1;
}

int __sys_kmem_cache_alloc(struct krnl_t *krnl, uint32_t pid, struct sc_regs *regs) {
    int reg_idx = regs->a1;
    int pool_id = regs->a2;
    struct pcb_t *caller = get_pcb_by_pid(krnl, pid);
    
    if (!caller || !caller->krnl->mm) return -1;
    
    struct kcache_pool_struct *pool = caller->mm->kcpooltbl;
    while (pool != NULL) {
        if (pool->cache_pool_id == pool_id) {
            for(int i = 0; i < pool->capacity; i++) {
                if (pool->free_map[i] == 0) {
                    pool->free_map[i] = 1;
                    addr_t object_addr = pool->storage + i * (pool->size + pool->align);
                    caller->regs[reg_idx] = object_addr;
                    return 0;
                }
            }
            return -1;
        }
        pool = pool->next;
    }
    return -1;
}
