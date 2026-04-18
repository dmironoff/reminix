//
// Created by dmironov on 17.04.2026.
//

#define _SYSTEM		1   /* Включает системные привилегии в заголовках MINIX */

#include <minix/callnr.h>   /* Номера системных вызовов */
#include <minix/com.h>      /* Константы IPC: номера процессов, типы сообщений */
#include <minix/config.h>
#include <minix/const.h>    /* Системные константы */
#include <minix/ds.h>       /* Data Store сервис */
#include <minix/endpoint.h> /* Типы и макросы для endpoint'ов */
#include <minix/minlib.h>
#include <minix/type.h>
#include <minix/ipc.h>      /* Базовые IPC примитивы: ipc_send, ipc_receive */
#include <minix/sysutil.h>
#include <minix/syslib.h>
#include <minix/const.h>
#include <minix/bitmap.h>
#include <minix/rs.h>       /* Интерфейс с Reincarnation Server */
#include <minix/vfsif.h>    /* Интерфейс VM <-> VFS */

#include <sys/exec.h>
#include <libexec.h>        /* Загрузчик ELF-бинарников */
#include <ctype.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

#define _MAIN 1             /* Объявляет глобальные переменные из glo.h (не extern) */
#include "glo.h"            /* Глобальные переменные: vmproc[], kernel_boot_info и др. */
#include "proto.h"          /* Прототипы всех внутренних функций VM */
#include "vm.h"             /* Внутренние константы и типы VM */

#include <machine/archtypes.h>
#include <sys/param.h>
#include "kernel/const.h"
#include "kernel/config.h"
#include "kernel/proc.h"    /* Структура proc — процессная запись в ядре */

#include <signal.h>
#include <lib.h>
#include <minix/abstract_pagetables.h>
#include <minix/physmemorymap.h>



/*
 * Таблица обработчиков IPC-вызовов.
 *
 * Индексируется по (m_type - VM_RQ_BASE).
 * Каждая запись хранит указатель на функцию-обработчик и строковое имя.
 * Заполняется в init_vm() макросом CALLMAP().
 * Проверка доступа через acl_check() происходит ДО вызова vmc_func.
 */
struct {
    int (*vmc_func)(message *);  /* Указатель на обработчик вызова */
    const char *vmc_name;        /* Имя вызова для диагностики */
} vm_calls[NR_VM_CALLS];

/*
 * CALLNUMBER(c) — проверяет и нормализует номер вызова.
 * Вызовы VM занимают диапазон [VM_RQ_BASE, VM_RQ_BASE + NR_VM_CALLS).
 * Возвращает индекс в vm_calls (0-based) или -1 при выходе за диапазон.
 */
#define CALLNUMBER(c) (((c) >= VM_RQ_BASE && 				\
			(c) < VM_RQ_BASE + ELEMENTS(vm_calls)) ?	\
			((c) - VM_RQ_BASE) : -1)

/* === Регистрация обработчиков IPC-вызовов === */
#define CALLMAP(code, func) { int _cmi;		      \
	_cmi=CALLNUMBER(code);				\
	assert(_cmi >= 0);				\
	assert(_cmi < NR_VM_CALLS);			\
	vm_calls[_cmi].vmc_func = (func); 	      \
	vm_calls[_cmi].vmc_name = #code;	      \
}

/*
 * Инициализация VM при первом старте системы
 *
 *  А я напомню мы запускаемся в своём собственном адресном пространстве собранном из портотипа, котрое собрало ядро.
 *  Так что в нашем адресном пространстве уже порядок
 *
 * Что должна делать:
 * 1. Получить kinfo с адресами apt, mmap, boot_modules
 * 2. Обнулить vmproc[0]
 * 3. Проинициализировать acl
 * 4. Запустить все процессы серверов, остальные стартовые процессы положить в приостановленном состоянии
 * до их инициализации соответствующими серверами
 * 5.Заполнить таблицу vm_calls[] и назначить обработчики вызовов
 * 6. Зарегистрировать в SEF
 *
 */
void init_vm(void) {
    extern void __minix_init(void);

    if (OK != (s = sys_getkinfo(&kbi))) {
        panic ("Couldn`t get kinfo: %d", s);
    }
    apt = kbi.apt;
    mmap = kbi.mmap;

    __minix_init();


    /* Включён ли file mmap? По умолчанию да, можно отключить через env */
    enable_filemap=1;
    env_parse("filemap", "d", 0, &enable_filemap, 0, 1);

    memset(vm_calls, 0, sizeof(vm_calls));

    /* Базовые вызовы управления памятью */
    CALLMAP(VM_MMAP, do_mmap);
    CALLMAP(VM_MUNMAP, do_munmap);
    CALLMAP(VM_MAP_PHYS, do_map_phys);
    CALLMAP(VM_UNMAP_PHYS, do_munmap);

    /* Вызовы от PM (Process Manager) */
    CALLMAP(VM_EXIT, do_exit);
    CALLMAP(VM_FORK, do_fork);
    CALLMAP(VM_BRK, do_brk);
    CALLMAP(VM_WILLEXIT, do_willexit);
    CALLMAP(VM_PROCCTL, do_procctl_notrans);

    /* Вызовы от VFS (Virtual File System) */
    CALLMAP(VM_VFS_REPLY, do_vfs_reply);
    CALLMAP(VM_VFS_MMAP, do_vfs_mmap);

    /* Вызовы от RS (Reincarnation Server) */
    CALLMAP(VM_RS_SET_PRIV, do_rs_set_priv);
    CALLMAP(VM_RS_PREPARE, do_rs_prepare);
    CALLMAP(VM_RS_UPDATE, do_rs_update);
    CALLMAP(VM_RS_MEMCTL, do_rs_memctl);

    /* Общие вызовы (shared memory, физические адреса) */
    CALLMAP(VM_REMAP, do_remap);
    CALLMAP(VM_REMAP_RO, do_remap);
    CALLMAP(VM_GETPHYS, do_get_phys);
    CALLMAP(VM_SHM_UNMAP, do_munmap);
    CALLMAP(VM_GETREF, do_get_refcount);
    CALLMAP(VM_INFO, do_info);

    /* Управление кэшем блоков файловой системы */
    CALLMAP(VM_MAPCACHEPAGE, do_mapcache);
    CALLMAP(VM_SETCACHEPAGE, do_setcache);
    CALLMAP(VM_FORGETCACHEPAGE, do_forgetcache);
    CALLMAP(VM_CLEARCACHE, do_clearcache);

    /* Статистика ресурсов процесса */
    CALLMAP(VM_GETRUSAGE, do_getrusage);
}

/*
 * Основная функция нашего сервера - точка входа и главный цикл
 */
int main(void) {
    panic("VM STARTED");
    return OK;
}

/*
 * Инициализация процесса из boot
 */
