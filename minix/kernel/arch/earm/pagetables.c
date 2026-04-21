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
#include "kernel/mmap_utils.h"
#include "kernel/apt_utils.h"

static uint32_t vm_enabled = 0;

/* TODO: Сделать механизм выделения и хранения таблиц виртуальной памяти процессов менее жрущим память - сократить количество выделяемых на автомате таблиц l2*/

/*
 * Включение MMU
 * Полностью архитектурнозависимая функция
 * Перед её выполнением нужно загрузить таблицы страниц в регистры
 */
void vm_arch_enable_paging(void)
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

/*
 * Загрузка таблицы страниц в регистр ttbr0 - пользовательское пространство
 */
void pg_load_ttbr0(vir_bytes arch_pagetables, uint32_t handler)
{
    arm_pt_t *pagedir = &((arm_pt_t *) arch_pagetables)[handler];
    if (vm_enabled) {
        clean_cache_range((vir_bytes) pagedir->l1_table, ((vir_bytes) pagedir->l1_table) + ARM_L1_SIZE);
        write_ttbr0(pagedir->l1_phys);
    } else {
        write_ttbr0(pagedir->l1_phys);
    }
}

/*
 * Выделить память для таблицы страниц l1
 */
int vm_arch_alloc_l1_table(mmap_t *mmap, vm_abstract_pagetables_t *apt, vm_abstract_pt_t *apt_table, arm_pt_t *pt){
    int res = 0;
    mmap_region_t *new_region;

    res = mmap_alloc_lowest_region(mmap, mmap_align(mmap, sizeof(uint32_t) * ARM_L1_ENTRIES), new_region);
    if (res < 0) {
        return res;
    }

    vir_bytes vir_addr;
    res = apt_map_region_to_max_free_end(apt, apt_table, new_region, &vir_addr, VM_APF_KERNEL |
            VM_APF_RW | VM_APF_PRESENT);
    if (res < 0) {
        return res;
    }

    pt->l1_phys = new_region->start;
    pt->l1_table = (uint32_t *) vir_addr;
    pt->l1_table_region = new_region;
    return OK;
}

/*
 * Выделить память для таблицы страниц l2
 */
int vm_arch_alloc_l2_table(mmap_t *mmap, vm_abstract_pagetables_t *apt, vm_abstract_pt_t *apt_table, arm_pt_t *pt){
    int res = 0;
    mmap_region_t *new_region;

    res = mmap_alloc_lowest_region(mmap, mmap_align(mmap, sizeof(uint32_t) * ARM_L1_ENTRIES * ARM_L2_ENTRIES), new_region);
    if (res < 0) {
        return res;
    }

    vir_bytes vir_addr;
    res = apt_map_region_to_max_free_end(apt, apt_table, new_region, &vir_addr, VM_APF_KERNEL |
                                                                                VM_APF_RW | VM_APF_PRESENT);
    if (res < 0) {
        return res;
    }

    pt->l2_phys = new_region->start;
    pt->l2_tables = (uint32_t *) vir_addr;
    pt->l2_tables_region = new_region;
    return OK;
}

/*
 * Выделить новую таблицу страниц из пула
 * Возвращает через указатели адрес начала для загрузки в регистр и хэндлер для использования в функциях
 */
int vm_arch_alloc_pagetable (vir_bytes arch_pagetables, mmap_t *mmap, vm_abstract_pagetables_t *apt, vm_abstract_pt_t *kerntable,
                             endpoint_t proc, phys_bytes *root_phys_out, uint32_t *handle_out) {
    arm_pt_t *pagetables = (arm_pt_t *) arch_pagetables;
    for (int i = 0; i < ARM_MAX_PT_HANDLES; i++) {
        if (pagetables[i].status == PT_FREE) {
            pagetables[i].status = PT_USED;
            pagetables[i].proc_ep = proc;
            vm_arch_alloc_l1_table(mmap, apt, kerntable, &pagetables[i]);
            vm_arch_alloc_l2_table(mmap, apt, kerntable, &pagetables[i]);
            *root_phys_out = pagetables[i].l1_phys;
            *handle_out = (uint32_t) i;
            return OK;
        }
    }
    return ENOMEM;
}

/*
 * Освободить таблицу памяти
 */
