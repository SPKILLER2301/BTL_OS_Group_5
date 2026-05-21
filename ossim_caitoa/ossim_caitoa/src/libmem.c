/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* Caitoa release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

// #ifdef MM_PAGING
/*
 * System Library
 * Memory Module Library libmem.c 
 */

#include "string.h"
#include "mm.h"
#include "mm64.h"
#include "syscall.h"
#include "libmem.h"
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>

static pthread_mutex_t mmvm_lock = PTHREAD_MUTEX_INITIALIZER;

static void merge_free_regions(struct vm_area_struct *vma)
{
  if (vma == NULL)
    return;

  /* Sort free regions by start address. */
  struct vm_rg_struct *sorted = NULL;
  struct vm_rg_struct *cur = vma->vm_freerg_list;
  while (cur != NULL) {
    struct vm_rg_struct *next = cur->rg_next;
    if (sorted == NULL || cur->rg_start < sorted->rg_start) {
      cur->rg_next = sorted;
      sorted = cur;
    } else {
      struct vm_rg_struct *it = sorted;
      while (it->rg_next != NULL && it->rg_next->rg_start < cur->rg_start)
        it = it->rg_next;
      cur->rg_next = it->rg_next;
      it->rg_next = cur;
    }
    cur = next;
  }

  /* Merge adjacent/overlapping regions. */
  cur = sorted;
  while (cur != NULL && cur->rg_next != NULL) {
    if (cur->rg_end >= cur->rg_next->rg_start) {
      if (cur->rg_end < cur->rg_next->rg_end)
        cur->rg_end = cur->rg_next->rg_end;
      struct vm_rg_struct *tmp = cur->rg_next;
      cur->rg_next = tmp->rg_next;
      free(tmp);
    } else {
      cur = cur->rg_next;
    }
  }

  vma->vm_freerg_list = sorted;
}
/*enlist_vm_freerg_list - add new rg to freerg_list
 *@mm: memory region
 *@rg_elmt: new region
 *
 */
int enlist_vm_freerg_list(struct mm_struct *mm, struct vm_rg_struct *rg_elmt)
{
  struct vm_rg_struct *rg_node = mm->mmap->vm_freerg_list;

  if (rg_elmt->rg_start >= rg_elmt->rg_end)
    return -1;

  if (rg_node != NULL)
    rg_elmt->rg_next = rg_node;

  /* Enlist the new region */
  mm->mmap->vm_freerg_list = rg_elmt;

  return 0;
}

/*get_symrg_byid - get mem region by region ID
 *@mm: memory region
 *@rgid: region ID act as symbol index of variable
 *
 */
struct vm_rg_struct *get_symrg_byid(struct mm_struct *mm, int rgid)
{
  if (mm == NULL || rgid < 0 || rgid >= PAGING_MAX_SYMTBL_SZ)
    return NULL;

  return &mm->symrgtbl[rgid];
}

/*__alloc - allocate a region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size
 *@alloc_addr: address of allocated memory region
 *
 */
int __alloc(struct pcb_t *caller, int vmaid, int rgid, addr_t size, addr_t *alloc_addr)
{
  if (!caller || !caller->krnl || !caller->krnl->mm || !alloc_addr)
        return -1;

  if (rgid < 0 || rgid >= PAGING_MAX_SYMTBL_SZ || size == 0)
        return -1;
  /*Allocate at the toproof */
  pthread_mutex_lock(&mmvm_lock);
  struct vm_rg_struct rgnode;
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->krnl->mm, vmaid);
    if (!cur_vma) {
        pthread_mutex_unlock(&mmvm_lock);
        return -1;
    }
  int inc_sz=0;

  if (get_free_vmrg_area(caller, vmaid, size, &rgnode) == 0)
  {
    caller->krnl->mm->symrgtbl[rgid].rg_start = rgnode.rg_start;
    caller->krnl->mm->symrgtbl[rgid].rg_end = rgnode.rg_end;
 
    *alloc_addr = rgnode.rg_start;

    pthread_mutex_unlock(&mmvm_lock);
    return 0;
  }

  /* TODO get_free_vmrg_area FAILED handle the region management (Fig.6)*/

  /*Attempt to increate limit to get space */
#ifdef MM64
  inc_sz = (uint32_t)(size/(int)PAGING64_PAGESZ);
  inc_sz = inc_sz + 1;
