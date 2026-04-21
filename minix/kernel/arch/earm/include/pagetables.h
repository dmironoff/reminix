//
// Created by dmironov on 25.03.2026.
//

#ifndef REMINIX_PAGETABLES_H
#define REMINIX_PAGETABLES_H

#include "cpufunc.h"
#include <minix/abstract_pagetables.h>
#include <minix/physmemorymap.h>
#include "kernel/apt_utils.h"
#include "kernel/mmap_utils.h"

/*
 * Конечно, в наших исходных кодах есть куча мест с описанием этих дескрипторов
 * Но вот этот механизм самый новый и использовать я буду только его
 */

/* L1 дескрипторы */
#define ARM_L1_TYPE_FAULT   0x00    /* не используется              */


/*Дескриптор целиковой секции L1*/
#define ARM_L1_TYPE_SECTION 0x02    /* 1MB секция                   */
#define ARM_L1_ADDR_MASK 0xFFF00000 // адрес при описании секции L1
#define ARM_L1_PXN          (1u)     // Не исполнять код в привелигированном режиме
#define ARM_L1_B            (1u << 2)  // Bufferable
#define ARM_L1_C            (1u << 3)  // Cacheble
#define ARM_L1_XN           (1u << 4)  // Execute never
#define ARM_L1_S            (1u << 16) // Shareable for smp
#define ARM_L1_nG          (1u << 17) // Non Global
#define ARM_L1_NS          (1u << 19) // Not secure
#define ARM_L1_AP2          (1u << 15) // 0 Read/Write, 1 - Read only -- используется вместе с AP0 и AP1
#define ARM_L1_AP1          (1u << 11) // Access Permissions 01 Только SVC,  11 User + SVC
#define ARM_L1_AP0          (1u << 10) //
#define ARM_L1_TEX0         (1u << 12) //
#define ARM_L1_TEX1         (1u << 13) //
#define ARM_L1_TEX2         (1u << 14) //
#define ARM_L1_DOMAIN(d)    ((d) <<  5) /* domain (обычно 0)            */
#define ARM_L1_P            (1u <<  9)  /* parity/ECC                   */

/*Права доступа для страниц l1*/
#define ARM_L1_AP_NONE         0            /* нет доступа                   */
#define ARM_L1_AP_KRW_UNO      ARM_L1_AP0     /* kernel RW, user none          */
#define ARM_L1_AP_KRW_URO      ARM_L1_AP1     /* kernel RW, user RO            */
#define ARM_L1_AP_KRW_URW      (ARM_L1_AP0 | ARM_L1_AP1)     /* kernel RW, user RW            */
#define ARM_L1_AP_KRO_UNO      (ARM_L1_AP2 | ARM_L1_AP0)     /* kernel RO, user none          */
#define ARM_L1_AP_KRO_URO      (ARM_L1_AP0 | ARM_L1_AP1 | ARM_L1_AP2)     /* kernel RO, user RO            */

/*Атрибуты памяти для страниц L1*/
#define ARM_L1_STRONGLY_ORDERED 0  /* TEX=0 C=0 B=0: MMIO регистры  */
#define ARM_L1_DEVICE           ARM_L1_B  /* TEX=0 C=0 B=1: Device memory  */
#define ARM_L1_WRITE_THROUGH    ARM_L1_C  /* TEX=0 C=1 B=0: Normal WT      */
#define ARM_L1_WRITE_BACK       (ARM_L1_B | ARM_L1_C)  /* TEX=0 C=1 B=1: Normal WB      */
#define ARM_L1_WRITE_BACK_WA    (ARM_L1_TEX0 | ARM_L1_TEX2 | ARM_L1_B | ARM_L1_C)  /* TEX=1 C=1 B=1: Normal WB+WA   - SVC */
#define ARM_L1_UNCACHED         ARM_L1_TEX1  /* TEX=1 C=0 B=0: Normal uncached - DMA*/

#define ARM_L1_INDEX(va)    ((va) >> 20)          /* биты [31:20] */


/*Дескриптор L2PT - ссылка в l1 на адрес таблицы страниц l2*/
#define ARM_L1_TYPE_L2PT    0x01    /* ссылка на L2 таблицу         */
#define ARM_L2PT_ADDR_MASK    0xFFFFFC00  /* биты [31:10] → адрес L2 таблицы  */
#define ARM_L2PT_NS         (1u << 9) // Non secure
#define ARM_L2PT_P          (1u << 4)
#define ARM_L2PT_PXN        (1u << 2) // Privileged Execute-Never.  1 - запрет для всех страниц таблицы
#define ARM_L2PT_DOMAIN(d)  ((d) <<  5) /* domain (обычно 0)            */

/* Дескриптор страницы l2 */
#define ARM_L2_TYPE_FAULT   0x00
#define ARM_L2_TYPE_LARGE   0x01    /* 64KB large page               */
#define ARM_L2_TYPE_SMALL   0x02    /* 4KB small page - Мы используем именно их               */
#define ARM_L2_ADDR_MASK    0xFFFFF000  /* биты [31:12] → адрес физ. страницы внутри L2 */
#define ARM_L2_XN           (1u << 0)   /* Execute Never                */
#define ARM_L2_B            (1u <<  2)  /* Bufferable                   */
#define ARM_L2_C            (1u <<  3)  /* Cacheable                    */
#define ARM_L2_S            (1u << 10)  /* Shareable                    */
#define ARM_L2_AP0          (1u <<  4)  /* Access Permission bit 0      */
#define ARM_L2_AP1          (1u <<  5)  /* Access Permission bit 1      */
#define ARM_L2_AP2          (1u <<  9)  /* Access Permission bit 2      */
#define ARM_L2_nG           (1u << 11)  /* Not Global (process-specific)*/
#define ARM_L2_TEX0         (1u << 6)
#define ARM_L2_TEX1         (1u << 7)
#define ARM_L2_TEX2         (1u << 8)
#define ARM_L2_INDEX(va)    (((va) >> 12) & 0xFF) /* биты [19:12] */

