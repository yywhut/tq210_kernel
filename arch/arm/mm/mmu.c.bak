/*
 *  linux/arch/arm/mm/mmu.c
 *
 *  Copyright (C) 1995-2005 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/mman.h>
#include <linux/nodemask.h>
#include <linux/memblock.h>
#include <linux/fs.h>

#include <asm/cputype.h>
#include <asm/sections.h>
#include <asm/cachetype.h>
#include <asm/setup.h>
#include <asm/sizes.h>
#include <asm/smp_plat.h>
#include <asm/tlb.h>
#include <asm/highmem.h>
#include <asm/traps.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include "mm.h"

/*
 * empty_zero_page is a special page that is used for
 * zero-initialized data and COW.
 */
struct page *empty_zero_page;
EXPORT_SYMBOL(empty_zero_page);

/*
 * The pmd table for the upper-most set of pages.
 */
pmd_t *top_pmd;

#define CPOLICY_UNCACHED	0
#define CPOLICY_BUFFERED	1
#define CPOLICY_WRITETHROUGH	2
#define CPOLICY_WRITEBACK	3
#define CPOLICY_WRITEALLOC	4

static unsigned int cachepolicy __initdata = CPOLICY_WRITEBACK;
static unsigned int ecc_mask __initdata = 0;
pgprot_t pgprot_user;
pgprot_t pgprot_kernel;

EXPORT_SYMBOL(pgprot_user);
EXPORT_SYMBOL(pgprot_kernel);

struct cachepolicy {
	const char	policy[16];
	unsigned int	cr_mask;
	unsigned int	pmd;
	pteval_t	pte;
};

static struct cachepolicy cache_policies[] __initdata = {
	{
		.policy		= "uncached",
		.cr_mask	= CR_W|CR_C,
		.pmd		= PMD_SECT_UNCACHED,
		.pte		= L_PTE_MT_UNCACHED,
	}, {
		.policy		= "buffered",
		.cr_mask	= CR_C,
		.pmd		= PMD_SECT_BUFFERED,
		.pte		= L_PTE_MT_BUFFERABLE,
	}, {
		.policy		= "writethrough",
		.cr_mask	= 0,
		.pmd		= PMD_SECT_WT,
		.pte		= L_PTE_MT_WRITETHROUGH,
	}, {
		.policy		= "writeback",
		.cr_mask	= 0,
		.pmd		= PMD_SECT_WB,
		.pte		= L_PTE_MT_WRITEBACK,
	}, {
		.policy		= "writealloc",
		.cr_mask	= 0,
		.pmd		= PMD_SECT_WBWA,
		.pte		= L_PTE_MT_WRITEALLOC,
	}
};

/*
 * These are useful for identifying cache coherency
 * problems by allowing the cache or the cache and
 * writebuffer to be turned off.  (Note: the write
 * buffer should not be on and the cache off).
 */
static int __init early_cachepolicy(char *p)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(cache_policies); i++) {
		int len = strlen(cache_policies[i].policy);

		if (memcmp(p, cache_policies[i].policy, len) == 0) {
			cachepolicy = i;
			cr_alignment &= ~cache_policies[i].cr_mask;
			cr_no_alignment &= ~cache_policies[i].cr_mask;
			break;
		}
	}
	if (i == ARRAY_SIZE(cache_policies))
		printk(KERN_ERR "ERROR: unknown or unsupported cache policy\n");
	/*
	 * This restriction is partly to do with the way we boot; it is
	 * unpredictable to have memory mapped using two different sets of
	 * memory attributes (shared, type, and cache attribs).  We can not
	 * change these attributes once the initial assembly has setup the
	 * page tables.
	 */
	if (cpu_architecture() >= CPU_ARCH_ARMv6) {
		printk(KERN_WARNING "Only cachepolicy=writeback supported on ARMv6 and later\n");
		cachepolicy = CPOLICY_WRITEBACK;
	}
	flush_cache_all();
	set_cr(cr_alignment);
	return 0;
}
early_param("cachepolicy", early_cachepolicy);

static int __init early_nocache(char *__unused)
{
	char *p = "buffered";
	printk(KERN_WARNING "nocache is deprecated; use cachepolicy=%s\n", p);
	early_cachepolicy(p);
	return 0;
}
early_param("nocache", early_nocache);

static int __init early_nowrite(char *__unused)
{
	char *p = "uncached";
	printk(KERN_WARNING "nowb is deprecated; use cachepolicy=%s\n", p);
	early_cachepolicy(p);
	return 0;
}
early_param("nowb", early_nowrite);

static int __init early_ecc(char *p)
{
	if (memcmp(p, "on", 2) == 0)
		ecc_mask = PMD_PROTECTION;
	else if (memcmp(p, "off", 3) == 0)
		ecc_mask = 0;
	return 0;
}
early_param("ecc", early_ecc);

static int __init noalign_setup(char *__unused)
{
	cr_alignment &= ~CR_A;
	cr_no_alignment &= ~CR_A;
	set_cr(cr_alignment);
	return 1;
}
__setup("noalign", noalign_setup);

#ifndef CONFIG_SMP
void adjust_cr(unsigned long mask, unsigned long set)
{
	unsigned long flags;

	mask &= ~CR_A;

	set &= mask;

	local_irq_save(flags);

	cr_no_alignment = (cr_no_alignment & ~mask) | set;
	cr_alignment = (cr_alignment & ~mask) | set;

	set_cr((get_cr() & ~mask) | set);

	local_irq_restore(flags);
}
#endif

#define PROT_PTE_DEVICE		L_PTE_PRESENT|L_PTE_YOUNG|L_PTE_DIRTY|L_PTE_XN
#define PROT_SECT_DEVICE	PMD_TYPE_SECT|PMD_SECT_AP_WRITE