#else
  inc_sz = PAGING_PAGE_ALIGNSZ(size);
#endif
  int old_sbrk;
  inc_sz = inc_sz + 1;

  old_sbrk = cur_vma->sbrk;

  /* TODO INCREASE THE LIMIT
   * SYSCALL 1 sys_memmap
   */
  struct sc_regs regs;
  regs.a1 = SYSMEM_INC_OP;
  regs.a2 = vmaid;
#ifdef MM64
  regs.a3 = size;
#else
  regs.a3 = PAGING_PAGE_ALIGNSZ(size);
#endif
if (_syscall(caller->krnl, caller->pid, 17, &regs) != 0) {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
}
  _syscall(caller->krnl, caller->pid, 17, &regs); /* SYSCALL 17 sys_memmap */

  /*Successful increase limit */
  caller->krnl->mm->symrgtbl[rgid].rg_start = old_sbrk;
  caller->krnl->mm->symrgtbl[rgid].rg_end = old_sbrk + size;
  caller->krnl->mm->symrgtbl[rgid].rg_end = old_sbrk + size;
  caller->krnl->mm->symrgtbl[rgid].rg_next = NULL;

  *alloc_addr = old_sbrk;

  pthread_mutex_unlock(&mmvm_lock);
  return 0;

}

/*__free - remove a region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size
 *
 */
int __free(struct pcb_t *caller, int vmaid, int rgid)
{
  if (caller == NULL || caller->krnl == NULL || caller->krnl->mm == NULL)
    return -1;
  pthread_mutex_lock(&mmvm_lock);

  if (rgid < 0 || rgid >= PAGING_MAX_SYMTBL_SZ)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  /* TODO: Manage the collect freed region to freerg_list */
  struct vm_rg_struct *rgnode = get_symrg_byid(caller->krnl->mm, rgid);

  if (rgnode->rg_start == 0 && rgnode->rg_end == 0)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }
  struct vm_rg_struct *freerg_node = malloc(sizeof(struct vm_rg_struct));
  if (freerg_node == NULL) {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }
  freerg_node->vmaid = vmaid;
  freerg_node->rg_start = rgnode->rg_start;
  freerg_node->rg_end = rgnode->rg_end;
  freerg_node->rg_next = NULL;

  rgnode->rg_start = rgnode->rg_end = 0;
  rgnode->rg_next = NULL;

  /*enlist the obsoleted memory region */
  enlist_vm_freerg_list(caller->krnl->mm, freerg_node);

  merge_free_regions(get_vma_by_num(caller->krnl->mm, vmaid));
  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}

/*liballoc - PAGING-based allocate a region memory
 *@proc:  Process executing the instruction
 *@size: allocated size
 *@reg_index: memory region ID (used to identify variable in symbole table)
 */
int liballoc(struct pcb_t *proc, addr_t size, uint32_t reg_index)
{
  addr_t  addr;
  int val = __alloc(proc, 0, reg_index, size, &addr);
  if (val == -1)
  {
    return -1;
  }
#ifdef IODUMP
  printf("liballoc:%d\n", __LINE__-4);
//#ifdef PAGETBL_DUMP
//  print_pgtbl(proc, 0, -1); // print max TBL
//#endif
#endif

  /* By default using vmaid = 0 */
  return val;
}

/*libfree - PAGING-based free a region memory
 *@proc: Process executing the instruction
 *@size: allocated size
 *@reg_index: memory region ID (used to identify variable in symbole table)
 */

int libfree(struct pcb_t *proc, uint32_t reg_index)
{
  int val = __free(proc, 0, reg_index);
  if (val == -1)
  {
    return -1;
  }
#ifdef IODUMP
  printf("libfree:%d\n", __LINE__-6);
//#ifdef PAGETBL_DUMP
//  print_pgtbl(proc, 0, -1); // print max TBL
//#endif
#endif
  return 0;
}

/*pg_getpage - get the page in ram
 *@mm: memory region
 *@pagenum: PGN
 *@framenum: return FPN
 *@caller: caller
 *
 */
