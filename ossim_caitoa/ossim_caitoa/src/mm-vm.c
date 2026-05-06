/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* Caitoa release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

//#ifdef MM_PAGING
/*
 * PAGING based Memory Management
 * Virtual memory module mm/mm-vm.c
 */

#include "string.h"
#include "mm.h"
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>

/*get_vma_by_num - get vm area by numID
 *@mm: memory region
 *@vmaid: ID vm area to alloc memory region
 *
 */
struct vm_area_struct *get_vma_by_num(struct mm_struct *mm, int vmaid)
{
  struct vm_area_struct *pvma = mm->mmap;

  if (mm->mmap == NULL)
    return NULL;

  while (pvma != NULL)
  {
    if (pvma->vm_id == vmaid) {
      return pvma;
    }
    pvma = pvma->vm_next;
  }

  return NULL;
}

int __mm_swap_page(struct pcb_t *caller, addr_t vicfpn , addr_t swpfpn)
{
    __swap_cp_page(caller->krnl->mram, vicfpn, caller->krnl->active_mswp, swpfpn);
    return 0;
}

/*get_vm_area_node - get vm area for a number of pages
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@incpgnum: number of page
 *@vmastart: vma end
 *@vmaend: vma end
 *
 */
struct vm_rg_struct *get_vm_area_node_at_brk(struct pcb_t *caller, int vmaid, addr_t size, addr_t alignedsz)
{
  struct vm_rg_struct * newrg;
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->krnl->mm, vmaid);

  newrg = malloc(sizeof(struct vm_rg_struct));
  newrg->rg_start = cur_vma->sbrk;
  newrg->rg_end = newrg->rg_start + size;

  return newrg;
}

/*validate_overlap_vm_area
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@vmastart: vma end
 *@vmaend: vma end
 *
 */
int validate_overlap_vm_area(struct pcb_t *caller, int vmaid, addr_t vmastart, addr_t vmaend)
{
  if (vmastart >= vmaend)
  {
    return -1;
  }

  struct vm_area_struct *vma = caller->krnl->mm->mmap;
  if (vma == NULL)
  {
    return -1;
  }

  struct vm_area_struct *cur_area = get_vma_by_num(caller->krnl->mm, vmaid);
  if (cur_area == NULL)
  {
    return -1;
  }

  while (vma != NULL)
  {
    if (vma != cur_area && OVERLAP(vmastart, vmaend, vma->vm_start, vma->vm_end))
    {
      return -1;
    }
    vma = vma->vm_next;
  }

  return 0;
}

/*inc_vma_limit - increase vm area limits to reserve space for new variable
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@inc_sz: increment size
 *
 */
int inc_vma_limit(struct pcb_t *caller, int vmaid, addr_t inc_sz)
{
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->krnl->mm, vmaid);
  if (cur_vma == NULL) return -1;

  int old_sbrk = cur_vma->sbrk;
  int new_sbrk = old_sbrk + inc_sz;

  if (new_sbrk > cur_vma->vm_end) {
    int inc_amt = new_sbrk - cur_vma->vm_end;
    int inc_sz_aligned;
    int incnumpage;

#ifdef MM64
    inc_sz_aligned = PAGING_PAGE_ALIGNSZ(inc_amt);
    incnumpage = inc_sz_aligned / PAGING_PAGESZ;
#else
    inc_sz_aligned = PAGING_PAGE_ALIGNSZ(inc_amt);
    incnumpage = inc_sz_aligned / PAGING_PAGESZ;
#endif

    int old_end = cur_vma->vm_end;

    if (validate_overlap_vm_area(caller, vmaid, old_end, old_end + inc_sz_aligned) < 0) {
      return -1; /* Overlap and failed allocation */
    }

    struct vm_rg_struct * newrg = malloc(sizeof(struct vm_rg_struct));
    newrg->rg_start = old_end;
    newrg->rg_end = old_end + inc_sz_aligned;

    /* Map the memory to MEMRAM */
    if (vm_map_ram(caller, cur_vma->vm_start, cur_vma->vm_end, 
                     old_end, incnumpage , newrg) < 0) {
      free(newrg);
      return -1; /* Map the memory to MEMRAM */
    }
    cur_vma->vm_end += inc_sz_aligned;
    free(newrg);
  }

  cur_vma->sbrk = new_sbrk;
  return 0;
}

// #endif
