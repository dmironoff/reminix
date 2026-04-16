/*
 * Источники информации:
[1] ARM Cortex-A7 MPCore Technical Reference Manual — описание команд MVA.
[2] ARM Architecture Reference Manual ARMv7-A — правила работы DCCMVAC.
[3] Linux-Sunxi: Allwinner H3 Architecture — работа с памятью в SoC Allwinner.
[4] ARM CoreLink GIC-400 Technical Reference Manual — вопросы когерентности.
[5] ARMv7-A TLB Maintenance — необходимость инвалидации TLB.
 */

/*
 * Все функции в этом файле используются только для инициализации системы
 * Дальше действует связка - mmap -> apt -> pagetables.c
 * А сейчас нам нужен набор утилит для работы с чистой таблицей страниц архитектуры
 * Что бы разместить в памяти все наши структуры данных при инициализации
 */

#include <minix/cpufeature.h>

#include <minix/type.h>
#include <assert.h>
#include "kernel/kernel.h"
#include "arch_proto.h"
#include <machine/cpu.h>
#include <arm/armreg.h>

#include <string.h>
#include <minix/type.h>

#include "bsp_serial.h"
#include <minix/physmemorymap.h>
#include "kernel/mmap_utils.h"

#include "pagetables.h"
#include "arch_configs.h"

extern void ser_print_variable(const char name, uint32_t value);
extern void pre_panic(const char message);

extern uint32_t _kern_vir_base;

/*
 * Временные таблицы страниц для включения mmu
 * Мы не боимся что в очередной раз разметим слишком много, так как после конца инициализации
 * Весь bootstrap будет освобождён в памяти и размечен как свободная оперативная память
 */
static uint32_t pagetable[ARM_L1_PAGES] __aligned(16384); // Таблица секций L1
static uint32_t l2pull[BOOTSTRAP_L2_PULL_SIZE][ARM_L2_ENTRIES] __aligned(1024);
static int used_l2 = 0;


/*
 * ФУНКЦИИ РАБОТЫ С ФИЗИЧЕСКОЙ ТАБЛИЦЕЙ СТРАНИЦ ДЛЯ ИНИЦИАЛИЗАЦИИ СИСТЕМЫ
 */

/*
 * Возвращает физический адрес инициализационной таблицы страниц
 */
phys_bytes pg_get_phys_addr(void) {
    return (phys_bytes) pagetable;
}

/*
 * Разметить регион с памятью устройств 1 к 1
 */
void pg_map_device_region(mmap_region_t *region) {
    phys_bytes addr;
    // Здесь мы действуем из предположения что регионы памяти устройств выровнены по 1 мб
    for (addr = region->start; addr < region->start + region->size; addr += ARM_L1_SIZE) {
        int pde = ARM_L1_INDEX(addr);
        pagetable[pde] = addr & ARM_L1_ADDR_MASK;
        pagetable[pde] |= ARM_L1_TYPE_SECTION;
        pagetable[pde] |= ARM_L1_DEVICE;
        pagetable[pde] |= ARM_L1_DOMAIN(0);
        pagetable[pde] |= ARM_L1_AP_KRW_URW;
        pagetable[pde] |= ARM_L1_S;
        pagetable[pde] |= ARM_L1_XN;
        pagetable[pde] |= ARM_L1_PXN;
        pagetable[pde] |= ARM_L1_nG;
    }
}

/*
 * Разметить обычный регион 1 к 1
 * При инициализации мы размечаем всю память как исполняему в привелигированном режиме
 */