int pg_getpage(struct mm_struct *mm, int pgn, int *fpn, struct pcb_t *caller)
{

  uint32_t pte = pte_get_entry(caller, pgn);

  if (!PAGING_PAGE_PRESENT(pte))
  { /* Page is not online, make it actively living */
    addr_t vicpgn, swpfpn;
    addr_t tgtfpn;

    /* Find victim page via FIFO */
    if (find_victim_page(caller->krnl->mm, &vicpgn) == -1)
    {
      return -1;
    }

    /* Get free frame in MEMSWP */
    if (MEMPHY_get_freefp(caller->krnl->active_mswp, &swpfpn) == -1)
    {
      return -1;
    }

    /* Get the victim frame number from its PTE */
    uint32_t vic_pte = pte_get_entry(caller, vicpgn);
    tgtfpn = PAGING_FPN(vic_pte);

    /* Swap victim frame out: MEMRAM -> MEMSWP */
    struct sc_regs regs;
    regs.a1 = SYSMEM_SWP_OP;
    regs.a2 = tgtfpn;
    regs.a3 = swpfpn;
    _syscall(caller->krnl, caller->pid, 17, &regs);

    /* Update victim page PTE to swapped state */
    pte_set_swap(caller, vicpgn, 0, swpfpn);

    /* The freed frame (tgtfpn) now holds our target page */
    /* If pte is swapped, copy from swap to RAM */
    if (PAGING_PAGE_PRESENT(pte) && (pte & PAGING_PTE_SWAPPED_MASK)) {
      addr_t swp_off = PAGING_SWP(pte);
      /* Copy from swap to the freed frame */
      __swap_cp_page(caller->krnl->active_mswp, swp_off,
                     caller->krnl->mram, tgtfpn);
      MEMPHY_put_freefp(caller->krnl->active_mswp, swp_off);
    }

    /* Update PTE for target page: now in RAM at tgtfpn */
    pte_set_fpn(caller, pgn, tgtfpn);

    enlist_pgn_node(&caller->krnl->mm->fifo_pgn, pgn);
  }

  *fpn = PAGING_FPN(pte_get_entry(caller,pgn));

  return 0;
}

/*pg_getval - read value at given offset
 *@mm: memory region
 *@addr: virtual address to acess
 *@value: value
 *
 */
int pg_getval(struct mm_struct *mm, addr_t addr, BYTE *data, struct pcb_t *caller)
{
#ifdef MM64
  addr_t pgn = addr >> PAGING64_ADDR_PT_SHIFT;
  addr_t off = addr & (PAGING64_PAGESZ - 1);
#else
  addr_t pgn = PAGING_PGN(addr);
  addr_t off = PAGING_OFFST(addr);
#endif
  int fpn;

  if (pg_getpage(mm, pgn, &fpn, caller) != 0)
    return -1; /* invalid page access */

#ifdef MM64
  addr_t phyaddr = fpn * PAGING64_PAGESZ + off;
#else
  addr_t phyaddr = (fpn << PAGING_ADDR_FPN_LOBIT) + off;
#endif

  /* Read via SYSCALL MEMIO */
  struct sc_regs regs;
  regs.a1 = SYSMEM_IO_READ;
  regs.a2 = phyaddr;
  regs.a3 = 0;
  _syscall(caller->krnl, caller->pid, 17, &regs);
  *data = (BYTE)regs.a3;

  return 0;
}

/*pg_setval - write value to given offset
 *@mm: memory region
 *@addr: virtual address to acess
 *@value: value
 *
 */
int pg_setval(struct mm_struct *mm, addr_t addr, BYTE value, struct pcb_t *caller)
{
#ifdef MM64
  addr_t pgn = addr >> PAGING64_ADDR_PT_SHIFT;
  addr_t off = addr & (PAGING64_PAGESZ - 1);
#else
  addr_t pgn = PAGING_PGN(addr);
  addr_t off = PAGING_OFFST(addr);
#endif

  int fpn;

  /* Get the page to MEMRAM, swap from MEMSWAP if needed */
  if (pg_getpage(mm, pgn, &fpn, caller) != 0)
    return -1;

#ifdef MM64
  addr_t phyaddr = ((addr_t)fpn * PAGING64_PAGESZ) + off;
#else
  addr_t phyaddr = (fpn << PAGING_ADDR_FPN_LOBIT) + off;
#endif

  struct sc_regs regs;
  regs.a1 = SYSMEM_IO_WRITE;
  regs.a2 = phyaddr;
  regs.a3 = value;
  _syscall(caller->krnl, caller->pid, 17, &regs);

  return 0;
}

/*__read - read value in region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@offset: offset to acess in memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size
 *
 */
