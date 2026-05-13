/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* LamiaAtrium release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

/*
 * PAGING based Memory Management
 * Memory management unit mm/mm.c
 */

#include "mm64.h"
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <pthread.h>

#if defined(MM64)
static pthread_mutex_t pgtbl_lock = PTHREAD_MUTEX_INITIALIZER;
/*
 * init_pte - Initialize PTE entry
 */
int init_pte(addr_t *pte,
             int pre,    // present
             addr_t fpn,    // FPN
             int drt,    // dirty
             int swp,    // swap
             int swptyp, // swap type
             addr_t swpoff) // swap offset
{
  if (pre != 0) {
    if (swp == 0) { // Non swap ~ page online
      if (fpn == 0)
        return -1;  // Invalid setting

      /* Valid setting with FPN */
      SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
      CLRBIT(*pte, PAGING_PTE_SWAPPED_MASK);
      CLRBIT(*pte, PAGING_PTE_DIRTY_MASK);

      SETVAL(*pte, fpn, PAGING_PTE_FPN_MASK, PAGING_PTE_FPN_LOBIT);
    }
    else
    { // page swapped
      SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
      SETBIT(*pte, PAGING_PTE_SWAPPED_MASK);
      CLRBIT(*pte, PAGING_PTE_DIRTY_MASK);

      SETVAL(*pte, swptyp, PAGING_PTE_SWPTYP_MASK, PAGING_PTE_SWPTYP_LOBIT);
      SETVAL(*pte, swpoff, PAGING_PTE_SWPOFF_MASK, PAGING_PTE_SWPOFF_LOBIT);
    }
  }

  return 0;
}


/*
 * get_pd_from_pagenum - Parse address to 5 page directory level
 * @pgn   : pagenumer
 * @pgd   : page global directory
 * @p4d   : page level directory
 * @pud   : page upper directory
 * @pmd   : page middle directory
 * @pt    : page table 
 */
int get_pd_from_address(addr_t addr, addr_t* pgd, addr_t* p4d, addr_t* pud, addr_t* pmd, addr_t* pt)
{
	/* Extract page direactories */
	*pgd = (addr&PAGING64_ADDR_PGD_MASK)>>PAGING64_ADDR_PGD_LOBIT;
	*p4d = (addr&PAGING64_ADDR_P4D_MASK)>>PAGING64_ADDR_P4D_LOBIT;
	*pud = (addr&PAGING64_ADDR_PUD_MASK)>>PAGING64_ADDR_PUD_LOBIT;
	*pmd = (addr&PAGING64_ADDR_PMD_MASK)>>PAGING64_ADDR_PMD_LOBIT;
	*pt = (addr&PAGING64_ADDR_PT_MASK)>>PAGING64_ADDR_PT_LOBIT;

	/* TODO: implement the page direactories mapping */

	return 0;
}

/*
 * get_pd_from_pagenum - Parse page number to 5 page directory level
 * @pgn   : pagenumer
 * @pgd   : page global directory
 * @p4d   : page level directory
 * @pud   : page upper directory
 * @pmd   : page middle directory
 * @pt    : page table 
 */
int get_pd_from_pagenum(addr_t pgn, addr_t* pgd, addr_t* p4d, addr_t* pud, addr_t* pmd, addr_t* pt)
{
	/* Shift the address to get page num and perform the mapping*/
	return get_pd_from_address(pgn << PAGING64_ADDR_PT_SHIFT,
                         pgd,p4d,pud,pmd,pt);
}


/*
 * pte_set_swap - Set PTE entry for swapped page
 * @pte    : target page table entry (PTE)
 * @swptyp : swap type
 * @swpoff : swap offset
 */
