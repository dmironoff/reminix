//
// Created by dmironov on 19.03.2026.
//

/*
 * Абстрактные, архитектурно независимые таблицы памяти
 * Их создаёт VM и передаёт ядру для применения,
 * ядро уже их интерпретирует в страницы памяти для конкретного процессора
 * Механизм работы похож на карты физической памяти
 * В каждой таблице есть регионы
 * Каждый регион имеет size
 * Если регион это таблица l2 то он не может быть больше SECTION SIZE
 * внутри таблицы l2 тоже регионы, что бы размечать память сразу по size
 *
 * см. apt_utils.h - это функции для работы с этой структурой
 */

#ifndef REMINIX_ABSTRACT_PAGETABLES_H
#define REMINIX_ABSTRACT_PAGETABLES_H

#include "physmemorymap.h"

typedef uint32_t vm_apt_flags_t;

typedef enum {
    VM_RECORD_UNDEF     = 0,   // Неиспользуемая запись, можно брать
    VM_RECORD_FREE      = 1,   // Используется в таблице, но вызывает pagefault
    VM_RECORD_INUSE     = 2,   // Размеченая память
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
    vir_bytes          vaddr;
    phys_bytes          paddr; // Если виртуальная, то будет равно 0
    vir_bytes          size;
    vm_apt_flags_t      flags;
    mmap_cache_hint_t   cache_hint;
    void                       *next;
    void                       *prev;
} vm_abstract_pt_l2_entry_t;

typedef struct {
    vm_apt_record_status_t      status;
    vir_bytes                  vaddr;
    phys_bytes                  paddr;
    vm_abstract_pt_l1_type      type;

    vir_bytes                  size;// Размер, для того что бы экономить память,
                                        // мы можем так размечать целый диапазон, а ядро уже разберётся
                                        // должен быть кратен размеру секции l1 Для l2 должен быть равен размеру секции
    vm_apt_flags_t              flags;
    mmap_cache_hint_t           cache_hint;
    unsigned long               l2_entries_count;
    vm_abstract_pt_l2_entry_t   *first_l2_entry;
    vm_abstract_pt_l2_entry_t   *last_l2_entry;

    int                         dirty;  // 0 - чистая, 1 - грязная, требует переобработки ядром после изменений

    void                       *next;
    void                       *prev;
} vm_abstract_pt_l1_entry_t;

typedef struct {
    vm_apt_record_status_t      status;
    endpoint_t                  owner;
    unsigned long                    version;
    phys_bytes                  phys_pt_root;
    unsigned long                    entries_count;
    unsigned long                    entries_dirty;  // Количество грязных секций, для понимания ядром, если больше нуля
                                                    // То ядро должно обойти всю таблицу, найти грязные секции и перенести информацию в физическую таблицу страниц
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
    unsigned long                    l1_sections_max_count;   // Максимальное количество l1 секций на архитектуре
    phys_bytes                  l1_section_size;     // Размер l1 секции на архитектуре
                                                // Соответственно общий объём памяти возможный к разметке на архитектуре:  l1_sections_max_count * l1_section_size
                                                // Для ARM и RISC-V: так как ядро размеченно в другом регистре другой таблицей,
                                                // то здесь количество записей должно быть равно тому что помещается в регистр таблиц для пользовательских приложений
    phys_bytes                  l2_page_size;    // Размер страницы l2 на архитектуре
                                                // Количество страниц l2 в секции l1: l1_section_size / l2_page_size
    unsigned long                    pagetables_allocated; // Количество записей о таблицах аллоцированное в памяти
    unsigned long                    pagetables_used;   // Счётчик использованых записей
    vm_abstract_pt_t            *first_pagetable;   // Указатель на вершину связанного списка
    vm_abstract_pt_t            *last_pagetable;   // Указатель на конец связанного списка
    unsigned long                    l1_entries_allocated;  // Количество записей о l1 аллоцированное в памяти
    unsigned long                    l1_entries_used;    // Количество использованных записей
    unsigned long                    l2_entries_allocated;
    unsigned long                    l2_entries_used;
    vm_abstract_pt_t            *tables;            // Указатели на весь массив аллоцированных записей
    vm_abstract_pt_l1_entry_t   *l1_entries;
    vm_abstract_pt_l2_entry_t   *l2_entries;
} vm_abstract_pagetables_t;

#endif //REMINIX_ABSTRACT_PAGETABLES_H