static struct mem_type mem_types[] = {
	[MT_DEVICE] = {		  /* Strongly ordered / ARMv6 shared device */
		.prot_pte	= PROT_PTE_DEVICE | L_PTE_MT_DEV_SHARED |
				  L_PTE_SHARED,
		.prot_l1	= PMD_TYPE_TABLE,
		.prot_sect	= PROT_SECT_DEVICE | PMD_SECT_S,
		.domain		= DOMAIN_IO,
	},
	[MT_DEVICE_NONSHARED] = { /* ARMv6 non-shared device */
		.prot_pte	= PROT_PTE_DEVICE | L_PTE_MT_DEV_NONSHARED,
		.prot_l1	= PMD_TYPE_TABLE,
		.prot_sect	= PROT_SECT_DEVICE,
		.domain		= DOMAIN_IO,
	},
	[MT_DEVICE_CACHED] = {	  /* ioremap_cached */
		.prot_pte	= PROT_PTE_DEVICE | L_PTE_MT_DEV_CACHED,
		.prot_l1	= PMD_TYPE_TABLE,
		.prot_sect	= PROT_SECT_DEVICE | PMD_SECT_WB,
		.domain		= DOMAIN_IO,
	},	
	[MT_DEVICE_WC] = {	/* ioremap_wc */
		.prot_pte	= PROT_PTE_DEVICE | L_PTE_MT_DEV_WC,
		.prot_l1	= PMD_TYPE_TABLE,
		.prot_sect	= PROT_SECT_DEVICE,
		.domain		= DOMAIN_IO,
	},
	[MT_UNCACHED] = {
		.prot_pte	= PROT_PTE_DEVICE,
		.prot_l1	= PMD_TYPE_TABLE,
		.prot_sect	= PMD_TYPE_SECT | PMD_SECT_XN,
		.domain		= DOMAIN_IO,
	},
	[MT_CACHECLEAN] = {
		.prot_sect = PMD_TYPE_SECT | PMD_SECT_XN,
		.domain    = DOMAIN_KERNEL,
	},
	[MT_MINICLEAN] = {
		.prot_sect = PMD_TYPE_SECT | PMD_SECT_XN | PMD_SECT_MINICACHE,
		.domain    = DOMAIN_KERNEL,
	},
	[MT_LOW_VECTORS] = {
		.prot_pte  = L_PTE_PRESENT | L_PTE_YOUNG | L_PTE_DIRTY |
				L_PTE_RDONLY,
		.prot_l1   = PMD_TYPE_TABLE,
		.domain    = DOMAIN_USER,
	},
	[MT_HIGH_VECTORS] = {
		.prot_pte  = L_PTE_PRESENT | L_PTE_YOUNG | L_PTE_DIRTY |
				L_PTE_USER | L_PTE_RDONLY,
		.prot_l1   = PMD_TYPE_TABLE,
		.domain    = DOMAIN_USER,
	},
	[MT_MEMORY] = {
		.prot_pte  = L_PTE_PRESENT | L_PTE_YOUNG | L_PTE_DIRTY,
		.prot_l1   = PMD_TYPE_TABLE,
		.prot_sect = PMD_TYPE_SECT | PMD_SECT_AP_WRITE,
		.domain    = DOMAIN_KERNEL,
	},
	[MT_ROM] = {
		.prot_sect = PMD_TYPE_SECT,
		.domain    = DOMAIN_KERNEL,
	},
	[MT_MEMORY_NONCACHED] = {
		.prot_pte  = L_PTE_PRESENT | L_PTE_YOUNG | L_PTE_DIRTY |
				L_PTE_MT_BUFFERABLE,
		.prot_l1   = PMD_TYPE_TABLE,
		.prot_sect = PMD_TYPE_SECT | PMD_SECT_AP_WRITE,
		.domain    = DOMAIN_KERNEL,
	},
	[MT_MEMORY_DTCM] = {
		.prot_pte  = L_PTE_PRESENT | L_PTE_YOUNG | L_PTE_DIRTY |
				L_PTE_XN,
		.prot_l1   = PMD_TYPE_TABLE,
		.prot_sect = PMD_TYPE_SECT | PMD_SECT_XN,
		.domain    = DOMAIN_KERNEL,
	},
	[MT_MEMORY_ITCM] = {
		.prot_pte  = L_PTE_PRESENT | L_PTE_YOUNG | L_PTE_DIRTY,
		.prot_l1   = PMD_TYPE_TABLE,
		.domain    = DOMAIN_KERNEL,
	},
};

const struct mem_type *get_mem_type(unsigned int type)
{
	return type < ARRAY_SIZE(mem_types) ? &mem_types[type] : NULL;
}
EXPORT_SYMBOL(get_mem_type);

/*
 * Adjust the PMD section entries according to the CPU in use.
 */
