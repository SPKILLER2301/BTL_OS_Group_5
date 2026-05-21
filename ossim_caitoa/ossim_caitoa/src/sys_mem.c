/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* Caitoa release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

#include "os-mm.h"
#include "syscall.h"
#include "libmem.h"
#include "queue.h"
#include <stdlib.h>
#include <stdio.h>

#ifdef MM64
#include "mm64.h"
#else
#include "mm.h"
#endif

int __sys_memmap(struct krnl_t *krnl, uint32_t pid, struct sc_regs* regs)
{
   if (krnl == NULL || regs == NULL)
        return -1;
   int memop = regs->a1;
   BYTE value;
   int ret = 0;

   /*
    * Kernel-space: access PCB by traversing running_list with PID.
    * Direct PCB pointer passing from user-space is NOT allowed.
    */
   struct pcb_t *caller = NULL;
   struct queue_t *running = krnl->running_list;
   if (running != NULL) {
      int i;
      for (i = 0; i < running->size; i++) {
         if (running->proc[i] != NULL && running->proc[i]->pid == pid) {
            caller = running->proc[i];
            break;
         }
      }
   }

   /* If not found in running_list, build a minimal stub */
   //int allocated = 0;
    if (caller == NULL &&
     (memop == SYSMEM_INC_OP || memop == SYSMEM_MAP_OP || memop == SYSMEM_SWP_OP)) {
        fprintf(stderr, "sys_memmap: cannot find running process with PID=%u\n", pid);
        return -1;
     }

    switch (memop) {
        case SYSMEM_MAP_OP:
            ret = vmap_pgd_memset(caller, regs->a2, regs->a3);
            break;

        case SYSMEM_INC_OP:
            ret = inc_vma_limit(caller, regs->a2, regs->a3);
            break;

        case SYSMEM_SWP_OP:
            ret = __mm_swap_page(caller, regs->a2, regs->a3);
            break;

        case SYSMEM_IO_READ:
            ret = MEMPHY_read(krnl->mram, regs->a2, &value);
            if (ret == 0)
                regs->a3 = value;
            break;

        case SYSMEM_IO_WRITE:
            ret = MEMPHY_write(krnl->mram, regs->a2, (BYTE)regs->a3);
            break;

        default:
            printf("Memop code: %d\n", memop);
            return -1;
    }


   return ret;
}