int pte_set_swap(struct pcb_t *caller, addr_t pgn, int swptyp, addr_t swpoff)
{
pthread_mutex_lock(&pgtbl_lock);
  struct krnl_t *krnl = caller->krnl;

  addr_t *pte;
  addr_t pgd=0;
  addr_t p4d=0;
  addr_t pud=0;
  addr_t pmd=0;
  addr_t pt=0;
	
  // dummy pte alloc to avoid runtime error
  //pte = malloc(sizeof(addr_t));
#ifdef MM64	
  /* Get value from the system */
  /* TODO Perform multi-level page mapping */
  get_pd_from_pagenum(pgn, &pgd, &p4d, &pud, &pmd, &pt);

  addr_t *pgd_table = krnl->mm->pgd;

  if (pgd_table[pgd] == 0) { pgd_table[pgd] = (addr_t)calloc(512, sizeof(addr_t)); }
  addr_t *p4d_table = (addr_t *)pgd_table[pgd];

  if (p4d_table[p4d] == 0) { p4d_table[p4d] = (addr_t)calloc(512, sizeof(addr_t)); }
  addr_t *pud_table = (addr_t *)p4d_table[p4d];

  if (pud_table[pud] == 0) { pud_table[pud] = (addr_t)calloc(512, sizeof(addr_t)); }
  addr_t *pmd_table = (addr_t *)pud_table[pud];

  if (pmd_table[pmd] == 0) { pmd_table[pmd] = (addr_t)calloc(512, sizeof(addr_t)); }
  addr_t *pt_table = (addr_t *)pmd_table[pmd];

  pte = &pt_table[pt];
#else
  pte = &krnl->mm->pgd[pgn];
#endif
	
  SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
  SETBIT(*pte, PAGING_PTE_SWAPPED_MASK);

  SETVAL(*pte, swptyp, PAGING_PTE_SWPTYP_MASK, PAGING_PTE_SWPTYP_LOBIT);
  SETVAL(*pte, swpoff, PAGING_PTE_SWPOFF_MASK, PAGING_PTE_SWPOFF_LOBIT);
pthread_mutex_unlock(&pgtbl_lock);
  return 0;
}

/*
 * pte_set_fpn - Set PTE entry for on-line page
 * @pte   : target page table entry (PTE)
 * @fpn   : frame page number (FPN)
 */
int pte_set_fpn(struct pcb_t *caller, addr_t pgn, addr_t fpn)
{
pthread_mutex_lock(&pgtbl_lock);
  struct krnl_t *krnl = caller->krnl;

  addr_t *pte;
  addr_t pgd=0;
  addr_t p4d=0;
  addr_t pud=0;
  addr_t pmd=0;
  addr_t pt=0;

  // pte = malloc(sizeof(addr_t));

#ifdef MM64
  /* Get value from the system */
  /* TODO Perform multi-level page mapping */
  get_pd_from_pagenum(pgn, &pgd, &p4d, &pud, &pmd, &pt);

  addr_t *pgd_table = krnl->mm->pgd;

  if (pgd_table[pgd] == 0) { pgd_table[pgd] = (addr_t)calloc(512, sizeof(addr_t)); }
  addr_t *p4d_table = (addr_t *)pgd_table[pgd];

  if (p4d_table[p4d] == 0) { p4d_table[p4d] = (addr_t)calloc(512, sizeof(addr_t)); }
  addr_t *pud_table = (addr_t *)p4d_table[p4d];

  if (pud_table[pud] == 0) { pud_table[pud] = (addr_t)calloc(512, sizeof(addr_t)); }
  addr_t *pmd_table = (addr_t *)pud_table[pud];

  if (pmd_table[pmd] == 0) { pmd_table[pmd] = (addr_t)calloc(512, sizeof(addr_t)); }
  addr_t *pt_table = (addr_t *)pmd_table[pmd];

  pte = &pt_table[pt];

#else
  pte = &krnl->mm->pgd[pgn];
#endif

  SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
  CLRBIT(*pte, PAGING_PTE_SWAPPED_MASK);

  SETVAL(*pte, fpn, PAGING_PTE_FPN_MASK, PAGING_PTE_FPN_LOBIT);

pthread_mutex_unlock(&pgtbl_lock);
  return 0;
}


/* Get PTE page table entry
 * @caller : caller
 * @pgn    : page number
 * @ret    : page table entry
 **/