static void __init build_mem_type_table(void)
{
	struct cachepolicy *cp;
	unsigned int cr = get_cr();
	unsigned int user_pgprot, kern_pgprot, vecs_pgprot;
	int cpu_arch = cpu_architecture();
	int i;

	if (cpu_arch < CPU_ARCH_ARMv6) {
#if defined(CONFIG_CPU_DCACHE_DISABLE)
		if (cachepolicy > CPOLICY_BUFFERED)
			cachepolicy = CPOLICY_BUFFERED;
#elif defined(CONFIG_CPU_DCACHE_WRITETHROUGH)
		if (cachepolicy > CPOLICY_WRITETHROUGH)
			cachepolicy = CPOLICY_WRITETHROUGH;
#endif
	}
	if (cpu_arch < CPU_ARCH_ARMv5) {
		if (cachepolicy >= CPOLICY_WRITEALLOC)
			cachepolicy = CPOLICY_WRITEBACK;
		ecc_mask = 0;
	}
	if (is_smp())
		cachepolicy = CPOLICY_WRITEALLOC;

	/*
	 * Strip out features not present on earlier architectures.
	 * Pre-ARMv5 CPUs don't have TEX bits.  Pre-ARMv6 CPUs or those
	 * without extended page tables don't have the 'Shared' bit.
	 */
	if (cpu_arch < CPU_ARCH_ARMv5)
		for (i = 0; i < ARRAY_SIZE(mem_types); i++)
			mem_types[i].prot_sect &= ~PMD_SECT_TEX(7);
	if ((cpu_arch < CPU_ARCH_ARMv6 || !(cr & CR_XP)) && !cpu_is_xsc3())
		for (i = 0; i < ARRAY_SIZE(mem_types); i++)
			mem_types[i].prot_sect &= ~PMD_SECT_S;

	/*
	 * ARMv5 and lower, bit 4 must be set for page tables (was: cache
	 * "update-able on write" bit on ARM610).  However, Xscale and
	 * Xscale3 require this bit to be cleared.
	 */
	if (cpu_is_xscale() || cpu_is_xsc3()) {
		for (i = 0; i < ARRAY_SIZE(mem_types); i++) {
			mem_types[i].prot_sect &= ~PMD_BIT4;
			mem_types[i].prot_l1 &= ~PMD_BIT4;
		}
	} else if (cpu_arch < CPU_ARCH_ARMv6) {
		for (i = 0; i < ARRAY_SIZE(mem_types); i++) {
			if (mem_types[i].prot_l1)
				mem_types[i].prot_l1 |= PMD_BIT4;
			if (mem_types[i].prot_sect)
				mem_types[i].prot_sect |= PMD_BIT4;
		}
	}

	/*
	 * Mark the device areas according to the CPU/architecture.
	 */
	if (cpu_is_xsc3() || (cpu_arch >= CPU_ARCH_ARMv6 && (cr & CR_XP))) {
		if (!cpu_is_xsc3()) {
			/*
			 * Mark device regions on ARMv6+ as execute-never
			 * to prevent speculative instruction fetches.
			 */
			mem_types[MT_DEVICE].prot_sect |= PMD_SECT_XN;
			mem_types[MT_DEVICE_NONSHARED].prot_sect |= PMD_SECT_XN;
			mem_types[MT_DEVICE_CACHED].prot_sect |= PMD_SECT_XN;
			mem_types[MT_DEVICE_WC].prot_sect |= PMD_SECT_XN;
		}
		if (cpu_arch >= CPU_ARCH_ARMv7 && (cr & CR_TRE)) {
			/*
			 * For ARMv7 with TEX remapping,
			 * - shared device is SXCB=1100
			 * - nonshared device is SXCB=0100
			 * - write combine device mem is SXCB=0001
			 * (Uncached Normal memory)
			 */
			mem_types[MT_DEVICE].prot_sect |= PMD_SECT_TEX(1);
			mem_types[MT_DEVICE_NONSHARED].prot_sect |= PMD_SECT_TEX(1);
			mem_types[MT_DEVICE_WC].prot_sect |= PMD_SECT_BUFFERABLE;
		} else if (cpu_is_xsc3()) {
			/*
			 * For Xscale3,
			 * - shared device is TEXCB=00101
			 * - nonshared device is TEXCB=01000
			 * - write combine device mem is TEXCB=00100
			 * (Inner/Outer Uncacheable in xsc3 parlance)
			 */
			mem_types[MT_DEVICE].prot_sect |= PMD_SECT_TEX(1) | PMD_SECT_BUFFERED;
			mem_types[MT_DEVICE_NONSHARED].prot_sect |= PMD_SECT_TEX(2);
			mem_types[MT_DEVICE_WC].prot_sect |= PMD_SECT_TEX(1);
		} else {
			/*
			 * For ARMv6 and ARMv7 without TEX remapping,
			 * - shared device is TEXCB=00001
			 * - nonshared device is TEXCB=01000
			 * - write combine device mem is TEXCB=00100
			 * (Uncached Normal in ARMv6 parlance).
			 */
			mem_types[MT_DEVICE].prot_sect |= PMD_SECT_BUFFERED;
			mem_types[MT_DEVICE_NONSHARED].prot_sect |= PMD_SECT_TEX(2);
			mem_types[MT_DEVICE_WC].prot_sect |= PMD_SECT_TEX(1);
		}
	} else {
		/*
		 * On others, write combining is "Uncached/Buffered"
		 */
		mem_types[MT_DEVICE_WC].prot_sect |= PMD_SECT_BUFFERABLE;
	}

	/*
	 * Now deal with the memory-type mappings
	 */
	cp = &cache_policies[cachepolicy];
	vecs_pgprot = kern_pgprot = user_pgprot = cp->pte;

	/*
	 * Only use write-through for non-SMP systems
	 */
	if (!is_smp() && cpu_arch >= CPU_ARCH_ARMv5 && cachepolicy > CPOLICY_WRITETHROUGH)
		vecs_pgprot = cache_policies[CPOLICY_WRITETHROUGH].pte;

	/*
	 * Enable CPU-specific coherency if supported.
	 * (Only available on XSC3 at the moment.)
	 */
	if (arch_is_coherent() && cpu_is_xsc3()) {
		mem_types[MT_MEMORY].prot_sect |= PMD_SECT_S;
		mem_types[MT_MEMORY].prot_pte |= L_PTE_SHARED;
		mem_types[MT_MEMORY_NONCACHED].prot_sect |= PMD_SECT_S;
		mem_types[MT_MEMORY_NONCACHED].prot_pte |= L_PTE_SHARED;
	}
	/*
	 * ARMv6 and above have extended page tables.
	 */
	if (cpu_arch >= CPU_ARCH_ARMv6 && (cr & CR_XP)) {
		/*
		 * Mark cache clean areas and XIP ROM read only
		 * from SVC mode and no access from userspace.
		 */
		mem_types[MT_ROM].prot_sect |= PMD_SECT_APX|PMD_SECT_AP_WRITE;
		mem_types[MT_MINICLEAN].prot_sect |= PMD_SECT_APX|PMD_SECT_AP_WRITE;
		mem_types[MT_CACHECLEAN].prot_sect |= PMD_SECT_APX|PMD_SECT_AP_WRITE;

		if (is_smp()) {
			/*
			 * Mark memory with the "shared" attribute
			 * for SMP systems
			 */
			user_pgprot |= L_PTE_SHARED;
			kern_pgprot |= L_PTE_SHARED;
			vecs_pgprot |= L_PTE_SHARED;
			mem_types[MT_DEVICE_WC].prot_sect |= PMD_SECT_S;
			mem_types[MT_DEVICE_WC].prot_pte |= L_PTE_SHARED;
			mem_types[MT_DEVICE_CACHED].prot_sect |= PMD_SECT_S;
			mem_types[MT_DEVICE_CACHED].prot_pte |= L_PTE_SHARED;
			mem_types[MT_MEMORY].prot_sect |= PMD_SECT_S;
			mem_types[MT_MEMORY].prot_pte |= L_PTE_SHARED;
			mem_types[MT_MEMORY_NONCACHED].prot_sect |= PMD_SECT_S;
			mem_types[MT_MEMORY_NONCACHED].prot_pte |= L_PTE_SHARED;
		}
	}

	/*
	 * Non-cacheable Normal - intended for memory areas that must
	 * not cause dirty cache line writebacks when used
	 */
	if (cpu_arch >= CPU_ARCH_ARMv6) {
		if (cpu_arch >= CPU_ARCH_ARMv7 && (cr & CR_TRE)) {
			/* Non-cacheable Normal is XCB = 001 */
			mem_types[MT_MEMORY_NONCACHED].prot_sect |=
				PMD_SECT_BUFFERED;
		} else {
			/* For both ARMv6 and non-TEX-remapping ARMv7 */
			mem_types[MT_MEMORY_NONCACHED].prot_sect |=
				PMD_SECT_TEX(1);
		}
	} else {
		mem_types[MT_MEMORY_NONCACHED].prot_sect |= PMD_SECT_BUFFERABLE;
	}

	for (i = 0; i < 16; i++) {
		unsigned long v = pgprot_val(protection_map[i]);
		protection_map[i] = __pgprot(v | user_pgprot);
	}

	mem_types[MT_LOW_VECTORS].prot_pte |= vecs_pgprot;
	mem_types[MT_HIGH_VECTORS].prot_pte |= vecs_pgprot;

	pgprot_user   = __pgprot(L_PTE_PRESENT | L_PTE_YOUNG | user_pgprot);
	pgprot_kernel = __pgprot(L_PTE_PRESENT | L_PTE_YOUNG |
				 L_PTE_DIRTY | kern_pgprot);

	mem_types[MT_LOW_VECTORS].prot_l1 |= ecc_mask;
	mem_types[MT_HIGH_VECTORS].prot_l1 |= ecc_mask;
	mem_types[MT_MEMORY].prot_sect |= ecc_mask | cp->pmd;
	mem_types[MT_MEMORY].prot_pte |= kern_pgprot;
	mem_types[MT_MEMORY_NONCACHED].prot_sect |= ecc_mask;
	mem_types[MT_ROM].prot_sect |= cp->pmd;

	switch (cp->pmd) {
	case PMD_SECT_WT:
		mem_types[MT_CACHECLEAN].prot_sect |= PMD_SECT_WT;
		break;
	case PMD_SECT_WB:
	case PMD_SECT_WBWA:
		mem_types[MT_CACHECLEAN].prot_sect |= PMD_SECT_WB;
		break;
	}
	printk("Memory policy: ECC %sabled, Data cache %s\n",
		ecc_mask ? "en" : "dis", cp->policy);

	for (i = 0; i < ARRAY_SIZE(mem_types); i++) {
		struct mem_type *t = &mem_types[i];
		if (t->prot_l1)
			t->prot_l1 |= PMD_DOMAIN(t->domain);
		if (t->prot_sect)
			t->prot_sect |= PMD_DOMAIN(t->domain);
	}
}