/*Права доступа для страниц l2*/
#define ARM_L2_AP_NONE         0            /* нет доступа                   */
#define ARM_L2_AP_KRW_UNO      ARM_L2_AP0     /* kernel RW, user none          */
#define ARM_L2_AP_KRW_URO      ARM_L2_AP1     /* kernel RW, user RO            */
#define ARM_L2_AP_KRW_URW      (ARM_L2_AP0 | ARM_L2_AP1)     /* kernel RW, user RW            */
#define ARM_L2_AP_KRO_UNO      (ARM_L2_AP2 | ARM_L2_AP0)     /* kernel RO, user none          */
#define ARM_L2_AP_KRO_URO      (ARM_L2_AP0 | ARM_L2_AP1 | ARM_L2_AP2)     /* kernel RO, user RO            */

/*Атрибуты памяти для страниц L2*/
#define ARM_L2_STRONGLY_ORDERED 0  /* TEX=0 C=0 B=0: MMIO регистры  */
#define ARM_L2_DEVICE           ARM_L2_B  /* TEX=0 C=0 B=1: Device memory  */
#define ARM_L2_WRITE_THROUGH    ARM_L2_C  /* TEX=0 C=1 B=0: Normal WT      */
#define ARM_L2_WRITE_BACK       (ARM_L2_B | ARM_L2_C)  /* TEX=0 C=1 B=1: Normal WB      */
#define ARM_L2_WRITE_BACK_WA    (ARM_L2_TEX0 | ARM_L2_TEX2 | ARM_L2_B | ARM_L2_C)  /* TEX=1 C=1 B=1: Normal WB+WA   - SVC */
#define ARM_L2_UNCACHED         ARM_L2_TEX1  /* TEX=1 C=0 B=0: Normal uncached - DMA*/

/* Размеры и количество страниц */
#define ARM_L1_SIZE         (4096 * 4)  /* 16KB — одна L1 таблица */
#define ARM_L2_SIZE         (256  * 4)  /* 1KB  — одна L2 таблица */
#define ARM_L1_ENTRIES      4096
#define ARM_L2_ENTRIES      256


/* ============================================================
 * Структура физической таблицы страниц (хранится в ядре)
 * ============================================================ */

typedef enum {
    PT_FREE = 0,
    PT_USED  = 1,
} arm_pt_status_t;

typedef struct {
    arm_pt_status_t status;
    endpoint_t  proc_ep;

    /* L1 таблица (Page Directory) */
    uint32_t   *l1_table;       /* виртуальный адрес для доступа ядра       */
    phys_bytes  l1_phys;        /* физический адрес — для TTBR0             */
    mmap_region_t *l1_table_region;

    /* L2 таблицы (Page Tables) — по одной на каждую используемую секцию */
    uint32_t   *l2_tables; /* виртуальные адреса            */
    phys_bytes  l2_phys;   /* физические адреса             */
    mmap_region_t *l2_tables_region;
} arm_pt_t;

/*
 * Включение MMU
 * Полностью архитектурнозависимая функция
 * Перед её выполнением нужно загрузить таблицы страниц в регистры
 */
void vm_arch_enable_paging(void);

/*
 * Загрузка таблицы страниц в регистр ttbr0 - пользовательское пространство
 */
void pg_load_ttbr0(arm_pt_t *pagedir);

/*
 * Выделить память для таблицы страниц l1
 */
int vm_arch_alloc_l1_table(mmap_t *mmap, vm_abstract_pagetables_t *apt, vm_abstract_pt_t *apt_table, arm_pt_t *pt);

/*
 * Выделить память для таблицы страниц l2
 */
int vm_arch_alloc_l2_table(mmap_t *mmap, vm_abstract_pagetables_t *apt, vm_abstract_pt_t *apt_table, arm_pt_t *pt);
/*
 * Выделить новую таблицу страниц из пула
 * Возвращает через указатели адрес начала для загрузки в регистр и хэндлер для использования в функциях
 */
int vm_arch_alloc_pagetable (vir_bytes arch_pagetables, mmap_t *mmap, vm_abstract_pagetables_t *apt, vm_abstract_pt_t *kerntable,
                             endpoint_t proc, phys_bytes *root_phys_out, uint32_t *handle_out);

/*
 * Преобразование флагов и режимов кеширования для секции L1
 */
uint32_t vm_arch_flags_to_l1(vm_apt_flags_t flags, mmap_cache_hint_t cache);
/*
 * Преобразование флагов и режимов кеширования для для страницы L2
 */
uint32_t vm_arch_flags_to_l2(vm_apt_flags_t flags, mmap_cache_hint_t cache);

/*
 * Флаги для секции описывающей таблицу страниц второго уровня
 */
uint32_t vm_arch_flags_to_l2pt (vm_apt_flags_t flags, mmap_cache_hint_t cache);
/*
 * Заделка на будущее: изменения в таблице по дельте
 */
int vm_arch_pt_apply(vir_bytes arch_pagetables, vm_pt_change_t changes);
/*
 * Сердце нашего механизма абстрактных таблиц
 * преобразованию абстрактной таблицы в физическую
 */
int vm_arch_apt_to_pt(vm_abstract_pagetables_t *apt, vm_abstract_pt_t *table, vir_bytes arch_pagetables, uint32_t handler);

void vm_arch_tlb_shoot() {

}

#endif //REMINIX_PAGETABLES_H