uint32_t pte_get_entry(struct pcb_t *caller, addr_t pgn)
{
pthread_mutex_lock(&pgtbl_lock);
//struct krnl_t *krnl = caller->krnl;
  //uint32_t pte = 0;
  addr_t pgd=0;
  addr_t p4d=0;
  addr_t pud=0;
  addr_t pmd=0;
  addr_t	pt=0;
	
  /* TODO Perform multi-level page mapping */
  get_pd_from_pagenum(pgn, &pgd, &p4d, &pud, &pmd, &pt);

  addr_t *pgd_table = caller->krnl->mm->pgd;
    if (pgd_table == NULL || pgd_table[pgd] == 0) {
        pthread_mutex_unlock(&pgtbl_lock);
        return 0;
    }
    addr_t *p4d_table = (addr_t *)pgd_table[pgd];
    if (p4d_table[p4d] == 0) {
        pthread_mutex_unlock(&pgtbl_lock);
        return 0;
    }
    addr_t *pud_table = (addr_t *)p4d_table[p4d];
    if (pud_table[pud] == 0) {
        pthread_mutex_unlock(&pgtbl_lock);
        return 0;
    }
    addr_t *pmd_table = (addr_t *)pud_table[pud];
    if (pmd_table[pmd] == 0) {
        pthread_mutex_unlock(&pgtbl_lock);
        return 0;
    }
    addr_t *pt_table = (addr_t *)pmd_table[pmd];
    uint32_t result = (uint32_t)pt_table[pt];
    pthread_mutex_unlock(&pgtbl_lock);
    return result;
}

/* Set PTE page table entry
 * @caller : caller
 * @pgn    : page number
 * @ret    : page table entry
 **/
int pte_set_entry(struct pcb_t *caller, addr_t pgn, uint32_t pte_val)
{
pthread_mutex_lock(&pgtbl_lock);
#ifdef MM64
    addr_t pgd=0, p4d=0, pud=0, pmd=0, pt=0;
    get_pd_from_pagenum(pgn, &pgd, &p4d, &pud, &pmd, &pt);

    addr_t *pgd_table = caller->krnl->mm->pgd;
    if (pgd_table[pgd] == 0)
        pgd_table[pgd] = (addr_t)calloc(512, sizeof(addr_t));
    addr_t *p4d_table = (addr_t *)pgd_table[pgd];
    if (p4d_table[p4d] == 0)
        p4d_table[p4d] = (addr_t)calloc(512, sizeof(addr_t));
    addr_t *pud_table = (addr_t *)p4d_table[p4d];
    if (pud_table[pud] == 0)
        pud_table[pud] = (addr_t)calloc(512, sizeof(addr_t));
    addr_t *pmd_table = (addr_t *)pud_table[pud];
    if (pmd_table[pmd] == 0)
        pmd_table[pmd] = (addr_t)calloc(512, sizeof(addr_t));
    addr_t *pt_table = (addr_t *)pmd_table[pmd];
    pt_table[pt] = (addr_t)pte_val;
#else
    caller->krnl->mm->pgd[pgn] = pte_val;
#endif
pthread_mutex_unlock(&pgtbl_lock);
    return 0;
}


/*
 * vmap_pgd_memset - map a range of page at aligned address
 */