#ifdef CONFIG_ARM_DMA_MEM_BUFFERABLE
pgprot_t phys_mem_access_prot(struct file *file, unsigned long pfn,
			      unsigned long size, pgprot_t vma_prot)
{
	if (!pfn_valid(pfn))
		return pgprot_noncached(vma_prot);
	else if (file->f_flags & O_SYNC)
		return pgprot_writecombine(vma_prot);
	return vma_prot;
}
EXPORT_SYMBOL(phys_mem_access_prot);
#endif

#define vectors_base()	(vectors_high() ? 0xffff0000 : 0)

static void __init *early_alloc(unsigned long sz)
{
	void *ptr = __va(memblock_alloc(sz, sz));
	memset(ptr, 0, sz);
	return ptr;
}

static pte_t * __init early_pte_alloc(pmd_t *pmd, unsigned long addr, unsigned long prot)
{

	/*
	判断pmd所指向的L2页表是否存在，不存在则通过early_alloc 函数分配
	PTE_HWTABLE_OFF（512*4=2KB）+PTE_HWTABLE_SIZE（512*4=2KB）总计4KB的一个物理页来存储
	2个linuxpet 页表+2个hwpte页表。

	*/
	if (pmd_none(*pmd)) { //如果一级页表项无效，即还未分配该表项所指向二级页表，即pte table  
		pte_t *pte = early_alloc(PTE_HWTABLE_OFF + PTE_HWTABLE_SIZE);//分配二级页表，即pte tabble 
		__pmd_populate(pmd, __pa(pte), prot);  // 把分配到的虚拟地址的物理地址写入 pmd
		// 这里有个小知识点，为什么分配到的地址可以直接通过__pa(pte)转化成物理地址
		// 因为整个物理内存的低端内存在前面已经跟 0xc0000000做了段式映射了。
		// 所以在整个内核的低端内存区，所分配的物理内存跟0xc0000000处的虚拟地址是一一对应的
											//将pte table的hw/pte page0，hw/pte page1分别填充到一级页表项的低4byte和高4byte
	}
	BUG_ON(pmd_bad(*pmd));

	   /*定位到二级页表pte的虚拟地址*/
	   /*
#define pte_offset_kernel(pmd,addr)	(pmd_page_vaddr(*(pmd)) + pte_index(addr))

// 将物理地址转化为虚拟地址，返回虚拟地址
static inline pte_t *pmd_page_vaddr(pmd_t pmd)
{
	return __va(pmd_val(pmd) & PAGE_MASK);
}

首先 从一级页表中 取出 其中的数据，因为是指针*(pmd) 这个数据是二级页表的物理地址，转化成虚拟地址，再
加上pte的偏移，就找到了 pte的表的虚拟地址

	   */
	return pte_offset_kernel(pmd, addr);  //line574返pte表所在地址的虚拟地址
											//返回二级页表中对应的页表项地址。  
}

static void __init alloc_init_pte(pmd_t *pmd, unsigned long addr,
				  unsigned long end, unsigned long pfn,
				  const struct mem_type *type)
{
 /*这里是做二级页表映射，即让一级页表表项pmd中写入二级页表的地址值

      这里是先检查这个一级页表表项是否已写入了东西，如果没有内容(NULL)，则说明需要创建一个二级页表

      可见，二级页表不同于一级页表，它是动态创建释放的，在slab分配器可用之前，使用bootmem分配器创建*/



	pte_t *pte = early_pte_alloc(pmd, addr, type->prot_l1);
	do {
		// 写二级页表
		// str r1, [r0] 
// *ptep = pfn_pte(phys >> PAGE_SHIFT, prot)，物理地址高20位加上保护位，放到一个second_level table 中的对应项 
		//pfn = 54fff
		set_pte_ext(pte, pfn_pte(pfn, __pgprot(type->prot_pte)), 0);  //  这里进入汇编了 cpu_v7_set_pte_ext
		pfn++;
	} while (pte++, addr += PAGE_SIZE, addr != end);
}
//pmd has been populated, pte = 0xf4ffe7c0, pfn = 0x54fff, pfn_pte = 0x54fff1cb
//before set_pte_ext(): hw_pte = 0xf4ffefc0, *hw_pte = 0x0, linux_pte = 0xf4ffe7c0, *linux_pte = 0x0
//after set_pte_ext(): hw_pte = 0xf4ffefc0, *hw_pte = 0x54fff23a, linux_pte = 0xf4ffe7c0, *linux_pte = 0x54fff1cb

//addr  索要映射的虚拟地址   0xc0000000   0xc0100000  
//pmd 是页表地址  0xc0007000 这里有8个字节，4个字节为一个表，映射1M的空间    0xc0007008
// phys 为索要映射的物理地址   20000000  20100000

/*
alloc_init_section  section
addr:c0000000 
pmd:c0007000 
phys:20000000 
alloc_init_section  section
addr:c0100000 
pmd:c0007000 
phys:20100000 
alloc_init_section  section
addr:c0200000 
pmd:c0007008 
phys:20200000 
alloc_init_section  section
addr:c0300000 
pmd:c0007008 
phys:20300000 


*/


