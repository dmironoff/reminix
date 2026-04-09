//
// Created by dmironov on 25.03.2026.
//
#include "kernel/kernel.h"
#include "kernel/vm.h"
#include <minix/abstract_pagetables.h>
#include <minix/vm.h>
#include <minix/physmemorymap.h>
#include "pagetables.h"
#include "arch_configs.h"
#include "vm.h"
#include "string.h"

static uint32_t vm_enabled = 0;

void vm_enable_paging(void)
{
    u32_t sctlr;
    u32_t actlr;

#ifdef ARCH_ARM_CORTEX_A7
    /*Включим когерентность кешей для Cortex A7*/
    actlr = read_actlr();
    actlr |= (1 << 6); // Bit SMP enable
    write_actlr(actlr);
#endif

    write_ttbcr(0);

    /* Set all Domains to Client */
    write_dacr(0x55555555);

    sctlr = read_sctlr();

    sctlr &= (~((u32_t) (1 << 1))); // Alignment check. 1 = ошибка при невыровненном доступе.

    /* Enable MMU */
    sctlr |= CPU_CONTROL_MMU_ENABLE;

    /* TRE set to zero (default reset value): TEX[2:0] are used, plus C and B bits.*/
    sctlr &= ~CPU_CONTROL_TR_ENABLE;

    /* AFE set to zero (default reset value): not using simplified model. */
    sctlr &= ~CPU_CONTROL_AF_ENABLE;

    /* Enable instruction ,data cache and branch prediction */
    sctlr &= ~CPU_CONTROL_DC_ENABLE;
    sctlr |= CPU_CONTROL_IC_ENABLE;
    sctlr |= CPU_CONTROL_BPRD_ENABLE;

    /* Enable barriers */
    sctlr |= CPU_CONTROL_32BD_ENABLE;

#ifdef ARCH_ARM_CORTEX_A8
    /* Enable L2 cache (cortex-a8) */
	#define CORTEX_A8_L2EN   (0x02)
	actlr = read_actlr();
	actlr |= CORTEX_A8_L2EN;
	write_actlr(actlr);
#endif

    refresh_tlb();

    write_sctlr(sctlr);

    vm_enabled = 1;
}

void pg_load_ttbr0(arm_pt_t *pagedir)
{
    if (vm_enabled) {
        clean_cache_range((vir_bytes) pagedir->l1_table, ((vir_bytes) pagedir->l1_table) + ARM_L1_SIZE);
        write_ttbr0(pagedir->l1_phys);
    } else {
        write_ttbr0(pagedir->l1_phys);
    }
}

void pg_load_ttbr1(arm_pt_t *pagedir)
{
    if (vm_enabled) {
        clean_cache_range((vir_bytes) pagedir->l1_table, ((vir_bytes) pagedir->l1_table) + ARM_L1_SIZE);
        write_ttbr1(pagedir->l1_phys);
    } else {
        write_ttbr1(pagedir->l1_phys);
    }
}


vir_bytes map_mmap_region_to_kernel_pt_l1 (uint32_t *l1_phys, mmap_region_t *region, vir_bytes *start, uint32_t flags) {
    vir_bytes end_addr;
    vir_bytes started = 0;
    for (uint32_t i = 0; i < ARM_KERNEL_L1_PAGES; i++) {
        if ((((vir_bytes)i * ARM_SECTION_SIZE + ARM_KERNEL_VIRT_START) >= *start) &&
            ((vir_bytes)i * ARM_SECTION_SIZE + ARM_KERNEL_VIRT_START) <= *start + region->size) {
            if (!started) {
                started = i * ARM_SECTION_SIZE + ARM_KERNEL_VIRT_START;
            }

            l1_phys[i] = ((region->start + ARM_SECTION_SIZE * i) & ARM_L1_ADDR_MASK) | flags;
            end_addr = (i + 1) * ARM_SECTION_SIZE + ARM_KERNEL_VIRT_START;
        } else {
            l1_phys[i] = 0;
        }
    }
    *start = started;
    return end_addr;
}

vir_bytes map_mmap_region_to_pt_l1 (uint32_t *l1_phys, mmap_region_t *region, vir_bytes *start, uint32_t flags) {
    vir_bytes end_addr;
    vir_bytes started = 0;
    for (uint32_t i = 0; i < ARM_USER_L1_PAGES; i++) {
        if ((((vir_bytes)i * ARM_SECTION_SIZE) >= *start) &&
            ((vir_bytes)i * ARM_SECTION_SIZE) <= *start + region->size) {
            if (!started) {
                started = i * ARM_SECTION_SIZE;
            }

            l1_phys[i] = ((region->start + ARM_SECTION_SIZE * i) & ARM_L1_ADDR_MASK) | flags;
            end_addr = (i + 1) * ARM_SECTION_SIZE;
        }
    }
    *start = started;
    return end_addr;
}

vir_bytes map_mmap_region_to_pt_l1_from_end (uint32_t *l1_phys, mmap_region_t *region, vir_bytes *start, uint32_t flags) {
    vir_bytes end_addr;
    vir_bytes started = 0;
    for (int i = ARM_USER_L1_PAGES - 1; i >= 0; i--) {
        if ((((vir_bytes) i * ARM_SECTION_SIZE) <= *start )
            && (((vir_bytes)i * ARM_SECTION_SIZE) >= *start - region->size)) {
            if (!started) {
                started = i * ARM_SECTION_SIZE;
            }

            l1_phys[i] = (((region->start + region->size) - ARM_SECTION_SIZE * i) & ARM_L1_ADDR_MASK) | flags;
            end_addr = (i - 1) * ARM_SECTION_SIZE;
        }
    }
    *start = started;
    return end_addr;
}

