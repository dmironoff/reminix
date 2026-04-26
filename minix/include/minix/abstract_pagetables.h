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
#define VM_APF_VM_SHARED    (1u <<  6)  /* общая память ядра и менеджера памяти
 *                                         При работе VM будет размечена как доступная пользователю,
 *                                         а при работе пользовательского процесса будет доступна только ядру */
#define VM_APF_USER_TO_KERNEL_CP_SPACE  (1u <<  6)  /*Пространство для копирования данных между ядром и пользовательскими процессами
 * Всегда размеченно как виртуальное и размечается в физической таблице страниц только если нужно
 * Этот механизм работает мимо apt - apt отвечает только за то что бы эти адреса не занимались ничем*/

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


typedef struct {
    vm_apt_record_status_t      status;
    vir_bytes                  vaddr;
    phys_bytes                  paddr;

    vir_bytes                  size;// Размер, для того что бы экономить память,
                                        // мы можем так размечать целый диапазон, а ядро уже разберётся
                                        // должен быть кратен размеру страницы
    vm_apt_flags_t              flags;
    mmap_cache_hint_t           cache;

    void                       *next;
    void                       *prev;
} vm_abstract_pt_entry_t;

typedef struct {
    kmutex_t                    lock;
    vm_apt_record_status_t      status;
    unsigned long                    version;

    vm_abstract_pt_entry_t   *first; // вершина связанного списка страниц
    vm_abstract_pt_entry_t   *last; // конец связанного списка страниц

    void                         *next;
    void                          *prev;
} vm_abstract_pt_t;


typedef uint32_t vm_abstract_arch_flags;

/*
 * Общий массив таблиц страниц
 * Как всегда связанный список
 * В ядре и VM будет основной из глобальных переменных
 */
typedef struct {
    kmutex_t                    lock; // Мьютекс на случай если сейчас мы переделываем весь массив таблиц
    // Сразу разместим здесь настройки для vm
    // Что бы не переписывать VM под разные архитектуры, просто ядро передаст VM базовые констранты таблиц
    // А сами эти константы мы инициализируем в pre_init исходя из архитектуры машины
    vm_abstract_arch_flags      flags;
    unsigned long               pages_max_count;        // Максимальное количество страниц на архитектуре
    phys_bytes                  page_size;    // Размер страницы на архитектуре
    unsigned long                    pagetables_allocated; // Количество записей о таблицах аллоцированное в памяти
    unsigned long                    pagetables_used;   // Счётчик использованых записей
    vm_abstract_pt_t            *first;   // Указатель на вершину связанного списка таблиц
    vm_abstract_pt_t            *last;   // Указатель на конец связанного списка таблиц
    unsigned long                    entries_allocated;  // Количество записей о страницых аллоцированное в памяти
    unsigned long                    entries_used;    // Количество использованных записей
    vm_abstract_pt_t            *tables;            // Указатели на весь массив аллоцированных записей
    vm_abstract_pt_entry_t   *entries;
} vm_abstract_pagetables_t;

#endif //REMINIX_ABSTRACT_PAGETABLES_H