static void __init alloc_init_section(pud_t *pud, unsigned long addr,
				      unsigned long end, phys_addr_t phys,
				      const struct mem_type *type)
{


	//这个pmd_t结构就只是一个ulong大小了，这里函数pmd_offset的实现就是pmd = (pmd_t *)pgd，
	//地址不变，但类型转变，意思很明显；
	pmd_t *pmd = pmd_offset(pud, addr);

	/*
	 * Try a section mapping - end, addr and phys must all be aligned
	 * to a section boundary.  Note that PMDs refer to the individual
	 * L1 entries, whereas PGDs refer to a group of L1 entries making
	 * up one logical pointer to an L2 table.
	 */
	 
	/*
	整个low mem都使用section映射
	如果打开了rodata配置，那么bss所在的最后一个1M会使用page映射
	其余都是page映射
	*/

	//们这里的都是2M对齐的，必然1M也对齐，底下的(addr & SECTION_SIZE)对于我们这里不会成立，
	//我们这里都是2M对齐，即addr值的第21位一直都会是偶数；
	
	if (((addr | end | phys) & ~SECTION_MASK) == 0) {
		pmd_t *p = pmd;

		if (addr & SECTION_SIZE)  // 1M 对齐，这里应该不会进来
			pmd++;

		do {
			*pmd = __pmd(phys | type->prot_sect);//页表的这个条目pmd，写入的值是什么，可见，把物理地址和type的映射方式(prot_sect)写进去了;
			phys += SECTION_SIZE;  //累加1M的物理地址值；
		} while (pmd++, addr += SECTION_SIZE, addr != end);
		//只要虚拟地址起始值addr再累加1M，没有超过end(这里是addr+2M)，那么写下一个页表条目(pmd+1)的值，
		//很明显，我们这里会再循环一次，即do里的内容总共运行过两次；

		flush_pmd_entry(p);//最终写入MMU，这个p指向pmd。
	} else {
		/*
		 * No need to loop; pte's aren't interested in the
		 * individual L1 entries.
		 */
		   /*  使用small页映射，必须为二级表分配空间4Kb(1page) */
		alloc_init_pte(pmd, addr, end, __phys_to_pfn(phys), type);
	}
}

//0xc0007000 0xc0000000,next,0x20000000,type  第一个2M
static void alloc_init_pud(pgd_t *pgd, unsigned long addr, unsigned long end,
	unsigned long phys, const struct mem_type *type)
{
	pud_t *pud = pud_offset(pgd, addr);  //return (pud_t *)pgd;  pud 就是pgd
	unsigned long next;

	do {
		next = pud_addr_end(addr, end);
		alloc_init_section(pud, addr, next, phys, type);
		phys += next - addr;
	} while (pud++, addr = next, addr != end);
}

static void __init create_36bit_mapping(struct map_desc *md,
					const struct mem_type *type)
{
	unsigned long addr, length, end;
	phys_addr_t phys;
	pgd_t *pgd;

	addr = md->virtual;
	phys = __pfn_to_phys(md->pfn);
	length = PAGE_ALIGN(md->length);

	if (!(cpu_architecture() >= CPU_ARCH_ARMv6 || cpu_is_xsc3())) {
		printk(KERN_ERR "MM: CPU does not support supersection "
		       "mapping for 0x%08llx at 0x%08lx\n",
		       (long long)__pfn_to_phys((u64)md->pfn), addr);
		return;
	}

	/* N.B.	ARMv6 supersections are only defined to work with domain 0.
	 *	Since domain assignments can in fact be arbitrary, the
	 *	'domain == 0' check below is required to insure that ARMv6
	 *	supersections are only allocated for domain 0 regardless
	 *	of the actual domain assignments in use.
	 */
	if (type->domain) {
		printk(KERN_ERR "MM: invalid domain in supersection "
		       "mapping for 0x%08llx at 0x%08lx\n",
		       (long long)__pfn_to_phys((u64)md->pfn), addr);
		return;
	}

	if ((addr | length | __pfn_to_phys(md->pfn)) & ~SUPERSECTION_MASK) {
		printk(KERN_ERR "MM: cannot create mapping for 0x%08llx"
		       " at 0x%08lx invalid alignment\n",
		       (long long)__pfn_to_phys((u64)md->pfn), addr);
		return;
	}

	/*
	 * Shift bits [35:32] of address into bits [23:20] of PMD
	 * (See ARMv6 spec).
	 */
	phys |= (((md->pfn >> (32 - PAGE_SHIFT)) & 0xF) << 20);

	pgd = pgd_offset_k(addr);
	end = addr + length;
	do {
		pud_t *pud = pud_offset(pgd, addr);
		pmd_t *pmd = pmd_offset(pud, addr);
		int i;

		for (i = 0; i < 16; i++)
			*pmd++ = __pmd(phys | type->prot_sect | PMD_SECT_SUPER);

		addr += SUPERSECTION_SIZE;
		phys += SUPERSECTION_SIZE;
		pgd += SUPERSECTION_SIZE >> PGDIR_SHIFT;
	} while (addr != end);
}

/*
 * Create the page directory entries and any necessary
 * page tables for the mapping specified by `md'.  We
 * are able to cope here with varying sizes and address
 * offsets, and we take full advantage of sections and
 * supersections.
 */


/*
那么arm呢，arm体系结构的MMU实际上支持两级页表，一级是刚才描述的段式映射即一级映射，再就是支持第二级映射，
包括1K、4K、64K的页实际上使用的是4K页，这里就牵扯到arm页表机制和linux页表机制融合的问题；这里记住，
arm的第一级页表条目数为4096个，对于4K页第二级目录条目个数为256个，一级二级条目都是每个条目4字节；

像这种物理的级数支持少的，砍掉中间目录pmd就可以，从本质看就是函数pmd_offset(pgd, addr)的实现是pgd，
即可什么pmd在arm-linux形同虚设。ARM在linux下二级分页如下：

虚拟地址——> PGD转换——> PTE转换——>物理地址

此外linux的内存管理中，有对页的置属性为“access”、“dirty”的需求，可是arm的MMU没有提供这种属性可以设置；

综合各种原因，最终arm-linux假装第一级目录只有2048个条目，但其实每个条目是2个ulong大小即8字节，所以最终
设置MMU的还是4096个条目，只是每访问1个pgd条目将可以访问到2个pte条目，linux为了实现其内存管理功能又在后面
加上2个对应的假pte表，这个假pte表专门给linux内核代码自己用的，不会影响arm硬件(事实上还有一个重要原因是，
linux要求pte表长度为4K即一页)，在arch/arm/include/asm/pgtable.h中：

*/