int __read(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE *data)
{
   if (!caller || !caller->krnl || !caller->krnl->mm || !data)
        return -1;

    struct vm_rg_struct *currg = get_symrg_byid(caller->krnl->mm, rgid);
    if (!currg)
        return -1;

    if (currg->rg_start == 0 && currg->rg_end == 0)
        return -1;

    if (currg->rg_start + offset >= currg->rg_end)
        return -1;

//struct vm_area_struct *cur_vma = get_vma_by_num(caller->krnl->mm, vmaid);

  /* TODO Invalid memory identify */

  return pg_getval(caller->krnl->mm, currg->rg_start + offset, data, caller);
}

/*libread - PAGING-based read a region memory */
int libread(
    struct pcb_t *proc, // Process executing the instruction
    uint32_t source,    // Index of source register
    addr_t offset,    // Source address = [source] + [offset]
    uint32_t* destination)
{
  BYTE data;
  int val = __read(proc, 0, source, offset, &data);

  *destination = data;
#ifdef IODUMP
  printf("libread:%d\n", __LINE__-5);
//#ifdef PAGETBL_DUMP
//  print_pgtbl(proc, 0, -1); // print max TBL
//#endif
#endif

  return val;
}

/*__write - write a region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@offset: offset to acess in memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size
 *
 */
int __write(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE value)
{
  pthread_mutex_lock(&mmvm_lock);
  struct vm_rg_struct *currg = get_symrg_byid(caller->krnl->mm, rgid);

  struct vm_area_struct *cur_vma = get_vma_by_num(caller->krnl->mm, vmaid);

  if (currg == NULL || cur_vma == NULL) /* Invalid memory identify */
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  if (currg->rg_start == 0 && currg->rg_end == 0) {
      pthread_mutex_unlock(&mmvm_lock);
      return -1;
  }

  if (currg->rg_start + offset >= currg->rg_end) {
      pthread_mutex_unlock(&mmvm_lock);
      return -1;
  }
  int ret = pg_setval(caller->krnl->mm, currg->rg_start + offset, value, caller);

  pthread_mutex_unlock(&mmvm_lock);
  return ret;
}

/*libwrite - PAGING-based write a region memory */
int libwrite(
    struct pcb_t *proc,   // Process executing the instruction
    BYTE data,            // Data to be wrttien into memory
    uint32_t destination, // Index of destination register
    addr_t offset)
{
  int val = __write(proc, 0, destination, offset, data);
  if (val == -1)
  {
    return -1;
  }
#ifdef IODUMP
  printf("libwrite:%d\n", __LINE__+86);
//#ifdef PAGETBL_DUMP
//  print_pgtbl(proc, 0, -1); // print max TBL
//#endif
#endif

  return val;
}


/*libkmem_malloc- alloc region memory in kmem
 *@caller: caller
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: memory size
 */

int libkmem_malloc(struct pcb_t * caller, uint32_t size, uint32_t reg_index)
{
  addr_t addr = 0;
  int ret = __kmalloc(caller, 0, reg_index, size, &addr);
  if (ret != 0)
    return ret;

  caller->regs[reg_index] = addr;
  return 0;
}


/*kmalloc - alloc region memory in kmem
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: memory size
 *@alloc_addr: allocated address
 */
addr_t __kmalloc(struct pcb_t *caller, int vmaid, int rgid, addr_t size, addr_t *alloc_addr)
{
  /* Minimal implementation: reserve a region through the same paging VM path.
   * The project headers do not expose a separate kernel symbol table, so this
   * keeps kmalloc functional while preserving the user/kernel syscall flow.
   */
  if (vmaid < 0)
    vmaid = 0;
  return __alloc(caller, vmaid, rgid, size, alloc_addr);
}

/*libkmem_cache_pool_create - create cache pool in kmem
 *@caller: caller
 *@size: memory size
 *@align: alignment size of each cache slot (identical cache slot size)
 *@cache_pool_id: cache pool ID
 */
