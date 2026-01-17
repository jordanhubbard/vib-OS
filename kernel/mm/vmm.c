/*
 * UnixOS Kernel - Virtual Memory Manager Implementation
 */

#include "mm/vmm.h"
#include "mm/pmm.h"
#include "printk.h"

/* ===================================================================== */
/* Static data */
/* ===================================================================== */

/* Kernel page table (identity mapped initially) */
static uint64_t *kernel_pgd __aligned(PAGE_SIZE);

/* Pre-allocated page tables for early boot */
#define EARLY_TABLES_COUNT  4
static uint64_t early_tables[EARLY_TABLES_COUNT][VMM_ENTRIES] __aligned(PAGE_SIZE);
static size_t early_table_index = 0;

/* ===================================================================== */
/* Helper functions */
/* ===================================================================== */

static inline int pte_index(virt_addr_t vaddr, int level)
{
    int shift;
    switch (level) {
        case 0: shift = VMM_LEVEL0_SHIFT; break;
        case 1: shift = VMM_LEVEL1_SHIFT; break;
        case 2: shift = VMM_LEVEL2_SHIFT; break;
        case 3: shift = VMM_LEVEL3_SHIFT; break;
        default: return -1;
    }
    return (vaddr >> shift) & (VMM_ENTRIES - 1);
}

static inline bool pte_is_valid(uint64_t pte)
{
    return (pte & PTE_VALID) != 0;
}

static inline bool pte_is_table(uint64_t pte)
{
    return (pte & (PTE_VALID | PTE_TABLE)) == (PTE_VALID | PTE_TABLE);
}

static inline phys_addr_t pte_to_phys(uint64_t pte)
{
    return pte & PTE_ADDR_MASK;
}

static inline uint64_t phys_to_pte(phys_addr_t paddr, uint64_t flags)
{
    return (paddr & PTE_ADDR_MASK) | flags;
}

static uint64_t vm_flags_to_pte(uint32_t flags)
{
    uint64_t pte_flags = PTE_VALID | PTE_TABLE | PTE_ACCESSED;
    
    if (flags & VM_DEVICE) {
        pte_flags |= PTE_ATTR_DEVICE | PTE_SH_NONE;
    } else {
        pte_flags |= PTE_ATTR_NORMAL | PTE_SH_INNER;
    }
    
    if (flags & VM_USER) {
        pte_flags |= PTE_USER;
    }
    
    if (!(flags & VM_WRITE)) {
        pte_flags |= PTE_RDONLY;
    }
    
    if (!(flags & VM_EXEC)) {
        if (flags & VM_USER) {
            pte_flags |= PTE_UXN;
        } else {
            pte_flags |= PTE_PXN;
        }
    }
    
    return pte_flags;
}

static uint64_t *alloc_page_table(void)
{
    /* Use early tables if available */
    if (early_table_index < EARLY_TABLES_COUNT) {
        uint64_t *table = early_tables[early_table_index++];
        for (int i = 0; i < VMM_ENTRIES; i++) {
            table[i] = 0;
        }
        return table;
    }
    
    /* Allocate from physical memory */
    phys_addr_t paddr = pmm_alloc_page();
    if (!paddr) {
        return NULL;
    }
    
    uint64_t *table = (uint64_t *)paddr;  /* Identity mapped for now */
    for (int i = 0; i < VMM_ENTRIES; i++) {
        table[i] = 0;
    }
    
    return table;
}

/* ===================================================================== */
/* Page table walking */
/* ===================================================================== */

static uint64_t *walk_page_table(uint64_t *pgd, virt_addr_t vaddr, bool allocate)
{
    uint64_t *table = pgd;
    
    for (int level = 0; level < 3; level++) {
        int idx = pte_index(vaddr, level);
        uint64_t pte = table[idx];
        
        if (!pte_is_valid(pte)) {
            if (!allocate) {
                return NULL;
            }
            
            /* Allocate new table */
            uint64_t *new_table = alloc_page_table();
            if (!new_table) {
                return NULL;
            }
            
            /* Install table entry */
            table[idx] = phys_to_pte((phys_addr_t)new_table, PTE_VALID | PTE_TABLE);
            table = new_table;
        } else if (pte_is_table(pte)) {
            table = (uint64_t *)pte_to_phys(pte);
        } else {
            /* Block mapping - can't continue */
            return NULL;
        }
    }
    
    return table;
}

/* ===================================================================== */
/* Public functions */
/* ===================================================================== */

