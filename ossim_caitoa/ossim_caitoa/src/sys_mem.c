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
   int memop = regs->a1;
   BYTE value;

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
   int allocated = 0;
   if (caller == NULL) {
      caller = malloc(sizeof(struct pcb_t));
      caller->krnl  = krnl;
      caller->pid   = pid;
      allocated = 1;
   }

   switch (memop) {
   case SYSMEM_MAP_OP:
            vmap_pgd_memset(caller, regs->a2, regs->a3);
            break;
   case SYSMEM_INC_OP:
            inc_vma_limit(caller, regs->a2, regs->a3);
            break;
   case SYSMEM_SWP_OP:
            __mm_swap_page(caller, regs->a2, regs->a3);
            break;
   case SYSMEM_IO_READ:
            MEMPHY_read(krnl->mram, regs->a2, &value);
            regs->a3 = value;
            break;
   case SYSMEM_IO_WRITE:
            MEMPHY_write(krnl->mram, regs->a2, (BYTE)regs->a3);
            break;
   default:
            printf("Memop code: %d\n", memop);
            break;
   }

   if (allocated)
      free(caller);

   return 0;
}
