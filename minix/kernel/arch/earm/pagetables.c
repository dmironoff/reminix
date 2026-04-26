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

extern struct kinfo kinfo;

#define ARM_PD_ALIGN    16384
#define ARM_PD_SIZE     16384
#define ARM_PT_SIZE     1024

/*
 * вычисление оффсета для выравнивая l1 относительно выделенного региона
 */
inline uint32_t pdoff(uint32_t sector_start) {
    return (sector_start + (ARM_PD_ALIGN - 1)) & ~(ARM_PD_ALIGN - 1) - sector_start;
}

/*
 * Вычисление оффсета начала таблицы l2 относительно начала выделенного региона
 */
inline uint32_t ptoff(uint32_t sector_start) {
    return ptaddr(sector_start) + ARM_PD_SIZE;
}


/*
 * "Монтирование" таблицы страниц для работы с ними
 */
void map_pt_table_to_me (uint32_t handler) {
    struct proc *ptproc = get_cpulocal_var(ptproc);
    mmap_region_t *my_pd_reg = (mmap_region_t *) ((arm_pt_t *) kinfo->arch_pagetables)[ptproc->pt_handler].table_region;
    mmap_region_t *need_pd_reg = (mmap_region_t *) ((arm_pt_t *) kinfo->arch_pagetables)[handler].table_region;
    uint32_t *pd = (uint32_t *) (kinfo->vir_memory_pt_region_addr + pdoff(my_pd_reg->start));
    for (int pde = ARM_L1_INDEX(kinfo->vir_memory_pt_work_region_addr); pde <= ARM_L1_INDEX(kinfo->vir_memory_pt_work_region_addr + kinfo->vir_memory_pt_work_region_size); pde++) {
        pd[pde] = (need_pd_reg->start + (pde * ARM_L1_SIZE - kinfo->vir_memory_pt_work_region_addr) & ARM_L1_ADDR_MASK)
                  | ARM_L1_TYPE_SECTION
                  | ARM_L1_DOMAIN(0)
                  | ARM_L1_WRITE_THROUGH
                  | ARM_L2_AP_KRW_UNO;
        tlbi_mva_asid_is(pde * ARM_L1_SIZE, get_cpulocal_var(ptproc)->context_id.id);
    }
}

/*
 * Освобождаем сектора в которые мы "монтировали" рабочую директорию страниц
 */
void unmap_pt_table_to_me () {
    struct proc *ptproc = get_cpulocal_var(ptproc);
    mmap_region_t *my_pd_reg = (mmap_region_t *) ((arm_pt_t *) kinfo->arch_pagetables)[ptproc->pt_handler].table_region;
    uint32_t *pd = (uint32_t *) (kinfo->vir_memory_pt_region_addr + pdoff(my_pd_reg->start));
    for (int pde = ARM_L1_INDEX(kinfo->vir_memory_pt_work_region_addr); pde <= ARM_L1_INDEX(kinfo->vir_memory_pt_work_region_addr + kinfo->vir_memory_pt_work_region_size); pde++) {
        pd[pde] = 0;
        tlbi_mva_asid_is(pde * ARM_L1_SIZE, get_cpulocal_var(ptproc)->context_id.id);
    }
}


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
 * Выделить новую таблицу страниц из пула
 * Возвращает через указатели адрес начала для загрузки в регистр и хэндлер для использования в функциях
 */