int vmm_init(void)
{
    printk(KERN_INFO "VMM: Initializing virtual memory manager\n");
    
    /* Allocate kernel page global directory */
    kernel_pgd = alloc_page_table();
    if (!kernel_pgd) {
        printk(KERN_ERR "VMM: Failed to allocate PGD\n");
        return -1;
    }
    
    printk("VMM: Kernel PGD allocated\n");
    
    /* Set up MAIR (Memory Attribute Indirection Register) */
    uint64_t mair = 
        (0xFFUL << 0) |     /* Index 0: Normal, Write-back */
        (0x00UL << 8) |     /* Index 1: Device nGnRnE */
        (0x44UL << 16);     /* Index 2: Normal, Non-cacheable */
    
    asm volatile("msr mair_el1, %0" : : "r" (mair));
    
    /* Set up TCR (Translation Control Register) for 4KB granule, 48-bit VA */
    uint64_t tcr = 
        (16UL << 0) |       /* T0SZ: 48-bit VA for TTBR0 */
        (16UL << 16) |      /* T1SZ: 48-bit VA for TTBR1 */
        (0UL << 14) |       /* TG0: 4KB granule for TTBR0 */
        (2UL << 30) |       /* TG1: 4KB granule for TTBR1 */
        (1UL << 8) |        /* IRGN0: Inner Write-back */
        (1UL << 10) |       /* ORGN0: Outer Write-back */
        (3UL << 12) |       /* SH0: Inner Shareable */
        (1UL << 24) |       /* IRGN1: Inner Write-back */
        (1UL << 26) |       /* ORGN1: Outer Write-back */
        (3UL << 28) |       /* SH1: Inner Shareable */
        (5UL << 32);        /* IPS: 48-bit Output Address */
    
    asm volatile("msr tcr_el1, %0" : : "r" (tcr));
    
    printk("VMM: TCR/MAIR configured\n");
    
    /* Create identity mapping for first 1GB (covers kernel and devices) */
    /* Using 1GB block mappings at level 1 for efficiency */
    
    /* Map 0x00000000-0x3FFFFFFF (first 1GB - RAM) as normal memory */
    int idx0 = pte_index(0x00000000UL, 0);
    uint64_t *l1_table = alloc_page_table();
    if (!l1_table) {
        printk(KERN_ERR "VMM: Failed to allocate L1 table\n");
        return -1;
    }
    kernel_pgd[idx0] = phys_to_pte((phys_addr_t)l1_table, PTE_VALID | PTE_TABLE);
    
    /* Map 0x00000000-0x3FFFFFFF (first 1GB - MMIO) as DEVICE memory */
    l1_table[0] = (0x00000000UL & PTE_ADDR_MASK) | 
                  PTE_VALID | PTE_BLOCK | PTE_ATTR_DEVICE | PTE_SH_NONE | PTE_ACCESSED;
    
    /* Map 0x40000000-0x7FFFFFFF as normal memory (kernel load area) */
    l1_table[1] = (0x40000000UL & PTE_ADDR_MASK) | 
                  PTE_VALID | PTE_BLOCK | PTE_ATTR_NORMAL | PTE_SH_INNER | PTE_ACCESSED;
    
    /* Map High PCI ECAM region (0x40_0000_0000) for 1GB (covers 0x40_1000_0000) */
    /* L1 index 256 (256GB) maps 0x40_0000_0000 - 0x40_3FFF_FFFF */
    /* Map as DEVICE memory (nGnRnE) */
    l1_table[256] = (0x4000000000ULL & PTE_ADDR_MASK) | 
                    PTE_VALID | PTE_BLOCK | PTE_ATTR_DEVICE | PTE_SH_NONE | PTE_ACCESSED;
    
    printk("VMM: RAM identity mapped (0-2GB) + High PCI ECAM (256GB base)\n");
    
    /* Map device region 0x08000000-0x10000000 for GIC, UART etc */
    /* This is at L1 index 0, but we need L2 tables for finer control */
    /* For simplicity, use block mappings - device memory is in first 1GB */
    
    /* Load TTBR0 (identity mapping for kernel boot) */
    asm volatile("msr ttbr0_el1, %0" : : "r" ((uint64_t)kernel_pgd));
    
    /* Load TTBR1 (will be used for high-half kernel later) */
    asm volatile("msr ttbr1_el1, %0" : : "r" ((uint64_t)kernel_pgd));
    
    /* Ensure all writes complete before enabling MMU */
    asm volatile("dsb sy");
    asm volatile("isb");
    
    printk("VMM: TTBRs configured, about to enable MMU...\n");
    
    /* Enable MMU */
    uint64_t sctlr;
    asm volatile("mrs %0, sctlr_el1" : "=r" (sctlr));
    sctlr |= (1 << 0);   /* M: Enable MMU */
    sctlr |= (1 << 2);   /* C: Enable data cache */
    sctlr |= (1 << 12);  /* I: Enable instruction cache */
    asm volatile("msr sctlr_el1, %0" : : "r" (sctlr));
    
    asm volatile("isb");
    
    printk(KERN_INFO "VMM: MMU enabled! Page tables active.\n");
    
    return 0;
}