void pg_map_mem_region(mmap_region_t *region) {
    phys_bytes addr;
    phys_bytes size;
    phys_bytes start_l2;
    phys_bytes end_l2;
    int pte, pde;

    addr = region->start;
    size = region->size;

    // Мы предполагаем, что наши регионы памяти могут быть не быть равны размеру секции l1
    start_l2 = addr % ARM_L1_SIZE;
    end_l2 = (addr + size) % ARM_L1_SIZE;

    if (start_l2 > 0) {
        pde = (addr - start_l2) / ARM_L1_SIZE;
        phys_bytes l2_addr;
        if (pagetable[pde] == 0) {
            // У нас не существует ещё записи о таблице страниц l2
            l2_addr = (phys_bytes) &l2pull[used_l2++][0];
            pagetable[pde] = l2_addr & ARM_L2PT_ADDR_MASK;
            pagetable[pde] |= ARM_L2PT_DOMAIN(0);
            pagetable[pde] |= ARM_L1_TYPE_L2PT;
        } else {
            l2_addr = pagetable[pde] & (~ARM_L2PT_ADDR_MASK);
        }
        for (;addr < region->start - start_l2 + ARM_L1_SIZE; addr += ARM_L2_SIZE) {
            pte = ARM_L2_INDEX(addr);
            uint32_t *l2page = (uint32_t *) l2_addr;
            l2page[pte] = addr & ARM_L2_ADDR_MASK;
            l2page[pte] |= ARM_L2_TYPE_SMALL;
            l2page[pte] |= ARM_L2_AP_KRW_URW;
            l2page[pte] |= ARM_L2_WRITE_BACK; // на время инициализации у нас ничего не кешируется
            l2page[pte] |= ARM_L2_S;
            l2page[pte] |= (~ARM_L2_nG); // все страницы у нас поначалу Global
            l2page[pte] |= (~ARM_L2_XN); // мы разрешаем выполнять всю память на время инициализации
        }
        size -= start_l2;
    }

    if (end_l2 > 0) {
        pde = (addr + size - end_l2) / ARM_L1_SIZE;
        phys_bytes l2_addr;
        if (pagetable[pde] == 0) {
            // У нас не существует ещё записи о таблице страниц l2
            l2_addr = (phys_bytes) &l2pull[used_l2++][0];
            pagetable[pde] = l2_addr & ARM_L2PT_ADDR_MASK;
            pagetable[pde] |= ARM_L2PT_DOMAIN(0);
            pagetable[pde] |= ARM_L1_TYPE_L2PT;
        } else {
            l2_addr = pagetable[pde] & (~ARM_L2PT_ADDR_MASK);
        }
        for (phys_bytes end_addr = region->start + region->size; end_addr > addr + size - end_l2; end_addr -= ARM_L2_SIZE) {
            pte = ARM_L2_INDEX(end_addr);
            uint32_t *l2page = (uint32_t *) l2_addr;
            l2page[pte] = end_addr & ARM_L2_ADDR_MASK;
            l2page[pte] |= ARM_L2_TYPE_SMALL;
            l2page[pte] |= ARM_L2_AP_KRW_URW;
            l2page[pte] |= ARM_L2_WRITE_BACK; // на время инициализации у нас ничего не кешируется
            l2page[pte] |= ARM_L2_S;
            l2page[pte] |= (~ARM_L2_nG); // все страницы у нас поначалу Global
            l2page[pte] |= (~ARM_L2_XN); // мы разрешаем выполнять всю память на время инициализации
        }
        size -= end_l2;
    }

    for (phys_bytes iter_addr = addr; iter_addr < addr + size; iter_addr += ARM_L1_SIZE) {
        pde = ARM_L1_INDEX(iter_addr);
        pagetable[pde] = iter_addr & ARM_L1_ADDR_MASK;
        pagetable[pde] |= ARM_L1_TYPE_SECTION;
        pagetable[pde] |= ARM_L1_WRITE_BACK;
        pagetable[pde] |= ARM_L1_DOMAIN(0);
        pagetable[pde] |= ARM_L1_AP_KRW_URW;
        pagetable[pde] |= ARM_L1_S;
    }
}

/*
 * Разметить виртуальный регион с исполняемым кодом ядра
 * Нам это нужно исключительно что бы когда мы будем пихать в свободную виртуальную память
 * наши структуры данных мы уже имели там размапленное ядро и не перезаписали его случайно
 */