int libkmem_cache_pool_create(struct pcb_t *caller, uint32_t size, uint32_t align, uint32_t cache_pool_id)
{
  /* Minimal cache-pool backing: allocate one aligned slot-sized region and
   * store it under cache_pool_id. A full slab allocator would need extra
   * structs in os-mm.h, which are not present in the provided src files.
   */
  if (caller == NULL || caller->krnl == NULL || caller->krnl->mm == NULL)
    return -1;

  if (size == 0 || cache_pool_id >= PAGING_MAX_SYMTBL_SZ)
    return -1;

  if (align == 0)
    align = 1;

  addr_t alloc_size = size;
  addr_t rem = alloc_size % align;
  if (rem != 0)
    alloc_size += align - rem;

  if (caller->krnl->mm->kcpooltbl == NULL) {
    caller->krnl->mm->kcpooltbl =
        calloc(PAGING_MAX_SYMTBL_SZ, sizeof(struct kcache_pool_struct));

    if (caller->krnl->mm->kcpooltbl == NULL)
      return -1;
  }
  addr_t addr = 0;
  int ret = __kmalloc(caller, 0, cache_pool_id, alloc_size, &addr);
  if (ret != 0)
    return ret;

  caller->krnl->mm->kcpooltbl[cache_pool_id].size = (int)alloc_size;
  caller->krnl->mm->kcpooltbl[cache_pool_id].align = (int)align;
  caller->krnl->mm->kcpooltbl[cache_pool_id].storage = addr;

  return 0;
}

/*libkmem_cache_alloc - allocate cache slot in cache pool, cache slot has identical size
 * the allocated size is embedded in pool management mechanism
 *@caller: caller
 *@cache_pool_id: cache pool ID
 *@reg_index: memory region index
 */
int libkmem_cache_alloc(struct pcb_t *proc, uint32_t cache_pool_id, uint32_t reg_index)
{
  /* TODO: provide OS level management
   *       and forward the request to helper
   */
  addr_t addr = 0;
  int ret = __kmem_cache_alloc(proc, -1, reg_index, cache_pool_id, &addr);
  if (ret != 0)  return ret;

  proc->regs[reg_index] = addr;

  //krnl->kcpooltbl...
  //krnl->krnl_pgd ...

  return 0;
}

/*kmem_cache_alloc - alloc region memory in kmem cache
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@cache_pool_id: cached pool ID
 *@alloc_addr: allocated address
 */

addr_t __kmem_cache_alloc(struct pcb_t *caller, int vmaid, int rgid, int cache_pool_id, addr_t *alloc_addr)
{
  (void)vmaid;
  if (caller == NULL || caller->krnl == NULL || caller->krnl->mm == NULL || alloc_addr == NULL)
    return -1;

  if (cache_pool_id < 0 || cache_pool_id >= PAGING_MAX_SYMTBL_SZ)
    return -1;

  if (caller->krnl->mm->kcpooltbl == NULL)
    return -1;

  struct kcache_pool_struct *pool = &caller->krnl->mm->kcpooltbl[cache_pool_id];

  if (pool->align <= 0 || pool->size < pool->align || pool->storage == 0)
    return -1;

  *alloc_addr = pool->storage;

  pool->storage += pool->align;
  pool->size -= pool->align;

  if (rgid >= 0 && rgid < PAGING_MAX_SYMTBL_SZ) {
    caller->krnl->mm->symrgtbl[rgid].vmaid = 0;
    caller->krnl->mm->symrgtbl[rgid].rg_start = *alloc_addr;
    caller->krnl->mm->symrgtbl[rgid].rg_end = *alloc_addr + pool->align;
    caller->krnl->mm->symrgtbl[rgid].rg_next = NULL;
  }

  return 0;
}


int libkmem_copy_from_user(struct pcb_t *caller, uint32_t source, uint32_t destination, uint32_t offset, uint32_t size)
{
  if (caller == NULL)
    return -1;

  for (uint32_t i = 0; i < size; i++) {
    BYTE data;
    if (__read_user_mem(caller, 0, source, offset + i, &data) != 0)
      return -1;
    if (__write_kernel_mem(caller, 0, destination, offset + i, data) != 0)
      return -1;
  }

  return 0;
}

int libkmem_copy_to_user(struct pcb_t *caller, uint32_t source, uint32_t destination, uint32_t offset, uint32_t size)
{
  if (caller == NULL)
    return -1;

  for (uint32_t i = 0; i < size; i++) {
    BYTE data;
    if (__read_kernel_mem(caller, 0, source, offset + i, &data) != 0)
      return -1;
    if (__write_user_mem(caller, 0, destination, offset + i, data) != 0)
      return -1;
  }

  return 0;
}


/*__read_kernel_mem - read value in kernel region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@offset: offset to acess in memory region
 *@value: data value
 */