int vmm_map_page(virt_addr_t vaddr, phys_addr_t paddr, uint32_t flags)
{
    /* Walk to level 3 table, allocating as needed */
    uint64_t *pte_table = walk_page_table(kernel_pgd, vaddr, true);
    if (!pte_table) {
        return -1;
    }
    
    /* Get level 3 index */
    int idx = pte_index(vaddr, 3);
    
    /* Check if already mapped */
    if (pte_is_valid(pte_table[idx])) {
        return -1;  /* Already mapped */
    }
    
    /* Install the page mapping */
    uint64_t pte_flags = vm_flags_to_pte(flags);
    pte_table[idx] = phys_to_pte(paddr, pte_flags);
    
    /* Flush TLB for this page */
    vmm_flush_tlb_page(vaddr);
    
    return 0;
}

int vmm_unmap_page(virt_addr_t vaddr)
{
    uint64_t *pte_table = walk_page_table(kernel_pgd, vaddr, false);
    if (!pte_table) {
        return -1;
    }
    
    int idx = pte_index(vaddr, 3);
    
    if (!pte_is_valid(pte_table[idx])) {
        return -1;  /* Not mapped */
    }
    
    /* Clear the entry */
    pte_table[idx] = 0;
    
    /* Flush TLB */
    vmm_flush_tlb_page(vaddr);
    
    return 0;
}

int vmm_map_range(virt_addr_t vaddr, phys_addr_t paddr, size_t size, uint32_t flags)
{
    vaddr = PAGE_ALIGN_DOWN(vaddr);
    paddr = PAGE_ALIGN_DOWN(paddr);
    size = PAGE_ALIGN(size);
    
    for (size_t offset = 0; offset < size; offset += PAGE_SIZE) {
        int ret = vmm_map_page(vaddr + offset, paddr + offset, flags);
        if (ret < 0) {
            /* Rollback on failure */
            vmm_unmap_range(vaddr, offset);
            return ret;
        }
    }
    
    return 0;
}

int vmm_unmap_range(virt_addr_t vaddr, size_t size)
{
    vaddr = PAGE_ALIGN_DOWN(vaddr);
    size = PAGE_ALIGN(size);
    
    for (size_t offset = 0; offset < size; offset += PAGE_SIZE) {
        vmm_unmap_page(vaddr + offset);
    }
    
    return 0;
}

phys_addr_t vmm_virt_to_phys(virt_addr_t vaddr)
{
    uint64_t *pte_table = walk_page_table(kernel_pgd, vaddr, false);
    if (!pte_table) {
        return 0;
    }
    
    int idx = pte_index(vaddr, 3);
    uint64_t pte = pte_table[idx];
    
    if (!pte_is_valid(pte)) {
        return 0;
    }
    
    return pte_to_phys(pte) | (vaddr & (PAGE_SIZE - 1));
}

struct mm_struct *vmm_create_address_space(void)
{
    /* Allocate mm_struct */
    /* TODO: Use kmalloc when available */
    static struct mm_struct mm_pool[64];
    static int mm_index = 0;
    
    if (mm_index >= 64) {
        return NULL;
    }
    
    struct mm_struct *mm = &mm_pool[mm_index++];
    
    /* Allocate page table */
    mm->pgd = alloc_page_table();
    if (!mm->pgd) {
        return NULL;
    }
    
    mm->vma_list = NULL;
    mm->total_vm = 0;
    mm->users.counter = 1;
    
    /* Copy kernel mappings (upper half) */
    for (int i = VMM_ENTRIES / 2; i < VMM_ENTRIES; i++) {
        mm->pgd[i] = kernel_pgd[i];
    }
    
    return mm;
}

void vmm_destroy_address_space(struct mm_struct *mm)
{
    if (!mm) {
        return;
    }
    
    /* TODO: Free all user page tables */
    /* TODO: Free all VMAs */
    
    mm->pgd = NULL;
    mm->vma_list = NULL;
}

void vmm_switch_address_space(struct mm_struct *mm)
{
    if (!mm || !mm->pgd) {
        return;
    }
    
    /* Load TTBR0 (user page tables) */
    asm volatile("msr ttbr0_el1, %0" : : "r" ((uint64_t)mm->pgd));
    asm volatile("isb");
    vmm_flush_tlb();
}

void vmm_flush_tlb(void)
{
    asm volatile(
        "dsb ishst\n"
        "tlbi vmalle1is\n"
        "dsb ish\n"
        "isb"
    );
}

void vmm_flush_tlb_page(virt_addr_t vaddr)
{
    asm volatile(
        "dsb ishst\n"
        "tlbi vale1is, %0\n"
        "dsb ish\n"
        "isb"
        : : "r" (vaddr >> 12)
    );
}