int vmap_pgd_memset(struct pcb_t *caller,           // process call
                    addr_t addr,                       // start address which is aligned to pagesz
                    int pgnum)                      // num of mapping page
{
pthread_mutex_lock(&pgtbl_lock);
  int pgit = 0;
  uint64_t pattern = 0xdeadbeef;

  /* TODO memset the page table with given pattern
   */
  for (pgit = 0; pgit < pgnum; pgit++)
  {
    // v-address for page
    addr_t current_addr = addr + (pgit * PAGING64_PAGESZ);
    addr_t pgd_idx = 0, p4d_idx = 0, pud_idx = 0, pmd_idx = 0, pt_idx = 0;

    //5 index
    get_pd_from_address(current_addr, &pgd_idx, &p4d_idx, &pud_idx, &pmd_idx, &pt_idx);

    addr_t *pgd_table = caller->krnl->mm->pgd;

    if (pgd_table[pgd_idx] == 0) {
      pgd_table[pgd_idx] = (addr_t)calloc(512, sizeof(addr_t));
    }
    addr_t *p4d_table = (addr_t *)pgd_table[pgd_idx];

    if (p4d_table[p4d_idx] == 0) {
      p4d_table[p4d_idx] = (addr_t)calloc(512, sizeof(addr_t));
    }
    addr_t *pud_table = (addr_t *)p4d_table[p4d_idx];

    if (pud_table[pud_idx] == 0) {
      pud_table[pud_idx] = (addr_t)calloc(512, sizeof(addr_t));
    }
    addr_t *pmd_table = (addr_t *)pud_table[pud_idx];

    if (pmd_table[pmd_idx] == 0) {
      pmd_table[pmd_idx] = (addr_t)calloc(512, sizeof(addr_t));
    }
    addr_t *pt_table = (addr_t *)pmd_table[pmd_idx];

    pt_table[pt_idx] = pattern;
  }
pthread_mutex_unlock(&pgtbl_lock);
  return 0;
}

/*
 * z_range - map a range of page at aligned address
 */
addr_t vmap_page_range(struct pcb_t *caller,           // process call
                    addr_t addr,                       // start address which is aligned to pagesz
                    int pgnum,                      // num of mapping page
                    struct framephy_struct *frames, // list of the mapped frames
                    struct vm_rg_struct *ret_rg)    // return mapped region, the real mapped fp
{                                                   // no guarantee all given pages are mapped
	struct framephy_struct *fpit = frames;
    int pgit = 0;
    addr_t pgn;

  /* TODO: update the rg_end and rg_start of ret_rg 
  //ret_rg->rg_end =  ....
  //ret_rg->rg_start = ...
  //ret_rg->vmaid = ...
  */
  ret_rg->rg_start = addr;
  ret_rg->rg_end = addr + pgnum * PAGING64_PAGESZ;

  /* TODO map range of frame to address space
   *      [addr to addr + pgnum*PAGING_PAGESZ
   *      in page table caller->krnl->mm->pgd,
   *                    caller->krnl->mm->pud...
   *                    ...
   */

  /* Tracking for later page replacement activities (if needed)
   * Enqueue new usage page */
  //enlist_pgn_node(&caller->krnl->mm->fifo_pgn, pgn64 + pgit);
  for (pgit = 0; pgit < pgnum && fpit != NULL; pgit++) {
        pgn = (addr >> PAGING64_ADDR_PT_SHIFT) + pgit;
        pte_set_fpn(caller, pgn, fpit->fpn);
        enlist_pgn_node(&caller->krnl->mm->fifo_pgn, pgn);
        fpit = fpit->fp_next;
    }
  return 0;
}

/*
 * alloc_pages_range - allocate req_pgnum of frame in ram
 * @caller    : caller
 * @req_pgnum : request page num
 * @frm_lst   : frame list
 */

addr_t alloc_pages_range(struct pcb_t *caller, int req_pgnum, struct framephy_struct **frm_lst)
{
  addr_t fpn;
  int pgit;
  struct framephy_struct *newfp = NULL;

  /* TODO: allocate the page 
  //caller-> ...
  //frm_lst-> ...
  */


/*
  for (pgit = 0; pgit < req_pgnum; pgit++)
  {
    // TODO: allocate the page 
    if (MEMPHY_get_freefp(caller->mram, &fpn) == 0)
    {
      newfp_str->fpn = fpn;
    }
    else
    { // TODO: ERROR CODE of obtaining somes but not enough frames
    }
  }
*/
for (pgit = 0; pgit < req_pgnum; pgit++) {
        if (MEMPHY_get_freefp(caller->krnl->mram, &fpn) != 0)
            return -3000; /* out of RAM — caller handles swap */

        newfp = malloc(sizeof(struct framephy_struct));
        newfp->fpn    = fpn;
        newfp->fp_next = *frm_lst;
        *frm_lst = newfp;
    }


  /* End TODO */

  return 0;
}

