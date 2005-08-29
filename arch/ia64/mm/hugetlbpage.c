/*
 * IA-64 Huge TLB Page Support for Kernel.
 *
 * Copyright (C) 2002-2004 Rohit Seth <rohit.seth@intel.com>
 * Copyright (C) 2003-2004 Ken Chen <kenneth.w.chen@intel.com>
 *
 * Sep, 2003: add numa support
 * Feb, 2004: dynamic hugetlb page size via boot parameter
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/hugetlb.h>
#include <linux/pagemap.h>
#include <linux/smp_lock.h>
#include <linux/slab.h>
#include <linux/sysctl.h>
#include <asm/mman.h>
#include <asm/pgalloc.h>
#include <asm/tlb.h>
#include <asm/tlbflush.h>

unsigned int hpage_shift=HPAGE_SHIFT_DEFAULT;

pte_t *
huge_pte_alloc (struct mm_struct *mm, unsigned long addr)
{
	unsigned long taddr = htlbpage_to_page(addr);
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte = NULL;

	pgd = pgd_offset(mm, taddr);
	pud = pud_alloc(mm, pgd, taddr);
	if (pud) {
		pmd = pmd_alloc(mm, pud, taddr);
		if (pmd)
			pte = pte_alloc_map(mm, pmd, taddr);
	}
	return pte;
}

pte_t *
huge_pte_offset (struct mm_struct *mm, unsigned long addr)
{
	unsigned long taddr = htlbpage_to_page(addr);
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte = NULL;

	pgd = pgd_offset(mm, taddr);
	if (pgd_present(*pgd)) {
		pud = pud_offset(pgd, taddr);
		if (pud_present(*pud)) {
			pmd = pmd_offset(pud, taddr);
			if (pmd_present(*pmd))
				pte = pte_offset_map(pmd, taddr);
		}
	}

	return pte;
}

#define mk_pte_huge(entry) { pte_val(entry) |= _PAGE_P; }

/*
 * This function checks for proper alignment of input addr and len parameters.
 */
int is_aligned_hugepage_range(unsigned long addr, unsigned long len)
{
	if (len & ~HPAGE_MASK)
		return -EINVAL;
	if (addr & ~HPAGE_MASK)
		return -EINVAL;
	if (REGION_NUMBER(addr) != RGN_HPAGE)
		return -EINVAL;

	return 0;
}

struct page *follow_huge_addr(struct mm_struct *mm, unsigned long addr, int write)
{
	struct page *page;
	pte_t *ptep;

	if (REGION_NUMBER(addr) != RGN_HPAGE)
		return ERR_PTR(-EINVAL);

	ptep = huge_pte_offset(mm, addr);
	if (!ptep || pte_none(*ptep))
		return NULL;
	page = pte_page(*ptep);
	page += ((addr & ~HPAGE_MASK) >> PAGE_SHIFT);
	return page;
}
int pmd_huge(pmd_t pmd)
{
	return 0;
}
struct page *
follow_huge_pmd(struct mm_struct *mm, unsigned long address, pmd_t *pmd, int write)
{
	return NULL;
}

void hugetlb_free_pgd_range(struct mmu_gather **tlb,
			unsigned long addr, unsigned long end,
			unsigned long floor, unsigned long ceiling)
{
	/*
	 * This is called only when is_hugepage_only_range(addr,),
	 * and it follows that is_hugepage_only_range(end,) also.
	 *
	 * The offset of these addresses from the base of the hugetlb
	 * region must be scaled down by HPAGE_SIZE/PAGE_SIZE so that
	 * the standard free_pgd_range will free the right page tables.
	 *
	 * If floor and ceiling are also in the hugetlb region, they
	 * must likewise be scaled down; but if outside, left unchanged.
	 */

	addr = htlbpage_to_page(addr);
	end  = htlbpage_to_page(end);
	if (is_hugepage_only_range(tlb->mm, floor, HPAGE_SIZE))
		floor = htlbpage_to_page(floor);
	if (is_hugepage_only_range(tlb->mm, ceiling, HPAGE_SIZE))
		ceiling = htlbpage_to_page(ceiling);

	free_pgd_range(tlb, addr, end, floor, ceiling);
}

unsigned long hugetlb_get_unmapped_area(struct file *file, unsigned long addr, unsigned long len,
		unsigned long pgoff, unsigned long flags)
{
	struct vm_area_struct *vmm;

	if (len > RGN_MAP_LIMIT)
		return -ENOMEM;
	if (len & ~HPAGE_MASK)
		return -EINVAL;
	/* This code assumes that RGN_HPAGE != 0. */
	if ((REGION_NUMBER(addr) != RGN_HPAGE) || (addr & (HPAGE_SIZE - 1)))
		addr = HPAGE_REGION_BASE;
	else
		addr = ALIGN(addr, HPAGE_SIZE);
	for (vmm = find_vma(current->mm, addr); ; vmm = vmm->vm_next) {
		/* At this point:  (!vmm || addr < vmm->vm_end). */
		if (REGION_OFFSET(addr) + len > RGN_MAP_LIMIT)
			return -ENOMEM;
		if (!vmm || (addr + len) <= vmm->vm_start)
			return addr;
		addr = ALIGN(vmm->vm_end, HPAGE_SIZE);
	}
}

static int __init hugetlb_setup_sz(char *str)
{
	u64 tr_pages;
	unsigned long long size;

	if (ia64_pal_vm_page_size(&tr_pages, NULL) != 0)
		/*
		 * shouldn't happen, but just in case.
		 */
		tr_pages = 0x15557000UL;

	size = memparse(str, &str);
	if (*str || (size & (size-1)) || !(tr_pages & size) ||
		size <= PAGE_SIZE ||
		size >= (1UL << PAGE_SHIFT << MAX_ORDER)) {
		printk(KERN_WARNING "Invalid huge page size specified\n");
		return 1;
	}

	hpage_shift = __ffs(size);
	/*
	 * boot cpu already executed ia64_mmu_init, and has HPAGE_SHIFT_DEFAULT
	 * override here with new page shift.
	 */
	ia64_set_rr(HPAGE_REGION_BASE, hpage_shift << 2);
	return 1;
}
__setup("hugepagesz=", hugetlb_setup_sz);