int vm_arch_free_pagetable(vir_bytes arch_pagetables, mmap_t *mmap, vm_abstract_pagetables_t *apt, vm_abstract_pt_t *kerntable, uint32_t handler) {
    arm_pt_t *pagetables = (arm_pt_t *) arch_pagetables;
    if (handler >= ARM_MAX_PT_HANDLES) {
        return EINVAL;
    }
    if (pagetables[handler].status == PT_FREE) {
        return OK;
    }
    pagetables[handler].proc_ep = 0;
    pagetables[handler].status = PT_FREE;
    apt_unmap_region(apt, kerntable, pagetables[handler].l1_table_region);
    apt_unmap_region(apt, kerntable, pagetables[handler].l2_tables_region);
    mmap_free_memory(mmap, pagetables[handler].l1_table_region->start, pagetables[handler].l1_table_region->size);
    mmap_free_memory(mmap, pagetables[handler].l2_tables_region->start, pagetables[handler].l2_tables_region->size);
    pagetables[handler].l1_table_region = (mmap_region_t *) 0;
    pagetables[handler].l2_tables_region = (mmap_region_t *) 0;
    pagetables[handler].l1_phys = 0;
    pagetables[handler].l1_table = (uint32_t *) 0;
    pagetables[handler].l2_phys = 0;
    pagetables[handler].l2_tables = (uint32_t *) 0;
    return OK;
}


/*
 * Преобразование флагов и режимов кеширования для секции L1
 */
uint32_t vm_arch_flags_to_l1(vm_apt_flags_t flags, mmap_cache_hint_t cache) {
    uint32_t res = ARM_L1_S;
    // Сначала проверим наши комбо
    if (flags & VM_APF_KERNEL) {
        res |= ARM_L1_AP_KRW_UNO | ~ARM_L1_XN | ~ARM_L1_PXN | ARM_L1_WRITE_BACK_WA;
        return res;
    } else if (flags & VM_APF_DEVICE) {
        res |= ARM_L1_AP_KRW_URW | ARM_L1_XN | ARM_L1_PXN | ARM_L1_DEVICE;
        return res;
    } else if (flags & VM_APF_DMA) {
        res |= ARM_L1_AP_KRW_URW | ARM_L1_XN | ARM_L1_PXN | ARM_L1_UNCACHED;
        return res;
    }

    /* Not Global — у пользовательских процессов */
    if (flags & VM_APF_USER)
        res |= ARM_L1_nG;

    /* Права доступа → AP[2:1:0] */
    int user  = (flags & VM_APF_USER)  ? 1 : 0;
    int write = (flags & VM_APF_WRITE) ? 1 : 0;
    int read  = (flags & VM_APF_READ)  ? 1 : 0;

    if (!read && !write) {
        /* нет доступа — fault при любом обращении */
        res |= ARM_L1_AP_NONE;
    } else if (user && write) {
        res |= ARM_L1_AP_KRW_URW;   /* kernel и user RW */
    } else if (user && !write) {
        res |= ARM_L1_AP_KRO_URO;   /* kernel и user RO */
    } else if (!user && write) {
        res |= ARM_L1_AP_KRW_UNO;   /* только kernel RW */
    } else {
        res |= ARM_L1_AP_KRO_UNO;   /* только kernel RO */
    }

    /* Execute Never */
    if (!(flags & VM_APF_EXEC))
        res |= ARM_L1_XN;

    switch (cache) {
        case MMAP_CACHE_NO:
        case MMAP_CACHE_DMA:
            res |= ARM_L1_UNCACHED;
            break;
        case MMAP_CACHE_WRITECOMB:
            res |= ARM_L1_WRITE_BACK;
            break;
        case MMAP_CACHE_NORMAL:
        case MMAP_CACHE_WRITETHROUGH:
        default:
            res |= ARM_L1_WRITE_THROUGH;
            break;
    }

    return res;
}

/*
 * Преобразование флагов и режимов кеширования для для страницы L2
 */
uint32_t vm_arch_flags_to_l2(vm_apt_flags_t flags, mmap_cache_hint_t cache) {
    uint32_t res = ARM_L2_S;
    // Сначала проверим наши комбо
    if (flags & VM_APF_KERNEL) {
        res |= ARM_L2_AP_KRW_UNO | ~ARM_L2_XN  | ARM_L2_WRITE_BACK_WA;
        return res;
    } else if (flags & VM_APF_DEVICE) {
        res |= ARM_L2_AP_KRW_URW | ARM_L2_XN | ARM_L2_DEVICE;
        return res;
    } else if (flags & VM_APF_DMA) {
        res |= ARM_L2_AP_KRW_URW | ARM_L2_XN |  ARM_L2_UNCACHED;
        return res;
    }

    /* Not Global — у пользовательских процессов */
    if (flags & VM_APF_USER)
        res |= ARM_L2_nG;

    /* Права доступа → AP[2:1:0] */
    int user  = (flags & VM_APF_USER)  ? 1 : 0;
    int write = (flags & VM_APF_WRITE) ? 1 : 0;
    int read  = (flags & VM_APF_READ)  ? 1 : 0;

    if (!read && !write) {
        /* нет доступа — fault при любом обращении */
        res |= ARM_L2_AP_NONE;
    } else if (user && write) {
        res |= ARM_L2_AP_KRW_URW;   /* kernel и user RW */
    } else if (user && !write) {
        res |= ARM_L2_AP_KRO_URO;   /* kernel и user RO */
    } else if (!user && write) {
        res |= ARM_L2_AP_KRW_UNO;   /* только kernel RW */
    } else {
        res |= ARM_L2_AP_KRO_UNO;   /* только kernel RO */
    }

    /* Execute Never */
    if (!(flags & VM_APF_EXEC))
        res |= ARM_L2_XN;

    switch (cache) {
        case MMAP_CACHE_NO:
        case MMAP_CACHE_DMA:
            res |= ARM_L2_UNCACHED;
            break;
        case MMAP_CACHE_WRITECOMB:
            res |= ARM_L2_WRITE_BACK;
            break;
        case MMAP_CACHE_NORMAL:
        case MMAP_CACHE_WRITETHROUGH:
        default:
            res |= ARM_L2_WRITE_THROUGH;
            break;
    }

    return res;
}