/*
 * vm_map_ram - do the mapping all vm are to ram storage device
 * @caller    : caller
 * @astart    : vm area start
 * @aend      : vm area end
 * @mapstart  : start mapping point
 * @incpgnum  : number of mapped page
 * @ret_rg    : returned region
 */
addr_t vm_map_ram(struct pcb_t *caller, addr_t astart, addr_t aend, addr_t mapstart, int incpgnum, struct vm_rg_struct *ret_rg)
{
  struct framephy_struct *frm_lst = NULL;
  addr_t ret_alloc = 0;
  int pgnum = incpgnum;

  /*@bksysnet: author provides a feasible solution of getting frames
   *FATAL logic in here, wrong behaviour if we have not enough page
   *i.e. we request 1000 frames meanwhile our RAM has size of 3 frames
   *Don't try to perform that case in this simple work, it will result
   *in endless procedure of swap-off to get frame and we have not provide
   *duplicate control mechanism, keep it simple
   */
   ret_alloc = alloc_pages_range(caller, pgnum, &frm_lst);

  if (ret_alloc < 0 && ret_alloc != -3000)
    return -1;

  /* Out of memory */
  if (ret_alloc == -3000)
  {
    return -1;
  }

  /* it leaves the case of memory is enough but half in ram, half in swap
   * do the swaping all to swapper to get the all in ram */
   vmap_page_range(caller, mapstart, incpgnum, frm_lst, ret_rg);

  return 0;
}

/* Swap copy content page from source frame to destination frame
 * @mpsrc  : source memphy
 * @srcfpn : source physical page number (FPN)
 * @mpdst  : destination memphy
 * @dstfpn : destination physical page number (FPN)
 **/
int __swap_cp_page(struct memphy_struct *mpsrc, addr_t srcfpn,
                   struct memphy_struct *mpdst, addr_t dstfpn)
{
  int cellidx;
  addr_t addrsrc, addrdst;
  for (cellidx = 0; cellidx < PAGING_PAGESZ; cellidx++)
  {
    addrsrc = srcfpn * PAGING_PAGESZ + cellidx;
    addrdst = dstfpn * PAGING_PAGESZ + cellidx;

    BYTE data;
    MEMPHY_read(mpsrc, addrsrc, &data);
    MEMPHY_write(mpdst, addrdst, data);
  }

  return 0;
}

/*
 *Initialize a empty Memory Management instance
 * @mm:     self mm
 * @caller: mm owner
 */
int init_mm(struct mm_struct *mm, struct pcb_t *caller)
{
  struct vm_area_struct *vma0 = malloc(sizeof(struct vm_area_struct));

  /* TODO init page table directory */
  mm->pgd = (addr_t *)calloc(512, sizeof(addr_t));

  mm->p4d = NULL;
  mm->pud = NULL;
  mm->pmd = NULL;
  mm->pt = NULL;


  /* By default the owner comes with at least one vma */
  vma0->vm_id = 0;
  vma0->vm_start = 0;
  vma0->vm_end = vma0->vm_start;
  vma0->sbrk = vma0->vm_start;
  struct vm_rg_struct *first_rg = init_vm_rg(vma0->vm_start, vma0->vm_end);
  enlist_vm_rg_node(&vma0->vm_freerg_list, first_rg);

  /* TODO update VMA0 next */
  vma0->vm_next = NULL;

  /* Point vma owner backward */
  vma0->vm_mm = mm;

  /* TODO: update mmap */
  mm->mmap = vma0;

  for (int i = 0; i < PAGING_MAX_SYMTBL_SZ; i++) {
    mm->symrgtbl[i].rg_start = 0;
    mm->symrgtbl[i].rg_end = 0;
    mm->symrgtbl[i].vmaid = 0;
    mm->symrgtbl[i].rg_next = NULL;
  }

  //Reset kernel cache pool
  mm->kcpooltbl = NULL;
  return 0;
}