int __read_kernel_mem(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE *data)
{
  /* Minimal project-level implementation: kernel regions are backed by the
   * same paging engine as user regions, but accessed only through these
   * wrappers so copy_from_user/copy_to_user remain explicit.
   */
  if (vmaid < 0)
    vmaid = 0;

  return __read(caller, vmaid, rgid, offset, data);
}

/*__write_kernel_mem - write a kernel region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@offset: offset to acess in memory region
 *@value: data value
 */
int __write_kernel_mem(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE value)
{
  if (vmaid < 0)
    vmaid = 0;

  return __write(caller, vmaid, rgid, offset, value);
}

/*__read_user_mem - read value in user region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@offset: offset to acess in memory region
 *@value: data value
 */
int __read_user_mem(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE *data)
{
  if (vmaid < 0)
    vmaid = 0;

  return __read(caller, vmaid, rgid, offset, data);
}


/*__write_user_mem - write a user region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@offset: offset to acess in memory region
 *@value: data value
 */
int __write_user_mem(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE value)
{
  if (vmaid < 0)
    vmaid = 0;

  return __write(caller, vmaid, rgid, offset, value);
}


/*free_pcb_memphy - collect all memphy of pcb
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@incpgnum: number of page
 */
int free_pcb_memph(struct pcb_t *caller)
{
  pthread_mutex_lock(&mmvm_lock);
  int pagenum, fpn;
  uint32_t pte;

  for (pagenum = 0; pagenum < PAGING_MAX_PGN; pagenum++)
  {
    pte = caller->krnl->mm->pgd[pagenum];

    if (PAGING_PAGE_PRESENT(pte))
    {
      fpn = PAGING_FPN(pte);
      MEMPHY_put_freefp(caller->krnl->mram, fpn);
    }
    else
    {
      fpn = PAGING_SWP(pte);
      MEMPHY_put_freefp(caller->krnl->active_mswp, fpn);
    }
  }

  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}


/*find_victim_page - find victim page
 *@caller: caller
 *@pgn: return page number
 *
 */
int find_victim_page(struct mm_struct *mm, addr_t *retpgn)
{
  if (mm == NULL || retpgn == NULL)
    return -1;

  struct pgn_t *pg = mm->fifo_pgn;
  struct pgn_t *prev = NULL;

  if (pg == NULL)
    return -1;

  /* FIFO: take the oldest page at the tail of fifo_pgn list */
  while (pg->pg_next != NULL)
  {
    prev = pg;
    pg = pg->pg_next;
  }

  *retpgn = pg->pgn;

  if (prev == NULL)
    mm->fifo_pgn = NULL;
  else
    prev->pg_next = NULL;

  free(pg);
  return 0;
}

/*get_free_vmrg_area - get a free vm region
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@size: allocated size
 *
 */
int get_free_vmrg_area(struct pcb_t *caller, int vmaid, int size, struct vm_rg_struct *newrg)
{
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->krnl->mm, vmaid);

  struct vm_rg_struct *rgit = cur_vma->vm_freerg_list;

  if (rgit == NULL)
    return -1;

  /* Probe unintialized newrg */
  newrg->rg_start = newrg->rg_end = -1;

  /* Traverse on list of free vm region to find a fit space */
  while (rgit != NULL)
  {
    if (rgit->rg_start + size <= rgit->rg_end)
    { /* Current region has enough space */
      newrg->rg_start = rgit->rg_start;
      newrg->rg_end = rgit->rg_start + size;

      /* Update left space in chosen region */
      if (rgit->rg_start + size < rgit->rg_end)
      {
        rgit->rg_start = rgit->rg_start + size;
      }
      else
      { /*Use up all space, remove current node */
        /*Clone next rg node */
        struct vm_rg_struct *nextrg = rgit->rg_next;

        /*Cloning */
        if (nextrg != NULL)
        {
          rgit->rg_start = nextrg->rg_start;
          rgit->rg_end = nextrg->rg_end;

          rgit->rg_next = nextrg->rg_next;

          free(nextrg);
        }
        else
        {                                /*End of free list */
          rgit->rg_start = rgit->rg_end; // dummy, size 0 region
          rgit->rg_next = NULL;
        }
      }
      break;
    }
    else
    {
      rgit = rgit->rg_next; // Traverse next rg
    }
  }

  if (newrg->rg_start == -1) // new region not found
    return -1;

  return 0;
}

// #endif
