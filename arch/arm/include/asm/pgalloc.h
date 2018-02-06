/*
 *  arch/arm/include/asm/pgalloc.h
 *
 *  Copyright (C) 2000-2001 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef _ASMARM_PGALLOC_H
#define _ASMARM_PGALLOC_H

#include <linux/pagemap.h>

#include <asm/domain.h>
#include <asm/pgtable-hwdef.h>
#include <asm/processor.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>

#define check_pgt_cache()		do { } while (0)

#ifdef CONFIG_MMU

#define _PAGE_USER_TABLE	(PMD_TYPE_TABLE | PMD_BIT4 | PMD_DOMAIN(DOMAIN_USER))
#define _PAGE_KERNEL_TABLE	(PMD_TYPE_TABLE | PMD_BIT4 | PMD_DOMAIN(DOMAIN_KERNEL))

/*
 * Since we have only two-level page tables, these are trivial
 */
#define pmd_alloc_one(mm,addr)		({ BUG(); ((pmd_t *)2); })
#define pmd_free(mm, pmd)		do { } while (0)
#define pgd_populate(mm,pmd,pte)	BUG()

extern pgd_t *pgd_alloc(struct mm_struct *mm);
extern void pgd_free(struct mm_struct *mm, pgd_t *pgd);

#define PGALLOC_GFP	(GFP_KERNEL | __GFP_NOTRACK | __GFP_REPEAT | __GFP_ZERO)

static inline void clean_pte_table(pte_t *pte)
{
	clean_dcache_area(pte + PTE_HWTABLE_PTRS, PTE_HWTABLE_SIZE);
}

/*
 * Allocate one PTE table.
 *
 * This actually allocates two hardware PTE tables, but we wrap this up
 * into one table thus:
 *
 *  +------------+
 *  | Linux pt 0 |
 *  +------------+
 *  | Linux pt 1 |
 *  +------------+
 *  |  h/w pt 0  |
 *  +------------+
 *  |  h/w pt 1  |
 *  +------------+
 */
static inline pte_t *
pte_alloc_one_kernel(struct mm_struct *mm, unsigned long addr)
{
	pte_t *pte;

	pte = (pte_t *)__get_free_page(PGALLOC_GFP);
	if (pte)
		clean_pte_table(pte);

	return pte;
}

static inline pgtable_t
pte_alloc_one(struct mm_struct *mm, unsigned long addr)
{
	struct page *pte;

#ifdef CONFIG_HIGHPTE
	pte = alloc_pages(PGALLOC_GFP | __GFP_HIGHMEM, 0);
#else
	pte = alloc_pages(PGALLOC_GFP, 0);
#endif
	if (pte) {
		if (!PageHighMem(pte))
			clean_pte_table(page_address(pte));
		pgtable_page_ctor(pte);
	}

	return pte;
}

/*
 * Free one PTE table.
 */
static inline void pte_free_kernel(struct mm_struct *mm, pte_t *pte)
{
	if (pte)
		free_page((unsigned long)pte);
}

static inline void pte_free(struct mm_struct *mm, pgtable_t pte)
{
	pgtable_page_dtor(pte);
	__free_page(pte);
}

static inline void __pmd_populate(pmd_t *pmdp, phys_addr_t pte,
	unsigned long prot)
{

	/*
在执行这个函数之前，2个L2页表已经建立，该函数的作用就是设置L1页表的对应表项，使其指向刚建立的2个L2
页表（hwpte0，hwpte1），正如前面所说，由于linux的L1页表项是8个字节大小，所以：

line133 将头4个字节指向hwpte0页表，

line135 将后4个字节指向hwpte1页表，至此L1---〉L2页表的关联已经建立。
注意那个4k的分配的 二级页表的page，前面1k是 linux用的 256个条目，每个条目4个字节的二级页表，
第二个1k也是linux用的，第三个1k是 mmu用的，第四个也是mmu用到的，是硬件用到的
注意这里是加了PTE_HWTABLE_OFF，所以实际上写的是后面 mmu用的到地方，也就是hw的
	*/
	unsigned long pmdval = (pte + PTE_HWTABLE_OFF) | prot;
	pmdp[0] = __pmd(pmdval);
	pmdp[1] = __pmd(pmdval + 256 * sizeof(pte_t)); //加了1k
	flush_pmd_entry(pmdp);
}

/*
 * Populate the pmdp entry with a pointer to the pte.  This pmd is part
 * of the mm address space.
 *
 * Ensure that we always set both PMD entries.
 */
static inline void
pmd_populate_kernel(struct mm_struct *mm, pmd_t *pmdp, pte_t *ptep)
{
	/*
	 * The pmd must be loaded with the physical address of the PTE table
	 */
	__pmd_populate(pmdp, __pa(ptep), _PAGE_KERNEL_TABLE);
}

static inline void
pmd_populate(struct mm_struct *mm, pmd_t *pmdp, pgtable_t ptep)
{
	__pmd_populate(pmdp, page_to_phys(ptep), _PAGE_USER_TABLE);
}
#define pmd_pgtable(pmd) pmd_page(pmd)

#endif /* CONFIG_MMU */

#endif