struct vm_rg_struct *init_vm_rg(addr_t rg_start, addr_t rg_end)
{
  struct vm_rg_struct *rgnode = malloc(sizeof(struct vm_rg_struct));

  rgnode->rg_start = rg_start;
  rgnode->rg_end = rg_end;
  rgnode->rg_next = NULL;

  return rgnode;
}

int enlist_vm_rg_node(struct vm_rg_struct **rglist, struct vm_rg_struct *rgnode)
{
  rgnode->rg_next = *rglist;
  *rglist = rgnode;

  return 0;
}

int enlist_pgn_node(struct pgn_t **plist, addr_t pgn)
{
  struct pgn_t *pnode = malloc(sizeof(struct pgn_t));

  pnode->pgn = pgn;
  pnode->pg_next = *plist;
  *plist = pnode;

  return 0;
}

int print_list_fp(struct framephy_struct *ifp)
{
  struct framephy_struct *fp = ifp;

  printf("print_list_fp: ");
  if (fp == NULL) { printf("NULL list\n"); return -1;}
  printf("\n");
  while (fp != NULL)
  {
    printf("fp[" FORMAT_ADDR "]\n", fp->fpn);
    fp = fp->fp_next;
  }
  printf("\n");
  return 0;
}

int print_list_rg(struct vm_rg_struct *irg)
{
  struct vm_rg_struct *rg = irg;

  printf("print_list_rg: ");
  if (rg == NULL) { printf("NULL list\n"); return -1; }
  printf("\n");
  while (rg != NULL)
  {
    printf("rg[" FORMAT_ADDR "->"  FORMAT_ADDR "]\n", rg->rg_start, rg->rg_end);
    rg = rg->rg_next;
  }
  printf("\n");
  return 0;
}

int print_list_vma(struct vm_area_struct *ivma)
{
  struct vm_area_struct *vma = ivma;

  printf("print_list_vma: ");
  if (vma == NULL) { printf("NULL list\n"); return -1; }
  printf("\n");
  while (vma != NULL)
  {
    printf("va[" FORMAT_ADDR "->" FORMAT_ADDR "]\n", vma->vm_start, vma->vm_end);
    vma = vma->vm_next;
  }
  printf("\n");
  return 0;
}

int print_list_pgn(struct pgn_t *ip)
{
  printf("print_list_pgn: ");
  if (ip == NULL) { printf("NULL list\n"); return -1; }
  printf("\n");
  while (ip != NULL)
  {
    printf("va[" FORMAT_ADDR "]-\n", ip->pgn);
    ip = ip->pg_next;
  }
  printf("n");
  return 0;
}

int print_pgtbl(struct pcb_t *caller, addr_t start, addr_t end)
{
    addr_t pgd_i, p4d_i, pud_i, pmd_i, pt_i;
    get_pd_from_address(start, &pgd_i, &p4d_i, &pud_i, &pmd_i, &pt_i);

    addr_t *pgd_table = caller->krnl->mm->pgd;
    if (!pgd_table) return -1;

    for (int g = 0; g < 512; g++) {
        if (!pgd_table[g]) continue;
        addr_t *p4 = (addr_t *)pgd_table[g];
        for (int f = 0; f < 512; f++) {
            if (!p4[f]) continue;
            addr_t *pu = (addr_t *)p4[f];
            for (int u = 0; u < 512; u++) {
                if (!pu[u]) continue;
                addr_t *pm = (addr_t *)pu[u];
                for (int m = 0; m < 512; m++) {
                    if (!pm[m]) continue;
                    addr_t *pt = (addr_t *)pm[m];
                    for (int t = 0; t < 512; t++) {
                        if (!pt[t]) continue;
                        printf("PGD[%d]P4D[%d]PUD[%d]PMD[%d]PT[%d] → PTE=0x%x\n",
                               g, f, u, m, t, (uint32_t)pt[t]);
                    }
                }
            }
        }
    }
    return 0;
}

#endif  //def MM64
