//
// Created by dmironov on 19.03.2026.
//

/*
 * Абстрактные, архитектурно независимые таблицы памяти
 * Их создаёт VM и передаёт ядру для применения,
 * ядро уже их интерпретирует в страницы памяти для конкретного процессора
 */

#ifndef REMINIX_ABSTRACT_PAGETABLES_H
#define REMINIX_ABSTRACT_PAGETABLES_H

#include "physmemorymap.h"

typedef uint32_t vm_apt_flags_t;

typedef enum {
    VM_RECORD_UNDEF     = 0,
    VM_RECORD_INUSE     = 1,
} vm_apt_record_status_t;

/* --- Тип региона (взаимоисключающие биты 0..7) --- */
#define VM_APF_ANON         (1u <<  0)  /* анонимная память (heap, stack, mmap)  */
#define VM_APF_KERNEL       (1u <<  1)  /* область ядра (присутствует везде)     */
#define VM_APF_DEVICE       (1u <<  2)  /* MMIO регистры периферии               */
#define VM_APF_DMA          (1u <<  3)  /* DMA-буфер (physically contiguous)     */
#define VM_APF_SHARED       (1u <<  4)  /* shared между несколькими процессами   */
#define VM_APF_STACK        (1u <<  5)  /* стек (растёт вниз, guard page)        */
#define VM_APF_VM_SHARED    (1u <<  5)  /* общая память ядра и менеджера памяти
 *                                         При работе VM будет размечена как доступная пользователю,
 *                                         а при работе пользовательского процесса будет доступна только ядру */

/* --- Права доступа (биты 8..11) --- */
#define VM_APF_READ         (1u <<  8)  /* чтение разрешено                      */
#define VM_APF_WRITE        (1u <<  9)  /* запись разрешена                      */
#define VM_APF_EXEC         (1u << 10)  /* исполнение разрешено                  */
#define VM_APF_USER         (1u << 11)  /* доступно userspace (ring3/EL0)        */

/* --- Состояние страницы (биты 16..23) --- */
#define VM_APF_PRESENT      (1u << 16)  /* физическая страница выделена          */
#define VM_APF_VIRTUAL_ONLY (1u << 17)  /* demand paging: виртуально есть,
                                           физически — нет. НЕ отображать
                                           в физическую таблицу страниц.
                                           При первом обращении — pagefault,
                                           VM выделяет физ. страницу и снимает
                                           этот флаг.                            */
#define VM_APF_COW          (1u << 18)  /* copy-on-write. Физическая страница
                                           shared (refcount > 1), mapped as RO.
                                           При записи: ядро генерит fault,
                                           VM копирует страницу, снимает COW,
                                           уменьшает refcount оригинала.         */
#define VM_APF_DIRTY        (1u << 19)  /* страница изменялась с последнего sync */
#define VM_APF_LOCKED       (1u << 20)  /* pinned — не выгружать из памяти       */
#define VM_APF_GUARD        (1u << 21)  /* guard page — любое обращение = SIGSEGV */

/* --- Флаги доступа к периферии (биты 24..26) --- */
#define VM_APF_DEV_EXCL     (1u << 24)  /* единоличный доступ к MMIO             */
#define VM_APF_DEV_SHARE_R  (1u << 25)  /* несколько процессов могут читать      */
#define VM_APF_DEV_SHARE_W  (1u << 26)  /* несколько процессов могут писать      */

/* --- Удобные комбинации --- */
#define VM_APF_RW       (VM_APF_READ | VM_APF_WRITE)
#define VM_APF_RO       (VM_APF_READ)
#define VM_APF_RWX      (VM_APF_READ | VM_APF_WRITE | VM_APF_EXEC)


typedef enum {
    VM_APT_L1_SECTION       = 0,  // Является цельной секцией
    VM_APT_L1_L2PT          = 1  // Является ссылкой на таблицу L2
} vm_abstract_pt_l1_type;

typedef struct {
    vm_apt_record_status_t  status;
    uint32_t            version;
    vir_bytes          vaddr;
    phys_bytes          paddr; // Если виртуальная, то будет равно 0
    phys_bytes          size;
    vm_apt_flags_t      flags;
    mmap_cache_hint_t   cache_hint;
    void                       *next;
    void                       *prev;
} vm_abstract_pt_l2_entry_t;

typedef struct {
    vm_apt_record_status_t      status;
    uint32_t                    version;
    vir_bytes                  vaddr;
    phys_bytes                  paddr;
    phys_bytes                  size;// Размер, для того что бы экономить память,
                                        // мы можем так размечать целый диапазон, а ядро уже разберётся
                                        // должен быть кратен размеру страницы
    vm_apt_flags_t              flags;
    mmap_cache_hint_t           cache_hint;
    vm_abstract_pt_l1_type      type;
    uint32_t                    l2_entries_count;
    vm_abstract_pt_l2_entry_t   *first_l2_entry;
    vm_abstract_pt_l2_entry_t   *last_l2_entry;

    void                       *next;
    void                       *prev;
} vm_abstract_pt_l1_entry_t;

typedef struct {
    vm_apt_record_status_t      status;
    endpoint_t                  owner;
    uint32_t                    version;
    phys_bytes                  phys_pt_root;
    uint32_t                    entries_count;
    uint32_t                    entries_dirty;
    vm_abstract_pt_l1_entry_t   *first_entry;
    vm_abstract_pt_l1_entry_t   *last_entry;

    void                         *next;
    void                          *prev;
} vm_abstract_pt_t;


typedef uint32_t vm_abstract_arch_flags;

/*
 * Общий массив таблиц страниц
 * Как всегда связанный список,
 * но мы будем его в процессе работы сразу сортировать по endpoint_t для более быстрой работы системы
 * Ну такая идея, просто сразу хочется избавится от завязывания на количестве процессов
 */
typedef struct {
    // Сразу разместим здесь настройки для vm
    // Что бы не переписывать VM под разные архитектуры, просто ядро передаст VM базовые констранты таблиц
    // А сами эти константы мы инициализируем в pre_init исходя из архитектуры машины
    vm_abstract_arch_flags      flags;
    uint32_t                    l1_entry_max_count;
    phys_bytes                  l1_entry_size;
    phys_bytes                  l2_entry_size;
    uint32_t                    pagetables_allocated;
    uint32_t                    pagetables_used;
    vm_abstract_pt_t            *first_pagetable;
    vm_abstract_pt_t            *last_pagetable;
    uint32_t                    l1_entries_allocated;
    uint32_t                    l1_entries_used;
    uint32_t                    l2_entries_allocated;
    uint32_t                    l2_entries_used;
    vm_abstract_pt_t            *tables;
    vm_abstract_pt_l1_entry_t   *l1_entries;
    vm_abstract_pt_l2_entry_t   *l2_entries;
} vm_abstract_pagetables_t;

#endif //REMINIX_ABSTRACT_PAGETABLES_H