/*
 * Флаги для секции описывающей таблицу страниц второго уровня
 */
uint32_t vm_arch_flags_to_l2pt (vm_apt_flags_t flags, mmap_cache_hint_t cache) {
    uint32_t res = 0;
    if (!(flags & VM_APF_KERNEL)) {
      //  res |= ARM_L2PT_PXN;
    }

    return res;
}

/*
 * Заделка на будущее: изменения в таблице по дельте
 */
int vm_arch_pt_apply(vir_bytes arch_pagetables, vm_pt_change_t changes) {
    return OK;
}

/*
 * Сердце нашего механизма абстрактных таблиц
 * преобразованию абстрактной таблицы в физическую
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

    for (l1_iter = table->first_entry; l1_iter != 0; l1_iter = (vm_abstract_pt_l1_entry_t *)l1_iter->next) {
        if (l1_iter->type == VM_APT_L1_L2PT) {
            // Таблица страниц второго уровня
            int pde = ARM_L1_INDEX(l1_iter->vaddr);
            uint32_t *l2pt = &pagetables[handler].l2_tables[pde];
            pagetables[handler].l1_table[pde] = ((uint32_t) pagetables[handler].l2_phys + sizeof(uint32_t) * ARM_L2_ENTRIES) & ARM_L2PT_ADDR_MASK;
            pagetables[handler].l1_table[pde] |= ARM_L1_TYPE_L2PT;
            pagetables[handler].l1_table[pde] |= ARM_L1_DOMAIN(0);
            pagetables[handler].l1_table[pde] |= vm_arch_flags_to_l2pt(l1_iter->flags, l1_iter->cache_hint);
            for (l2_iter = l1_iter->first_l2_entry; l2_iter != 0; l2_iter = (vm_abstract_pt_l2_entry_t *) l2_iter->next) {
                for (uint32_t vaddr = l2_iter->vaddr; vaddr < l2_iter->vaddr + l2_iter->size; vaddr += ARM_L2_SIZE) {
                    if (l2_iter->flags & VM_APF_VIRTUAL_ONLY) {
                        l2pt[ARM_L2_INDEX(vaddr)] = 0;
                    } else {
                        l2pt[ARM_L2_INDEX(vaddr)] = ((uint32_t)
                        l2_iter->paddr + (vaddr - l2_iter->vaddr)) &ARM_L2_ADDR_MASK;
                        l2pt[ARM_L2_INDEX(vaddr)] |= vm_arch_flags_to_l2(l2_iter->flags, l2_iter->cache_hint);
                    }
                 }
            }
        } else {
            // Обычная секция
            for (uint32_t vaddr = l1_iter->vaddr; vaddr < l1_iter->vaddr + l1_iter->size; vaddr += ARM_L1_SIZE) {
                if (l1_iter->flags & VM_APF_VIRTUAL_ONLY) {
                    pagetables[handler].l1_table[ARM_L1_INDEX(vaddr)] = 0;
                } else {
                    pagetables[handler].l1_table[ARM_L1_INDEX(vaddr)] = ((uint32_t) l1_iter->paddr + (vaddr - l1_iter->vaddr)) & ARM_L1_ADDR_MASK;
                    pagetables[handler].l1_table[ARM_L1_INDEX(vaddr)] |= ARM_L1_TYPE_SECTION;
                    pagetables[handler].l1_table[ARM_L1_INDEX(vaddr)] |= ARM_L1_DOMAIN(0);
                    pagetables[handler].l1_table[ARM_L1_INDEX(vaddr)] |= vm_arch_flags_to_l1(l1_iter->flags, l1_iter->cache_hint);
                }
            }
        }
    }

    return OK;
}