/*
create_mapping  
md.pfn:0x20000
md->virtual:0xc0000000
md->length:0x35000000
md->type:9
addr:0xc0000000
phys:0x20000000
length:35000000
*/
static void __init create_mapping(struct map_desc *md)
{
	unsigned long addr, length, end;
	phys_addr_t phys;
	const struct mem_type *type;
	pgd_t *pgd;

	//?*虚拟地址不是中断表地址并且在用户区（0~3G），出错*/
	if (md->virtual != vectors_base() && md->virtual < TASK_SIZE) {
		printk(KERN_WARNING "BUG: not creating mapping for 0x%08llx"
		       " at 0x%08lx in user region\n",
		       (long long)__pfn_to_phys((u64)md->pfn), md->virtual);
		return;
	}
// /*内存类型为IO型或ROM并且虚拟地址为低端内存申请区(3G~3G + 768MB)，出错*/
	if ((md->type == MT_DEVICE || md->type == MT_ROM) &&
	    md->virtual >= PAGE_OFFSET && md->virtual < VMALLOC_END) {
		printk(KERN_WARNING "BUG: mapping for 0x%08llx"
		       " at 0x%08lx overlaps vmalloc space\n",
		       (long long)__pfn_to_phys((u64)md->pfn), md->virtual);
	}

	type = &mem_types[md->type];

	/*
	 * Catch 36-bit addresses
	 /*页表号>1M，超过4G了,实际上这个情况绝大多数情况下不会出现*/
	 */
	if (md->pfn >= 0x100000) {
		create_36bit_mapping(md, type);
		return;
	}

	addr = md->virtual & PAGE_MASK;
	phys = __pfn_to_phys(md->pfn);
	length = PAGE_ALIGN(md->length + (md->virtual & ~PAGE_MASK));

	//意思是本区间不为段式映射(type->prot_l1 == 0)但该区间可以按1M对齐
	//((addr | phys | length) & ~SECTION_MASK)，记住这是一个原则，能按1M对齐的映射空间就按段式映射，
	//不足1M的空间才需要二级映射
	if (type->prot_l1 == 0 && ((addr | phys | length) & ~SECTION_MASK)) {
		printk(KERN_WARNING "BUG: map for 0x%08llx at 0x%08lx can not "
		       "be mapped using pages, ignoring.\n",
		       (long long)__pfn_to_phys(md->pfn), addr);
		return;
	}

	/*
	 获得该虚拟地址addr属于第一级页表（L1）的哪个表项，详细跟踪pgd_offset_k
	 函数（定义在：arch/arm/include/asm/pgtable.h），你会发现，我们内核的L1页目录表的基地址
	 位于0xc0004000，而我们的内核代码则是放置在0xc0008000开始的位置。而从0xc0004000到0xc0008000区间
	 大小是16KB，刚好就是L1页表的大小（见文章开头的描述）
	*/
	pgd = pgd_offset_k(addr);   //add虚拟地址对应的一级页表的虚拟地址  0xc0007000
	end = addr + length;      //计算结束地址
	do {
		unsigned long next = pgd_addr_end(addr, end);  //获得下一段开始地址 ,这里一段是2M的空间

		//申请并初始化一个段

         // 一级页表(段页表)虚拟地址、虚拟起始地址、虚拟结尾地址(要么是addr + 2MB，要么是end)、物理起始地址、内存

		// 0xc0007000 0xc0000000,next,0x20000000,type
		alloc_init_pud(pgd, addr, next, phys, type);

		phys += next - addr;
		addr = next;
	} while (pgd++, addr != end);
}

/*
 * Create the architecture specific mappings
 */
void __init iotable_init(struct map_desc *io_desc, int nr)
{
	int i;

	for (i = 0; i < nr; i++)
		create_mapping(io_desc + i);
}
//#define VMALLOC_END     (0xFD000000)
static void * __initdata vmalloc_min = (void *)(VMALLOC_END - SZ_128M);
//计算的到 vmalloc_min = f5000000
/*
 * vmalloc=size forces the vmalloc area to be exactly 'size'
 * bytes. This can be used to increase (or decrease) the vmalloc
 * area - the default is 128m.
 */
static int __init early_vmalloc(char *arg)
{
	unsigned long vmalloc_reserve = memparse(arg, NULL);

	if (vmalloc_reserve < SZ_16M) {
		vmalloc_reserve = SZ_16M;
		printk(KERN_WARNING
			"vmalloc area too small, limiting to %luMB\n",
			vmalloc_reserve >> 20);
	}

	if (vmalloc_reserve > VMALLOC_END - (PAGE_OFFSET + SZ_32M)) {
		vmalloc_reserve = VMALLOC_END - (PAGE_OFFSET + SZ_32M);
		printk(KERN_WARNING
			"vmalloc area is too big, limiting to %luMB\n",
			vmalloc_reserve >> 20);
	}

	vmalloc_min = (void *)(VMALLOC_END - vmalloc_reserve);
	return 0;
}
early_param("vmalloc", early_vmalloc);

static phys_addr_t lowmem_limit __initdata = 0;

