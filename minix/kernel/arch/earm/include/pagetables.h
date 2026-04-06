//
// Created by dmironov on 25.03.2026.
//

#ifndef REMINIX_PAGETABLES_H
#define REMINIX_PAGETABLES_H

#include "cpufunc.h"

/* ============================================================
 * ARM Short-Descriptor Page Table битовые поля
 * (только в этом файле — больше нигде в системе)
 * ============================================================ */

/* L1 дескрипторы */
#define ARM_L1_TYPE_FAULT   0x00    /* не используется              */
#define ARM_L1_TYPE_PAGE    0x01    /* ссылка на L2 таблицу         */
#define ARM_L1_TYPE_SECTION 0x02    /* 1MB секция                   */

/* L1 флаги для ссылки на L2 */
#define ARM_L1_PXN          (1u <<  2)  /* privileged execute never     */
#define ARM_L1_NS           (1u <<  3)  /* non-secure                   */
#define ARM_L1_DOMAIN(d)    ((d) <<  5) /* domain (обычно 0)            */
#define ARM_L1_P            (1u <<  9)  /* parity/ECC                   */

/* L2 дескрипторы (small page = 4KB) */
#define ARM_L2_TYPE_FAULT   0x00
#define ARM_L2_TYPE_LARGE   0x01    /* 64KB large page               */
#define ARM_L2_TYPE_SMALL   0x02    /* 4KB small page                */

/* L2 флаги для small page */
#define ARM_L2_B            (1u <<  2)  /* Bufferable                   */
#define ARM_L2_C            (1u <<  3)  /* Cacheable                    */
#define ARM_L2_AP0          (1u <<  4)  /* Access Permission bit 0      */
#define ARM_L2_AP1          (1u <<  5)  /* Access Permission bit 1      */
#define ARM_L2_TEX(t)       ((t) <<  6) /* Type Extension [2:0]         */
#define ARM_L2_AP2          (1u <<  9)  /* Access Permission bit 2      */
#define ARM_L2_S            (1u << 10)  /* Shareable                    */
#define ARM_L2_NG           (1u << 11)  /* Not Global (process-specific)*/
#define ARM_L2_XN           (1u << 0)   /* Execute Never                */

/* Access Permission комбинации (AP[2:1:0]) */
#define ARM_AP_NONE         0x0     /* нет доступа                   */
#define ARM_AP_KRW_UNO      0x1     /* kernel RW, user none          */
#define ARM_AP_KRW_URO      0x2     /* kernel RW, user RO            */
#define ARM_AP_KRW_URW      0x3     /* kernel RW, user RW            */
#define ARM_AP_KRO_UNO      0x5     /* kernel RO, user none          */
#define ARM_AP_KRO_URO      0x7     /* kernel RO, user RO            */

/* Memory attributes (TEX + C + B) — стандартная MAIR таблица ARM */
#define ARM_ATTR_STRONGLY_ORDERED 0x00  /* TEX=0 C=0 B=0: MMIO регистры  */
#define ARM_ATTR_DEVICE           0x01  /* TEX=0 C=0 B=1: Device memory  */
#define ARM_ATTR_WRITE_THROUGH    0x02  /* TEX=0 C=1 B=0: Normal WT      */
#define ARM_ATTR_WRITE_BACK       0x03  /* TEX=0 C=1 B=1: Normal WB      */
#define ARM_ATTR_WRITE_BACK_WA    0x07  /* TEX=1 C=1 B=1: Normal WB+WA   */
#define ARM_ATTR_UNCACHED         0x08  /* TEX=1 C=0 B=0: Normal uncached */

/* Маски адресов */
#define ARM_L1_ADDR_MASK    0xFFFFFC00  /* биты [31:10] → адрес L2 таблицы  */
#define ARM_L2_ADDR_MASK    0xFFFFF000  /* биты [31:12] → адрес физ. страницы */

#define ARM_L1_INDEX(va)    ((va) >> 20)          /* биты [31:20] */
#define ARM_L2_INDEX(va)    (((va) >> 12) & 0xFF) /* биты [19:12] */

#define ARM_L1_SIZE         (4096 * 4)  /* 16KB — одна L1 таблица */
#define ARM_L2_SIZE         (256  * 4)  /* 1KB  — одна L2 таблица */
#define ARM_L1_ENTRIES      4096
#define ARM_L2_ENTRIES      256


/* ============================================================
 * Структура физической таблицы страниц (хранится в ядре)
 * ============================================================ */

typedef enum {
    PT_FREE = 0,
    PT_USED  = 1
} arm_pt_status_t;

typedef struct {
    arm_pt_status_t status;
    endpoint_t  proc_ep;

    /* L1 таблица (Page Directory) */
    uint32_t   *l1_table;       /* виртуальный адрес для доступа ядра       */
    phys_bytes  l1_phys;        /* физический адрес — для TTBR0             */

    /* L2 таблицы (Page Tables) — по одной на каждую используемую секцию */
    uint32_t   *l2_tables; /* виртуальные адреса            */
    phys_bytes  l2_phys;   /* физические адреса             */
} arm_pt_t;

void pg_load_ttbr1(arm_pt_t *pagedir);
void pg_load_ttbr0(arm_pt_t *pagedir);
void vm_enable_paging(void);
vir_bytes map_mmap_region_to_pt_l1_from_end (uint32_t *l1_phys, mmap_region_t *region, vir_bytes *start, uint32_t flags);
vir_bytes map_mmap_region_to_pt_l1 (uint32_t *l1_phys, mmap_region_t *region, vir_bytes *start, uint32_t flags);
vir_bytes map_mmap_region_to_kernel_pt_l1 (uint32_t *l1_phys, mmap_region_t *region, vir_bytes *start, uint32_t flags);

#endif //REMINIX_PAGETABLES_H