void pg_map_kern_region (mmap_region_t *region) {
    // Унас ядро выровнено по границе 1мб, но для будущих поколений мы всёравно сделаем возможность разметки для 4кб страниц
    phys_bytes phys_addr;
    vir_bytes  vir_addr = (vir_bytes) _kern_vir_base;
    phys_bytes size;
    phys_bytes start_l2;
    phys_bytes end_l2;
    int pte, pde;

    phys_addr = region->start;
    size = region->size;

    // Мы предполагаем, что наши регионы памяти могут быть не быть равны размеру секции l1
    start_l2 = _kern_vir_base % ARM_L1_SIZE;
    end_l2 = (_kern_vir_base + size) % ARM_L1_SIZE;

    if (start_l2 > 0) {
        pde = (vir_addr - start_l2) / ARM_L1_SIZE;
        phys_bytes l2_addr;
        if (pagetable[pde] == 0) {
            // У нас не существует ещё записи о таблице страниц l2
            l2_addr = (phys_bytes) &l2pull[used_l2++][0];
            pagetable[pde] = l2_addr & ARM_L2PT_ADDR_MASK;
            pagetable[pde] |= ARM_L2PT_DOMAIN(0);
            pagetable[pde] |= ARM_L1_TYPE_L2PT;
        } else {
            l2_addr = pagetable[pde] & (~ARM_L2PT_ADDR_MASK);
        }
        for (;vir_addr < _kern_vir_base - start_l2 + ARM_L1_SIZE; vir_addr += ARM_L2_SIZE) {
            pte = ARM_L2_INDEX(vir_addr);
            uint32_t *l2page = (uint32_t *) l2_addr;
            l2page[pte] = phys_addr & ARM_L2_ADDR_MASK;
            l2page[pte] |= ARM_L2_TYPE_SMALL;
            l2page[pte] |= ARM_L2_AP_KRW_URW;
            l2page[pte] |= ARM_L2_WRITE_BACK; // на время инициализации у нас ничего не кешируется
            l2page[pte] |= ARM_L2_S;
            phys_addr += ARM_L2_SIZE;
        }
        size -= start_l2;
    }

    if (end_l2 > 0) {
        pde = (vir_addr + size - end_l2) / ARM_L1_SIZE;
        phys_bytes l2_addr;
        if (pagetable[pde] == 0) {
            // У нас не существует ещё записи о таблице страниц l2
            l2_addr = (phys_bytes) &l2pull[used_l2++][0];
            pagetable[pde] = l2_addr & ARM_L2PT_ADDR_MASK;
            pagetable[pde] |= ARM_L2PT_DOMAIN(0);
            pagetable[pde] |= ARM_L1_TYPE_L2PT;
        } else {
            l2_addr = pagetable[pde] & (~ARM_L2PT_ADDR_MASK);
        }
        for (vir_bytes end_addr = _kern_vir_base + region->size; end_addr > _kern_vir_base + region->size - end_l2; end_addr -= ARM_L2_SIZE) {
            pte = ARM_L2_INDEX(end_addr);
            uint32_t *l2page = (uint32_t *) l2_addr;
            l2page[pte] = (phys_addr + size) & ARM_L2_ADDR_MASK;
            l2page[pte] |= ARM_L2_TYPE_SMALL;
            l2page[pte] |= ARM_L2_AP_KRW_URW;
            l2page[pte] |= ARM_L2_WRITE_BACK; // на время инициализации у нас ничего не кешируется
            l2page[pte] |= ARM_L2_S;
            size -= ARM_L2_SIZE;
        }
    }

    for (phys_bytes iter_addr = vir_addr; iter_addr < vir_addr + size; iter_addr += ARM_L1_SIZE) {
        pde = ARM_L1_INDEX(iter_addr);
        pagetable[pde] = phys_addr & ARM_L1_ADDR_MASK;
        pagetable[pde] |= ARM_L1_TYPE_SECTION;
        pagetable[pde] |= ARM_L1_WRITE_BACK;
        pagetable[pde] |= ARM_L1_DOMAIN(0);
        pagetable[pde] |= ARM_L1_AP_KRW_URW;
        pagetable[pde] |= ARM_L1_S;
        phys_addr += ARM_L1_SIZE;
    }
}

/*
 * Инициализация страниц виртуальной памяти с разметкой 1 к 1
 */
void pg_init(mmap_t *mmap) {
    mmap_region_t *iter;

    for (iter = mmap->first_region; iter != 0; iter = (mmap_region_t *) iter->next) {
       switch (iter->type) {
           MMAP_DEVICE:
               pg_map_device_region(iter);
               break;
           MMAP_KERNEL:
               pg_map_kern_region(iter);
               break;
           default:
               pg_map_mem_region(iter);
               break;
       }
    }
}

/*
 * Размапливание региона по определённому виртуальному адресу
 * Не проверяет на свободное место, просто вхуючивает.
 */