void __init sanity_check_meminfo(void)
{
	int i, j, highmem = 0;
//sanity_check_meminfon meminfo.nr_banks is 2 
	for (i = 0, j = 0; i < meminfo.nr_banks; i++) {
		struct membank *bank = &meminfo.bank[j];
		*bank = meminfo.bank[i];

//由下面代码判断每个物理内存bank是否属于高端内存：
//vmalloc : 0xf5800000 - 0xfd000000   ( 120 MB)
/*
即：该bank的物理内存起始虚拟地址大于VMALLOC_MIN，或者小于PAGE_OFFSET；
PAGE_OFFSET是内核用户空间的交界，在这里定义为0xc0000000也就是arm-linux普遍适用值3G/1G；
VMALLOC_MIN就定义在本文件(arch/arm/mm/mmu.c)，如下：

所以，一个bank的物理内存属于高端内存的条件是：

1、  起始地址不大于vmalloc区域的起始虚拟地址；

2、  起始地址不小于内核用户交界的虚拟地址；
*/
#ifdef CONFIG_HIGHMEM
		if (__va(bank->start) >= vmalloc_min ||
		    __va(bank->start) < (void *)PAGE_OFFSET)
			highmem = 1;

		bank->highmem = highmem;

		/*
		 * Split those memory banks which are partially overlapping
		 * the vmalloc area greatly simplifying things later.
		 */
		if (__va(bank->start) < vmalloc_min &&
		    bank->size > vmalloc_min - __va(bank->start)) {
			if (meminfo.nr_banks >= NR_BANKS) {
				printk(KERN_CRIT "NR_BANKS too low, "
						 "ignoring high memory\n");
			} else {
				memmove(bank + 1, bank,
					(meminfo.nr_banks - i) * sizeof(*bank));
				meminfo.nr_banks++;
				i++;
				bank[1].size -= vmalloc_min - __va(bank->start);
				bank[1].start = __pa(vmalloc_min - 1) + 1;
				bank[1].highmem = highmem = 1;
				j++;
			}
			bank->size = vmalloc_min - __va(bank->start);
		}
#else
		bank->highmem = highmem;

		/*
		 * Check whether this memory bank would entirely overlap
		 * the vmalloc area.
		 */
		 //计算的到 vmalloc_min = f5000000
		 //__va(bank->start) is c0000000 
		if (__va(bank->start) >= vmalloc_min ||
		    __va(bank->start) < (void *)PAGE_OFFSET) {
			printk(KERN_NOTICE "Ignoring RAM at %.8llx-%.8llx "
			       "(vmalloc region overlap).\n",
			       (unsigned long long)bank->start,
			       (unsigned long long)bank->start + bank->size - 1);
			continue;
		}

		/*
		 * Check whether this memory bank would partially overlap
		 * the vmalloc area.
		 */
		 //bank->size is 20000000   第一次进来是512M,第一次不会进入这段代码
		 //第二次进来的时候
		 	//__va(bank->start) is e0000000 
			//(bank->start) is 40000000 
			//bank->size is 20000000 
		if (__va(bank->start + bank->size) > vmalloc_min ||
		    __va(bank->start + bank->size) < __va(bank->start)) {
			unsigned long newsize = vmalloc_min - __va(bank->start);
			printk(KERN_NOTICE "Truncating RAM at %.8llx-%.8llx "
			       "to -%.8llx (vmalloc region overlap).\n",
			       (unsigned long long)bank->start,
			       (unsigned long long)bank->start + bank->size - 1,
			       (unsigned long long)bank->start + newsize - 1);
			bank->size = newsize;  //newsize 15000000 也就是336  跟第一个bank512M相加刚好是848
		}
#endif
		if (!bank->highmem && bank->start + bank->size > lowmem_limit)
			lowmem_limit = bank->start + bank->size;
		//resize lowmem_limitis 40000000 
		//newsize 15000000 
		//lowmem_limitis 40000000 
		//resize lowmem_limitis 55000000   这个值表示的是物理地址最高是在什么位置
		// 也就是说物理地址是分的两块，一块是从0x20000000 到0x40000000
		// 另外一个是从0x40000000 到 0x550000000

		j++;
	}
#ifdef CONFIG_HIGHMEM
	if (highmem) {
		const char *reason = NULL;

		if (cache_is_vipt_aliasing()) {
			/*
			 * Interactions between kmap and other mappings
			 * make highmem support with aliasing VIPT caches
			 * rather difficult.
			 */
			reason = "with VIPT aliasing cache";
		}
		if (reason) {
			printk(KERN_CRIT "HIGHMEM is not supported %s, ignoring high memory\n",
				reason);
			while (j > 0 && meminfo.bank[j - 1].highmem)
				j--;
		}
	}
#endif
	meminfo.nr_banks = j;
	memblock_set_current_limit(lowmem_limit);  //这里是多少?
}


//有两块地址空间区域是不需要清除的，一个是kernel image，另外一个是kernel线性地址映射区
static inline void prepare_page_table(void)
{
	unsigned long addr;
	phys_addr_t end;

	/*
	 * Clear out all the mappings below the kernel image.
	 */
  /*
  MODULES_VADDR(模块区)值为(PAGE_OFFSET - 16*1024*1024)即内核下16MB，

前两个for循环将清除内核空间以下的映射
前两个for循环可以合并为一个，它们的作用是清空地址区间[0x00000000, 0xC0000000)的内存映射；

      pmd_clear是将()中的页表项地址中的Cache无效，以及清除段开始的两个字节

      pmd_off_k为获取addr在一级页表中的位置,但是注意这个地址是虚拟地址

      要注意pmd_clear的实现，注意传进来的地址是什么，为什么操作其后两个成员*/

	//）MODULES_VADDR 0xbf000000
//10）MODULES_END   0xc0000000    这里算刚好16M
 //这里的描述都是对的，但是我还没有找到MODULES_VADDR 是在哪里定义的
  */
  //PGDIR_SIZE 200000 
  	//pmd_off_k(addr):c0004000 
	//pmd_off_k(addr):c0004008 
	//pmd_off_k(addr):c0004010 
	//pmd_off_k(addr):c0004018 

//
	for (addr = 0; addr < MODULES_VADDR; addr += PGDIR_SIZE)
		pmd_clear(pmd_off_k(addr));

#ifdef CONFIG_XIP_KERNEL
	/* The XIP kernel is mapped in the module area -- skip over it */
	addr = ((unsigned long)_etext + PGDIR_SIZE - 1) & PGDIR_MASK;
#endif

  /*再完成模块区MODULES_VADDR到0xc00000000这16M清0*/
	for ( ; addr < PAGE_OFFSET; addr += PGDIR_SIZE)
		pmd_clear(pmd_off_k(addr));

	/*
	 * Find the end of the first block of lowmem.
	 */
	 //memblock.memory.regions[0].base:  20000000, memblock.memory.regions[0].size:35000000 

	end = memblock.memory.regions[0].base + memblock.memory.regions[0].size;
	if (end >= lowmem_limit)
		end = lowmem_limit;

//end 55000000 
	/*
	 * Clear out all the kernel space mappings, except for the first
	 * memory bank, up to the end of the vmalloc region.
	 */
	   /*从物理内存对应的虚拟起始地址开始，直到VMALLOC区结尾，清除*/
//#define VMALLOC_END     (0xFD000000)
	for (addr = __phys_to_virt(end);
	     addr < VMALLOC_END; addr += PGDIR_SIZE)
		pmd_clear(pmd_off_k(addr));
}

/*
 * Reserve the special regions of memory
 */
void __init arm_mm_memblock_reserve(void)
{
	/*
	 * Reserve the page tables.  These are already in use,
	 * and can only be in node 0.
	 */
	/*
		 swapper_pg_dir
arch/arm/kernel/head.S下
.equ    swapper_pg_dir,KERNEL_RAM_VADDR - PG_DIR_SIZE
#define PG_DIR_SIZE    0x4000
KERNEL_RAM_VADDR是0x30008000
    这里是临时页表的保存地址

	*/
	
	memblock_reserve(__pa(swapper_pg_dir), PTRS_PER_PGD * sizeof(pgd_t));

#ifdef CONFIG_SA1111
	/*
	 * Because of the SA1111 DMA bug, we want to preserve our
	 * precious DMA-able memory...
	 */
	memblock_reserve(PHYS_OFFSET, __pa(swapper_pg_dir) - PHYS_OFFSET);
#endif
}

/*
 * Set up device the mappings.  Since we clear out the page tables for all
 * mappings above VMALLOC_END, we will remove any debug device mappings.
 * This means you have to be careful how you debug this function, or any
 * called function.  This means you can't use any function or debugging
 * method which may touch any device, otherwise the kernel _will_ crash.
 */