int vm_arch_alloc_pagetable (vir_bytes arch_pagetables, mmap_t *mmap, endpoint_t proc, uint32_t *handle_out) {
    arm_pt_t *pagetables = (arm_pt_t *) arch_pagetables;
    for (int i = 0; i < ARM_MAX_PT_HANDLES; i++) {
        if (pagetables[i].status == PT_FREE) {
            pagetables[i].status = PT_USED;
            pagetables[i].proc_ep = proc;
            mmap_alloc_highest_region(mmap, ARM_L1_SIZE,  pagetables[i].table_region);
            pagetables[i].next_pte = 0;
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
    mmap_free_memory(mmap, pagetables[handler].table_region->start, pagetables[handler].table_region->size);
    pagetables[handler].table_region = 0;
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

/*TODO: Перепроверить функцию трансляции APT в PT*/

/*
 * Сердце нашего механизма абстрактных таблиц
 * преобразованию абстрактной таблицы в физическую
 */
int vm_arch_apt_to_pt(vm_abstract_pagetables_t *apt, vm_abstract_pt_t *table, vir_bytes arch_pagetables, uint32_t handler) {
    arm_pt_t *pagetable = &((arm_pt_t *) arch_pagetables)[handler];
    vm_abstract_pt_entry_t *iter;

    if (handler >= ARM_MAX_PT_HANDLES) {
        return EINVAL;
    }
    if (pagetables[handler].status == PT_FREE) {
        return EINVAL;
    }
    if (table->status == VM_RECORD_UNDEF) {
        return EINVAL;
    }


    map_pt_table_to_me(handler);
    pagetable->next_pte = 0;

    // Затираем текущие данные в редактируемой таблице страниц
    memset((void *)kinfo->vir_memory_pt_work_region_addr, 0, kinfo->vir_memory_pt_work_region_size);

    uint32_t *pd = (uint32_t *) (kinfo->vir_memory_pt_work_region_addr + pdoff(pagetable->table_region->start));

    for (iter = table->first; iter != 0; iter = (vm_abstract_pt_entry_t *) iter->next) {
        // Размеры l2 страниц по концам региона
        vir_bytes on_start_l2 = iter->vaddr % ARM_L1_SIZE;
        vir_bytes on_end_l2 = (iter->vaddr + iter->size) % ARM_L1_SIZE;

        // Начало и конец региона который можно разметить как l1
        vir_bytes start_l1_addr = 0;
        vir_bytes end_l1_addr = 0;
        if (iter->size / ARM_L1_SIZE > 0) {
            vir_bytes start_l1_addr = iter->vaddr + ARM_L1_SIZE - on_start_l2;
            vir_bytes end_l1_addr = iter->vaddr + iter->size - ARM_L1_SIZE - on_end_l2;
        }

        if (on_start_l2) {
            for (int pte = ARM_L2_INDEX(iter->vaddr); pte <= ARM_L2_INDEX(iter->vaddr + on_start_l2); pte++) {
                uint32_t *pt = (uint32_t *)(kinfo.vir_memory_pt_work_region_addr + ptoff(pagetable->table_region->start));
                if (iter->flags & VM_APF_VIRTUAL_ONLY || iter->paddr == 0) {
                    pt[pagetable->next_pte + pte] = 0;
                } else {
                    pt[pagetable->next_pte + pte] = (iter->paddr + (pte * ARM_L2_SIZE - iter->vaddr)) & ARM_L2_ADDR_MASK;
                    pt[pagetable->next_pte + pte] |= vm_arch_flags_to_l2(iter->flags, iter->cache);
                }
            }
        }

        for (int pde = ARM_L1_INDEX(start_l1_addr); pde <= ARM_L1_INDEX(end_l1_addr); pde++) {
            if (iter->flags & VM_APF_VIRTUAL_ONLY || iter->paddr == 0) {
                pd[pde] = 0;
            } else {
                pd[pde] = (uint32_t) (iter->paddr + (pde * ARM_L1_SIZE - iter->vaddr)) & ARM_L1_ADDR_MASK;
                pd[pde] |= ARM_L1_TYPE_SECTION;
                pd[pde] |= ARM_L1_DOMAIN(0);
                pd[pde] |= vm_arch_flags_to_l1(iter->flags, iter->cache);
            }
        }

        if (on_end_l2) {
            pagetable->next_pte++;
            pd[ARM_L1_INDEX(iter->vaddr + iter->size - on_end_l2)] = (uint32_t)
                    (pagetable->table_region->start + ptoff(pagetable->table_region->start) + sizeof(uint32_t) * ARM_L2_ENTRIES) & ARM_L2PT_ADDR_MASK;
            pd[ARM_L1_INDEX(iter->vaddr + iter->size - on_end_l2)] |= ARM_L1_TYPE_L2PT | ARM_L1_DOMAIN(0) | vm_arch_flags_to_l2pt(iter->flags, iter->cache);
            for (int pte = ARM_L2_INDEX(iter->vaddr + iter->size - on_end_l2); pte <= ARM_L2_INDEX(iter->vaddr + size); pte++) {
                uint32_t *pt = (uint32_t *)(kinfo.vir_memory_pt_work_region_addr + ptoff(pagetable->table_region->start));
                if (iter->flags & VM_APF_VIRTUAL_ONLY || iter->paddr == 0) {
                    pt[pagetable->next_pte + pte] = 0;
                } else {
                    pt[pagetable->next_pte + pte] = (iter->paddr + iter->size - on_end_l2 - (pte * ARM_L2_SIZE)) & ARM_L2_ADDR_MASK;
                    pt[pagetable->next_pte + pte] |= vm_arch_flags_to_l2(iter->flags, iter->cache);
                }
            }
        }
    }

    for (int pde = ARM_L1_INDEX(kinfo.vir_memory_pt_region_addr); pde <= ARM_L1_INDEX(kinfo.vir_memory_pt_region_addr + kinfo.vir_memory_pt_region_size); pde++) {
        pd[pde] = (uint32_t) (pagetable->table_region->start + (pde * ARM_L1_SIZE - kinfo.vir_memory_pt_region_addr) & ARM_L1_ADDR_MASK)
                  | ARM_L1_TYPE_SECTION
                  | ARM_L1_DOMAIN(0)
                  | ARM_L1_WRITE_THROUGH
                  | ARM_L2_AP_KRW_UNO;
    }
    unmap_pt_table_to_me();

    return OK;
}
