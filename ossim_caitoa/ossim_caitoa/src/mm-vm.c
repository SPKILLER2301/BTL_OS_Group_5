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
#ifdef MM64
#include "mm64.h"
#endif
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>

/* Forward declaration - defined in mm64.c / mm.c */
addr_t vm_map_ram(struct pcb_t *caller, addr_t astart, addr_t aend,
                  addr_t mapstart, int incpgnum, struct vm_rg_struct *ret_rg);

/*get_vma_by_num - get vm area by numID
 *@mm: memory region
 *@vmaid: ID vm area to alloc memory region
 *
 */
struct vm_area_struct *get_vma_by_num(struct mm_struct *mm, int vmaid)
{
  if (mm == NULL || vmaid < 0)
    return NULL;

  struct vm_area_struct *pvma = mm->mmap;

  while (pvma != NULL) {
    if ((int)pvma->vm_id == vmaid)
      return pvma;

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
  (void)alignedsz;
  if (caller == NULL || caller->krnl == NULL || caller->krnl->mm == NULL)  return NULL;

  struct vm_rg_struct *newrg = malloc(sizeof(struct vm_rg_struct));

  /* TODO retrive current vma to obtain newrg, current comment out due to compiler redundant warning*/
  //struct vm_area_struct *cur_vma = get_vma_by_num(caller->kernl->mm, vmaid);

  //newrg = malloc(sizeof(struct vm_rg_struct));
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->krnl->mm, vmaid);
  if (cur_vma == NULL)
    return NULL;
  if (newrg == NULL)
    return NULL;
  /* TODO: update the newrg boundary
  // newrg->rg_start = ...
  // newrg->rg_end = ...
  */

  newrg->vmaid = vmaid;
  newrg->rg_start = cur_vma->sbrk;
  newrg->rg_end = newrg->rg_start + size;
  newrg->rg_next = NULL;

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
  //struct vm_area_struct *vma = caller->krnl->mm->mmap;
  if (caller == NULL || caller->krnl == NULL || caller->krnl->mm == NULL)
    return -1;
  /* TODO validate the planned memory area is not overlapped */
  if (vmastart >= vmaend)
  {
    return -1;
  }
  struct vm_area_struct *cur_area = get_vma_by_num(caller->krnl->mm, vmaid);
  if (cur_area == NULL)
    return -1;

  struct vm_area_struct *vma = caller->krnl->mm->mmap;
  if (vma == NULL)
  {
    return -1;
  }

  /* TODO validate the planned memory area is not overlapped */


  while (vma != NULL) {
    if (vma != cur_area &&
        OVERLAP(vmastart, vmaend, vma->vm_start, vma->vm_end)) {
      return -1;
    }

    vma = vma->vm_next;
  }
  /* End TODO*/

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
  if (caller == NULL || caller->krnl == NULL || caller->krnl->mm == NULL || inc_sz == 0)
    return -1;

  struct vm_area_struct *cur_vma = get_vma_by_num(caller->krnl->mm, vmaid);
  if (cur_vma == NULL)
    return -1;
  //struct vm_rg_struct *newrg = malloc(sizeof(struct vm_rg_struct));

  /* Align the increment size to page boundary */
#ifdef MM64
  addr_t inc_amt = PAGING64_PAGE_ALIGNSZ(inc_sz);
  int incnumpage = inc_amt / PAGING64_PAGESZ;
#else
  addr_t inc_amt = PAGING_PAGE_ALIGNSZ(inc_sz);
  int incnumpage = inc_amt / PAGING_PAGESZ;
#endif




  addr_t old_end = cur_vma->vm_end;
  addr_t old_sbrk = cur_vma->sbrk;

  /* Extend the vm area end */
  cur_vma->vm_end = old_end + inc_amt;
  cur_vma->sbrk = old_sbrk + inc_sz;

  /* Validate no overlap with other vm areas */
  if (validate_overlap_vm_area(caller, vmaid, cur_vma->vm_start, cur_vma->vm_end) < 0) {
    /* Rollback */
    cur_vma->vm_end = old_end;
    cur_vma->sbrk = old_sbrk;
    return -1;
  }
  struct vm_rg_struct newrg;
  newrg.vmaid = vmaid;
  newrg.rg_start = old_end;
  newrg.rg_end = old_end + inc_amt;
  newrg.rg_next = NULL;

  /* Map the new pages to RAM */
  if (vm_map_ram(caller, old_end, cur_vma->vm_end, old_end, incnumpage, &newrg) < 0) {
    cur_vma->vm_end = old_end;
    cur_vma->sbrk = old_sbrk;
    return -1;
  }

  return 0;
}

// #endif