int vm_arch_create_pagetable (vir_bytes arch_pagetables, endpoint_t proc, phys_bytes *root_phys_out, uint32_t *handle_out) {
    arm_pt_t *pagetables = (arm_pt_t *) arch_pagetables;
    for (int i = 0; i < ARM_MAX_PT_HANDLES; i++) {
        if (pagetables[i].status == PT_FREE) {
            pagetables[i].status = PT_USED;
            pagetables[i].proc_ep = proc;
            *root_phys_out = pagetables[i].l1_phys;
            *handle_out = (uint32_t) i;
            return OK;
        }
    }
    return ENOMEM;
}

int vm_arch_destroy_pagetable(vir_bytes arch_pagetables, uint32_t handler) {
    arm_pt_t *pagetables = (arm_pt_t *) arch_pagetables;
    if (handler >= ARM_MAX_PT_HANDLES) {
        return EINVAL;
    }
    if (pagetables[handler].status == PT_FREE) {
        return OK;
    }
    pagetables[handler].proc_ep = 0;
    pagetables[handler].status = PT_FREE;
    memset((void *) pagetables[handler].l1_table, 0, ARM_USER_L1_PAGES * sizeof(uint32_t));
    memset((void *) pagetables[handler].l2_tables, 0, ARM_USER_L1_PAGES * sizeof(uint32_t) * 256);
    return OK;
}

/*
 * Наш переводчик флагов между нашей абстракцией на язык arm
 * */
static uint32_t flags_to_arm_l2(vm_apt_flags_t flags, vm_cache_hint_t cache)
{
    uint32_t pte = ARM_L2_TYPE_SMALL;  /* базовый тип: small page 4KB */

    /* Not Global — у пользовательских процессов */
    if (flags & VM_APF_USER)
        pte |= ARM_L2_NG;

    /* Права доступа → AP[2:1:0] */
    int user  = (flags & VM_APF_USER)  ? 1 : 0;
    int write = (flags & VM_APF_WRITE) ? 1 : 0;
    int read  = (flags & VM_APF_READ)  ? 1 : 0;

    if (!read && !write) {
        /* нет доступа — fault при любом обращении */
        pte |= (ARM_AP_NONE << 4);
    } else if (user && write) {
        pte |= (ARM_AP_KRW_URW << 4);   /* kernel и user RW */
    } else if (user && !write) {
        pte |= (ARM_AP_KRO_URO << 4);   /* kernel и user RO */
    } else if (!user && write) {
        pte |= (ARM_AP_KRW_UNO << 4);   /* только kernel RW */
    } else {
        pte |= (ARM_AP_KRO_UNO << 4);   /* только kernel RO */
    }

    /* Execute Never */
    if (!(flags & VM_APF_EXEC))
        pte |= ARM_L2_XN;

    /* Атрибуты кеширования — по подсказке от VM */
    switch (cache) {
        case VM_CACHE_NOCACHE:
            /* Strongly Ordered: MMIO регистры */
            /* TEX=0, C=0, B=0 → уже установлено по умолчанию */
            break;

        case VM_CACHE_WRITECOMB:
            /* Write-Combining: TEX=1, C=0, B=1 */
            pte |= ARM_L2_TEX(1) | ARM_L2_B;
            break;

        case VM_CACHE_WRITETHROUGH:
            /* Write-Through: TEX=0, C=1, B=0 */
            pte |= ARM_L2_C;
            break;

        case VM_CACHE_DMA:
            /* DMA coherent = Strongly Ordered на ARM без IOMMU */
            break;

        case VM_CACHE_NORMAL:
        default:
            /* Write-Back Write-Allocate: наилучшая производительность
             * TEX=1, C=1, B=1, S=1 (Shareable для SMP) */
            pte |= ARM_L2_TEX(1) | ARM_L2_C | ARM_L2_B | ARM_L2_S;
            break;
    }

    return pte;
}

int vm_arch_pt_apply(vir_bytes arch_pagetables, vm_pt_change_t changes) {
    return OK;
}

/*
 * Сердце нашего механизма абстрактных таблиц
 * Aeyrwbz преобразованию абстрактной таблицы в физическую
 */
int vm_arch_apt_to_pt(vm_abstract_pagetables_t *apt, vm_abstract_pt_t *table, vir_bytes arch_pagetables, uint32_t handler) {
    arm_pt_t *pagetables = (arm_pt_t *) arch_pagetables;
    vm_abstract_pt_l1_entry_t *l1_iter;
    vm_abstract_pt_l2_entry_t  *l2_iter;

    if (handler >= ARM_MAX_PT_HANDLES) {
        return EINVAL;
    }
    if (pagetables[handler].status == PT_FREE) {
        return EINVAL;
    }
    if (table->status == VM_RECORD_UNDEF) {
        return EINVAL;
    }

    for (l1_iter = table->first_entry; l1_iter != 0; l1_iter = (vm_abstract_pt_l1_entry_t)l1_iter->next) {
        if (l1_iter->type == VM_APT_L1_L2PT) {
            // Таблица страниц второго уровня

        } else {
            // Обычная секция
            uint32_t start_pde = l1_iter->vaddr / ARM_SECTION_SIZE;
            for (int i = l1_iter->size / ARM_SECTION_SIZE; i >= 0 ; i--) {
                pagetables[handler].l1_table[start_pde + (i)] = (l1_iter->paddr + (i * ARM_SECTION_SIZE))
                                                                    & ARM_L1_ADDR_MASK) | flags;
            }
        }
    }

    return OK;
}