void pg_map_region_to_vir (mmap_region_t *region, vir_bytes base) {
    // Унас ядро выровнено по границе 1мб, но для будущих поколений мы всёравно сделаем возможность разметки для 4кб страниц
    phys_bytes phys_addr;
    vir_bytes  vir_addr = (vir_bytes) base;
    phys_bytes size;
    phys_bytes start_l2;
    phys_bytes end_l2;
    int pte, pde;

    phys_addr = region->start;
    size = region->size;

    // Мы предполагаем, что наши регионы памяти могут быть не быть равны размеру секции l1
    start_l2 = base % ARM_L1_SIZE;
    end_l2 = (base + size) % ARM_L1_SIZE;

    if (start_l2 > 0) {
        pde = (vir_addr - start_l2) / ARM_L1_SIZE;
        phys_bytes l2_addr;
        if (pagetable[pde] == 0) {
            // У нас не существует ещё записи о таблице страниц l2
            l2_addr = (phys_bytes) &l2pull[used_l2++][0];
            pagetable[pde] = l2_addr & ARM_L2PT_ADDR_MASK;
            pagetable[pde] |= ARM_L2PT_DOMAIN(0);
            pagetable[pde] |= ARM_L1_TYPE_L2PT;
        } else {
            l2_addr = pagetable[pde] & (~ARM_L2PT_ADDR_MASK);
        }
        for (;vir_addr < base - start_l2 + ARM_L1_SIZE; vir_addr += ARM_L2_SIZE) {
            pte = ARM_L2_INDEX(vir_addr);
            uint32_t *l2page = (uint32_t *) l2_addr;
            l2page[pte] = phys_addr & ARM_L2_ADDR_MASK;
            l2page[pte] |= ARM_L2_TYPE_SMALL;
            l2page[pte] |= ARM_L2_AP_KRW_URW;
            l2page[pte] |= ARM_L2_WRITE_BACK; // на время инициализации у нас ничего не кешируется
            l2page[pte] |= ARM_L2_S;
            phys_addr += ARM_L2_SIZE;
        }
        size -= start_l2;
    }

    if (end_l2 > 0) {
        pde = (vir_addr + size - end_l2) / ARM_L1_SIZE;
        phys_bytes l2_addr;
        if (pagetable[pde] == 0) {
            // У нас не существует ещё записи о таблице страниц l2
            l2_addr = (phys_bytes) &l2pull[used_l2++][0];
            pagetable[pde] = l2_addr & ARM_L2PT_ADDR_MASK;
            pagetable[pde] |= ARM_L2PT_DOMAIN(0);
            pagetable[pde] |= ARM_L1_TYPE_L2PT;
        } else {
            l2_addr = pagetable[pde] & (~ARM_L2PT_ADDR_MASK);
        }
        for (vir_bytes end_addr = base + region->size; end_addr > base + region->size - end_l2; end_addr -= ARM_L2_SIZE) {
            pte = ARM_L2_INDEX(end_addr);
            uint32_t *l2page = (uint32_t *) l2_addr;
            l2page[pte] = (phys_addr + size) & ARM_L2_ADDR_MASK;
            l2page[pte] |= ARM_L2_TYPE_SMALL;
            l2page[pte] |= ARM_L2_AP_KRW_URW;
            l2page[pte] |= ARM_L2_WRITE_BACK; // на время инициализации у нас ничего не кешируется
            l2page[pte] |= ARM_L2_S;
            size -= ARM_L2_SIZE;
        }
    }

    for (phys_bytes iter_addr = vir_addr; iter_addr < vir_addr + size; iter_addr += ARM_L1_SIZE) {
        pde = ARM_L1_INDEX(iter_addr);
        pagetable[pde] = phys_addr & ARM_L1_ADDR_MASK;
        pagetable[pde] |= ARM_L1_TYPE_SECTION;
        pagetable[pde] |= ARM_L1_WRITE_BACK;
        pagetable[pde] |= ARM_L1_DOMAIN(0);
        pagetable[pde] |= ARM_L1_AP_KRW_URW;
        pagetable[pde] |= ARM_L1_S;
        phys_addr += ARM_L1_SIZE;
    }
}

/*
 * Разметка виртуального адреса для доступа к новым структурам ядра
 * Находим первый свободный диапозон с конца виртуальной памяти размером с размер региона
 * и мапим туда регион
 * потом возвращаем новый виртуальный адрес старта
 * Задача: размапиться как можно более компактно, то есть влезть в страницы 4кб в самый притык
 * Поэтому мы не будем выравнивать наши виртуальные адреса по 1мб, а выровняемся по 4 кб
 * */
vir_bytes pg_map_high(mmap_region_t *region) {
    vir_bytes vir_start = 0;

    // Сначала найдём пространство для нашего региона
    vir_bytes size = region->size;
    for (vir_bytes iter = ARM_L1_SIZE * ARM_L1_ENTRIES; iter > 0; iter -= ARM_L1_SIZE) {
        if (pagetable[ARM_L1_INDEX(iter)] == 0) {
           // Мы наткнулись на целый свободный мегабайт
           if (size == ARM_L1_SIZE) {
               vir_start = iter;
               size = 0;
           } else if (size < ARM_L1_SIZE) {
               vir_start = iter + (ARM_L1_SIZE - size);
               size = 0;
           } else {
               vir_start = iter;
               size -= ARM_L1_SIZE;
           }
        } else if (pagetable[ARM_L1_INDEX(iter)] & ARM_L1_TYPE_L2PT) {
           // Мы наткнулись на таблицу L2 - будем её итерировать
           uint32_t *l2pt = (uint32_t *) (pagetable[ARM_L1_INDEX(iter)] & (~ARM_L2PT_ADDR_MASK));
           for (int i = ARM_L2_ENTRIES - 1; i >= 0; i--) {
               if (l2pt[i] == 0) {
                   // Страница свободна
                   vir_start = iter + ARM_L2_SIZE * i;
                   size -= ARM_L2_SIZE;
               } else {
                   // наткнулись на занятую страницу
                   vir_bytes size = region->size;
                   break;
               }
               if (size == 0) {
                   break;
               }
           }
        } else {
           vir_bytes size = region->size;
        }

        if (size == 0) {
            break;
        }
    }

    // Теперь разместимся здесь
    pg_map_region_to_vir (region, vir_start);

    return vir_start;
}