static void __init devicemaps_init(struct machine_desc *mdesc)
{
	struct map_desc map;
	unsigned long addr;

	//memblock_alloc is 54fff000 
	//early_alloc is f4fff000
	//vectors_page:0xf4fff000
	/*
	 * Allocate the vector page early.
	 */
	vectors_page = early_alloc(PAGE_SIZE);

	////清除pgd映射了虚拟地址VAMLLOC_END-4G的项
    //addr每次步进2M,直到溢出addr=0才结束
	for (addr = VMALLOC_END; addr; addr += PGDIR_SIZE)
		pmd_clear(pmd_off_k(addr));

	/*
	 * Map the kernel if it is XIP.
	 * It is always first in the modulearea.
	 */
#ifdef CONFIG_XIP_KERNEL
	map.pfn = __phys_to_pfn(CONFIG_XIP_PHYS_ADDR & SECTION_MASK);
	map.virtual = MODULES_VADDR;
	map.length = ((unsigned long)_etext - map.virtual + ~SECTION_MASK) & SECTION_MASK;
	map.type = MT_ROM;
	create_mapping(&map);
#endif

	/*
	 * Map the cache flushing regions.
	 */
#ifdef FLUSH_BASE
	map.pfn = __phys_to_pfn(FLUSH_BASE_PHYS);
	map.virtual = FLUSH_BASE;
	map.length = SZ_1M;
	map.type = MT_CACHECLEAN;
	create_mapping(&map);
#endif
#ifdef FLUSH_BASE_MINICACHE
	map.pfn = __phys_to_pfn(FLUSH_BASE_PHYS + SZ_1M);
	map.virtual = FLUSH_BASE_MINICACHE;
	map.length = SZ_1M;
	map.type = MT_MINICLEAN;
	create_mapping(&map);
#endif

	/*
	 * Create a mapping for the machine vectors at the high-vectors
	 * location (0xffff0000).  If we aren't using high-vectors, also
	 * create a mapping at the low-vectors virtual address.
	 */

/*
phys:0x54fff000
vectors_page:0xf4fff000
md->virtual:0xffff0000

把物理地址为0x54fff000的一个page的数据 映射到 0xffff0000 这个虚拟地址上去

*/
	map.pfn = __phys_to_pfn(virt_to_phys(vectors_page));
	map.virtual = 0xffff0000;
	map.length = PAGE_SIZE;
	map.type = MT_HIGH_VECTORS;
	create_mapping(&map);



	

	if (!vectors_high()) {
		map.virtual = 0;
		map.type = MT_LOW_VECTORS;
		create_mapping(&map);
	}

	/*
	 * Ask the machine support to map in the statically mapped devices.
	 */
	if (mdesc->map_io)    // 在这里初始化了系统的频率。
		mdesc->map_io();

	/*
	 * Finally flush the caches and tlb to ensure that we're in a
	 * consistent state wrt the writebuffer.  This also ensures that
	 * any write-allocated cache lines in the vector page are written
	 * back.  After this point, we can start to touch devices again.
	 */
	local_flush_tlb_all();
	flush_cache_all();
}

static void __init kmap_init(void)
{
/*
//获取kmap所对应的虚拟地址[PKMAP_BASE，PAGE_OFFSET]所对应的二级映射表的开始地址。
该二级映射表刚好就是一个物理页的大小 

*/

#ifdef CONFIG_HIGHMEM
	pkmap_page_table = early_pte_alloc(pmd_off_k(PKMAP_BASE),
		PKMAP_BASE, _PAGE_KERNEL_TABLE);
#endif
}
/*
上述函数中的pmd_off_k(PKMAP_BASE)是获取PKMAP_BASE虚拟地址对应的一级映射表中所对应的页表项地址，

*/

static void __init map_lowmem(void)
{
	struct memblock_region *reg;

	/* Map all the lowmem memory banks. */
	for_each_memblock(memory, reg) {
		phys_addr_t start = reg->base;
		phys_addr_t end = start + reg->size;
		struct map_desc map;

		if (end > lowmem_limit)
			end = lowmem_limit;
		if (start >= end)
			break;
		/*
		start :20000000
		end :55000000 
		map.pfn :20000 
		map.virtual:c0000000 
		一共848M

		*/
		map.pfn = __phys_to_pfn(start);
		map.virtual = __phys_to_virt(start);
		map.length = end - start;
		map.type = MT_MEMORY;

		create_mapping(&map);
	}
}

/*
 * paging_init() sets up the page tables, initialises the zone memory
 * maps, and sets up the zero page, bad page and bad page tables.
 */
/*
Paging_init函数首先调用的是build_mem_type_table，这个函数做的事情就是给静态全局变量mem_types赋值，
这个变量就在本文件(arch/arm/mm/mmu.c)定义，它的用处就是在create_mapping函数创建映射时配置MMU硬件时需要；
build_mem_type_table函数里面是完全与本arm芯片自身体系结构相关的配置，我还没完全搞明白。。。
后续再补充吧。

接下来调用的是prepare_page_table，它的作用是清除在内核代码执行到start_kernel之前时创建的大部分临时内存页表，
这里需要对arm-linux内存页表的机制原理进行理解：

*/



void __init paging_init(struct machine_desc *mdesc)
{
	void *zero_page;

	memblock_set_current_limit(lowmem_limit);

	build_mem_type_table();  //根据MEM的策略补充页表的属性
	prepare_page_table();   //清除段映射（16K一级页表，从地址0开始的到0xc0000000,以及物理地址结束地址
	//所对应的虚拟地址到vmalloc_end 的页表） (不包括vmalloc_end以上的高端内存区域)
	map_lowmem();   //这是为物理内存创建内存映射  
	devicemaps_init(mdesc);  // 初始化频率  为中断向量创建内存映射,

/*对于marvell，没有高端内存，无需关心，

      对于高端内存映射初始化，也是从bootmem获取页表表项占用的空间，

      大小为1页(4K)，并在表项中填入表项地址值和属性内容即可*/

	kmap_init();


 /*给全局变量top_pmd赋值，值为0xffff0000在一级页表中的表项位置，

      地址0xffff0000到4G之间为copy_user_page/clear_user_page等函数使用*/

	top_pmd = pmd_off_k(0xffff0000);


    /*由bootmem分配器获取一页物理内存，转换为虚拟地址后赋给zero_page，

      把对应的物理页地址赋值给empty_zero_page并刷新该页*/
	/* allocate the zero page. */
	zero_page = early_alloc(PAGE_SIZE);

	bootmem_init();  // 物理内存初始化  /*为主(物理)内存创建映射；建立bootmem分配器；初始化重要全局变量*/
	 //实现在arch\mm\init.c中，初始化bootmem分配信息

       //实际上是初始化引导阶段所使用的内存，当内核启动完成，将使用SLUB来管理内存，届时管理方法将和boot阶段会不一样。

	empty_zero_page = virt_to_page(zero_page);
	__flush_dcache_page(NULL, empty_zero_page);
}
sys_open
