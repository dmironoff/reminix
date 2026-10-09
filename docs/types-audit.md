# ReMinix — аудит типов данных на 64-битную переносимость (этап 0.2)

> Этап 0.2 плана модернизации (`docs/modernization.md`, п. 7, ч. 1). Результат —
> **список мест, без правок кода**. Исправления идут по шагам: физические адреса и
> `refcount` — А1 (`docs/memory.md`, М1), остальное — по шагам модернизации и в порте
> amd64 (столбец «Шаг» в таблицах). Дата: 2026-10-02, дерево — `0b1bf6c49`.

## 1. Кратко

Проверено всё, что работает в привилегированном или системном режиме: ядро (общий код,
`arch/i386`, `arch/earm`, ассемблер, `procoffsets.cf`), граница ядро–серверы
(`minix/include`), все системные серверы (VM, PM, VFS, RS, DS, SCHED, MIB, IPC, IS,
DEVMAN, INPUT), файловые системы (MFS, ext2, ISO 9660, PFS, procfs, ptyfs, vbfs, hgfs),
сетевые службы (lwip, uds), библиотеки, на которые они опираются (libsys, libminc,
libmthread, libexec, libtimers, обёртки системных вызовов libc, libfsdriver,
libminixfs, libbdev, libblockdriver, libchardriver, libsockevent, libnetdriver,
libvirtio, libusb, libddekit, libgpio, libclkconf, libacpi и др.) и все драйверы
аппаратного ввода-вывода (накопители, шины, сеть, tty, видео, звук, питание, USB,
гостевые драйверы VirtualBox/VMware, IOMMU), включая драйверы только для earm.

Метод: автоматический проход clang по всем ~950 исходникам с настоящими флагами
сборки в трёх режимах (i386, x86_64, i386 с 64-битным `phys_bytes`), сравнение
раскладок всех структур, постоянный тест раскладок на хосте (`minix/tests/host/abi64`)
и ручное чтение каждого найденного места (§2).

**Итог: 323 места в таблицах** (§11) плюс около 400 безопасных сужений, сведённых
группами. По проявлению: 107 — тихие ошибки в 64-битном режиме, 79 — только при
физических адресах выше 4 ГБ (и на 64, и на PAE/LPAE), 26 — в обоих случаях, 37 —
ошибки сборки, остальное — заметки. По шагам: 105 — порт amd64/aarch64 (большая часть —
переписываемый архитектурный код), 82 — драйверы, 27 — переработка сообщений IPC,
30 — А1, по нескольку — А2–А7, Б1–Б2, В1, В4, п. 8.

Главное:

1. **Сообщения IPC.** На LP64 128 из 256 типов полезной нагрузки больше 56 байт,
   `message` — 104 байта вместо 64, и `_ASSERT_MSG_SIZE` останавливает сборку всего
   системного кода. Кроме раскладки, адреса ходят в 32-битных полях:
   `ps_strings` в `m7_i5` (`int`), корень таблицы страниц в `m1_i3` (`int`),
   адрес сегмента ELF в `u32_t`. Это отдельный шаг (§5, §8), а не правка по месту.
2. **`reg_t` — 32 бита** (`unsigned` на i386, `u32_t` на earm) и через него идут
   указатели: аргументы IPC (`do_ipc(r1, r2, r3)`), кадр сигнала, стек, `p_cr3`,
   `cpuid`. В ядре есть и явные `(u32_t)` для адресов (`do_exec.c`, `arch_proc_init`).
3. **Макрос `cpuid`** (`arch_smp.h:11`) получает номер CPU из адреса стека через
   `u32_t` — на этом стоят все данные «на CPU» SMP-ядра (236 мест прохода clang). Шаг А4.
4. **`exec` на LP64 не работает:** вершина стека нового процесса в `int`
   (`libc/sys/stack_utils.c`, `rs/exec.c`, `vm/main.c`), `argc` пишется словом
   указателя, а считается 4-байтным (`STACK_MIN_SZ`, `ps_argvstr`); загрузчик ELF
   отсекает заголовки программы больше 512 байт (ELF64 — ≤ 8 заголовков).
5. **Физические адреса выше 4 ГБ** теряются почти везде: `phys_bytes` = `unsigned
   long` (32 бита на i386/earm); `phys_clicks` и номера страниц — `unsigned`/`int`;
   физические адреса передаются как `void *` (`vm_map_phys`, 25 потребителей) и как
   `long` (`SVMCTL_MAP_PHYS_*`); в ядре и VM линейные и физические адреса смешаны
   (`createpde(…, phys_bytes *)` для линейных, `vm_suspend(…, vir_bytes)` для
   физических); VM-аллокатор жёстко на 4 ГБ (`alloc.c`).
6. **DMA и железо.** Нет флага «выделить ниже 4 ГБ» (`alloc_contig` знает только
   16 МБ и 1 МБ), а почти все драйверы пишут в устройство только младшие 32 бита адреса,
   даже если устройство умеет 64 (e1000, rtl8169, ip1000, AHCI без S64A). Аппаратные
   структуры с типами, зависящими от ширины: `at_wini` `struct prdte` (`phys_bytes`
   в PRD-записи: 8 → 16 байт), `lance_init_block` (`unsigned long filter[2]`),
   `VMMDevHGCMParam` (12 → 24 байт), `dec21140A` (8-байтная запись `sys_umap` в
   32-битное слово дескриптора), usbd `hcd_reg4` = `unsigned long` (64-битные
   обращения к 32-битным регистрам на aarch64), `minix/mmio.h` `read32/write32(uint32_t
   address)`. PCI хранит BAR в 32 битах и отбрасывает 64-битные BAR выше 4 ГБ.
7. **Загрузчик и прошивка.** Структуры multiboot (`char *`, `vaddr_t`, неупакованный
   `uint64_t`) на x86_64 читаются неверно (abi64: `multiboot_info` 120 → 160 байт,
   `multiboot_mmap.mm_base_addr` со смещения 4 на 8); ACPI в ядре — только RSDT
   (32-битные указатели), XSDT не поддерживается; earm `pg_utils.c` обрезает 64-битный
   адрес банка памяти до 32 бит.
8. **Тихие ошибки в библиотеках:** `ex64lo()` на LP64 возвращает всё 64-битное
   значение; libfsdriver кладёт указатель в 32-битный `cp_grant_id_t` союза
   (`call.c:241`); libbdev шлёт `iovec_s_t`, а libblockdriver читает его как
   `iovec_t`; lwip — `MEM_ALIGNMENT 4` и `assert(sizeof(void *) == 4)`; filter — CRC
   на `long` (выход за таблицу, порча дискового формата контрольных сумм).
9. **Хорошие новости.** Дисковые форматы MFS, ext2, ISO 9660 и таблица разделов от
   ширины не зависят (проверено и автоматически, и чтением: `zone_t`/`block_t`/`bit_t`/
   `bitchunk_t` — `uint32_t`); кольца virtio, заголовки virtio-blk/net и дескрипторы
   e1000 тоже; `procoffsets.cf` генерирует смещения сам; битовые карты на `bitchunk_t`
   переносимы (после §4.16 `testing.md`); RS, DS, SCHED, DEVMAN, INPUT 64-битных
   дефектов не имеют.
10. **Сборка на новой архитектуре** упрётся сначала в архитектурные развилки:
    `CLICK_SIZE` (`#error Unsupported arch` в `minix/const.h`), `arch/${MACHINE_ARCH}`
    в libsys/libminc/libc/tty/readclock, `#error` в `_ucontext.c`, libmthread, PM/MIB
    (uname), `MACHINE_ARCH == "i386"` в Makefile драйверов (категория ARCH, §11).

## 2. Как проверяли

### 2.1 Проход clang по всему дереву (`minix/tests/host/abi64/scan64.py`)

Флаги сборки каждого из 109 каталогов программ и библиотек берутся из самой сборки:
`nbmake-i386 -V '${CPPFLAGS}' -V '${CFLAGS}' -V '${SRCS}' -V '${.PATH}'` в контейнере
сборки (основной репозиторий смонтирован только для чтения), отдельно для ядра без SMP
и с `CONFIG_SMP`. Каждый исходник компилируется хостовым clang-18 с `-fsyntax-only` и
предупреждениями о ширине (`-Wshorten-64-to-32`, `-W*pointer*-cast`, `-Wformat`,
`-Wincompatible-pointer-types` и др.) в режимах:

| Режим | Что | Заголовки |
|---|---|---|
| m32 | i586, как сейчас (база для сравнения) | установленные (`DESTDIR`) |
| m64 | x86_64 с `-D__i386__`: существующие i386-ветки кода при 64-битных `long` и указателях | те же + LP64-прослойка `machine/` (`size_t`, `ssize_t`, `ptrdiff_t`, `register_t`, `LONG_*`) |
| p64 | i586 с `phys_bytes` = `uint64_t` | те же, правка `minix/type.h` |

Проверки размера сообщений в m64/p64 выключены (их меряет abi64, §2.2). Результат —
места, появившиеся в m64/p64 и отсутствующие в m32, и раскладки структур
(`-fdump-record-layouts`), различающиеся в m32 и m64/p64.

| | m64 | p64 |
|---|---|---|
| Новые диагностики (UP + только SMP) | 1036 + 236 | 154 + 1 |
| из них `-Wshorten-64-to-32` | 766 | 126 |
| приведения указатель↔целое | 419 (вкл. 234 SMP) | — |
| `-Wformat` | 64 | 21 |
| ошибки компиляции (файлы) | 8 | 3 |
| Структуры с другой раскладкой (из 1897) | 1082 | 63 |

Ограничения прохода: компилируется только то, что входит в сборку i386 (драйверы и
ядро earm, ассемблер — разобраны вручную); заголовки `machine/`, специфичные для MINIX
(`stackframe.h`, `mcontext.h`, `archconst.h`), — от i386, поэтому `reg_t`, `clock_t`,
`ELFSIZE` в m64 остаются 32-битными (это то, что получится, если просто собрать текущий
код под 64 бита); clang не видит обрезку без приведения между 32-битными типами и
явные приведения — их искали шаблонами (§2.3).

**Эталон перед А1 (2026-10-07, `fc5cf831c`, сборка с нуля).** UP — как в
таблице: m64 1036 новых (764 `-Wshorten-64-to-32`, 64 `-Wformat`), p64 154 (21
`-Wformat`), ошибки в 8 и 3 файлах. Только SMP: m64 — 330 (было 236; рост —
MCS, IPI, `ipcstat`, размещение служб после аудита: приведения
указатель↔целое в `proc.c`, `smp.c`, `arch_clock.c`, `memory.c`,
`arch_smp.c`), p64 — 1. Раскладок, различающихся в m64/p64, — **814 и 54** из
1897 (в таблице — 1082 и 63; `scan64.py` с аудита не менялся, прежние
`ldiff-*.txt` не сохранились, причину расхождения установить нельзя).
Ход А1 меряется от этих чисел (`obj/abi64-scan/summary.txt`).

### 2.2 Тест раскладок на хосте (`minix/tests/host/abi64`, постоянный)

Пробник собирается с заголовками MINIX (дерево `machine/` из исходников, для 64 бит —
та же LP64-прослойка), эталон — его же 32-битная сборка. Проверяется:

- каждый тип полезной нагрузки сообщения — ровно 56 байт, `message` — 64 байта;
- структуры, чью раскладку задаёт внешний мир (загрузчик multiboot, таблица разделов,
  дисковые части MFS/ext2/ISO 9660, кольца virtio, заголовки virtio-blk/net,
  дескрипторы e1000), совпадают с i386;
- структуры границы ядро–процессы (`kinfo`, `machine`, `minix_kerninfo`, `cp_grant_t`,
  `vumap_*`, `stackframe_s`, `mcontext_t`, `ps_strings`, …) — выводятся для сведения.

На m32 всё проходит; на m64 известные проблемы — TAP `# TODO`
(`messages_56_bytes`, `message_union_64_bytes`, `external_layouts_known_bugs`), а
«храповик» (`KNOWN_BAD_MSGS_LP64` = 128 и список внешних структур) валит прогон, если
сломается новое сообщение или новая внешняя структура. Исправленная проблема даёт
`ok … # TODO` — `run.sh` выводит её отдельной строкой («remove TODO»). Запуск:
`make -C docker -f build.mk host-test [HOST_TESTS=abi64]` (`testing.md` §1д).

### 2.3 Ручной разбор

Каждое место из прохода clang прочитано; безопасные сужения (длины до `ARG_MAX`,
индексы, endpoint'ы, счётчики, 24-битные адреса ISA) в таблицы не внесены, а посчитаны
группами в разделах участков. Сверх clang искали шаблонами: адреса и размеры в
`u32_t`/`int`/`unsigned` без приведения, `0xffffffff`/`~0U`/`(u32_t)-1`/4 ГБ, битовые
карты на 32 бита, `sizeof(long)`, запись DMA-адресов в регистры без старшей половины,
регистровые и дескрипторные структуры с `long`/указателями, `ex64lo/ex64hi`,
ассемблер с 4-байтовыми слотами. Разбор шёл пятью участками (ядро, граница и VM;
серверы и базовые библиотеки; ФС, сеть и библиотеки драйверов; драйверы; сетевые
драйверы); выборочные находки каждого участка перепроверены.

## 3. Базовые типы

| Тип | Сейчас | На LP64 | Проблема | Варианты (решение — не здесь) |
|---|---|---|---|---|
| `vir_bytes` | `unsigned long` | 64 | правильно расширяется; но задаёт раскладку сообщений и общих структур | оставить; в сообщениях — поля фиксированной ширины (§5) |
| `phys_bytes` | `unsigned long` | 64 | ширина физ. адреса = ширина `long`: на i386/earm нет PAE/LPAE и памяти > 4 ГБ | решено (§9а): `phys_addr_t` шириной платформы + `CONFIG_PHYS_ADDR_64`; длины — `size_t` |
| `phys_clicks`, `vir_clicks` | `unsigned int` | 32 | номер страницы в 32 битах (до 16 ТБ) — хватает, но `<< CLICK_SHIFT` переполняется | решено (§9а): `pfn_t`, `CLICK_*` → `PAGE_*` |
| `reg_t` | `unsigned` (i386), `u32_t` (earm) | 32 | через него идут указатели (IPC, кадры, стек, `p_cr3`) | решено (§9а): `register_t` платформы, в А1 |
| `endpoint_t` | `int` | 32 | нет | — |
| `cp_grant_id_t` | `int32_t` | 32 | драйверы кладут в него указатели (псевдогранты SELF/KERNEL: tty, pty, vnd, floppy, libfsdriver) | отдельное поле/союз для адреса |
| `size_t`/`ssize_t` | `unsigned int`/`int` | 64 | расхождение `int` ↔ `ssize_t` в прототипах (ошибки сборки), 58 сообщений растут | в сообщениях — фиксированная ширина |
| `clock_t` | `unsigned int` | 32 | 32-битные тики (`kclockinfo` с «резервом для 64-битного uptime») | 64-битный счётчик тиков ядра |
| `time_t`, `off_t`, `ino_t`, `dev_t` | 64 (`long long`) | 64 | уже 64-битные и на i386 — хорошо; но `VMV_INO` (`long`) обрезает `ino_t` на ILP32 | — |
| `u64.h` `ex64lo/ex64hi/make64` | `unsigned long` | 64 | `ex64lo` на LP64 не обрезает | `uint32_t` |
| `atomic_t`, `bitchunk_t` | `u32_t` | 32 | нет | — |

## 4. Классы проблем

Сводка по категориям (строки таблиц §11; у строки может быть несколько категорий):

| Категория | Мест | Суть | Главные места |
|---|---|---|---|
| HW — регистр/DMA/дескриптор | 65 | DMA-адрес только младшими 32 битами; нет ограничения «ниже 4 ГБ»; MMIO через `u32_t` | at_wini PRD, AHCI, все сетевые PCI-мастера, VMMDev, amddev, usbd `hcd_reg4`, `mmio.h`, PCI BAR |
| PHYS — физ. адреса | 60 | `phys_bytes`/`phys_clicks` в 32 битах; физ. адрес как `void *`/`long`/`int`/`vir_bytes` | `type.h`, `alloc.c`, `pt.h`, `sys_vmctl.c`, `vm_map_phys`, ядро `memory.c`, ACPI |
| PTR — указатель в 32-битном целом | 58 | `(u32_t)ptr`, `int vsp`, указатель в `cp_grant_id_t`/`u32_t`-поле | `cpuid`, `stack_utils.c`, `do_exec.c`, `usermapped`, `libfsdriver`, lance |
| ARCH — привязка к i386/earm | 43 | `#error`, `arch/${MACHINE_ARCH}`, x86-asm, `MACHINE_ARCH == "i386"` | `const.h`, `_ucontext.c`, libsys/libminc/libc arch, PM/MIB, tty, readclock, hgfs |
| MSG — сообщения IPC | 22 (+128 типов) | раскладка; адреса в `int`/`u32_t`/`long`-полях | §5 |
| FMT — форматы | 22 | `%x`/`%lx`/`%llx` для адресов, `off_t`, `dev_t`, `ino_t` | VFS, libsys, ядро, VM |
| CONST — константы | 22 | `0xFFFFF000`, `0xFFFFFFFE`, `(u32_t)-1`, `LONG_MAX`, `ULONG_MAX`, 4 ГБ, 512 байт ELF | `vm.h`, `alloc.c`, `u64.h`, MFS, libsffs, lwip, libexec |
| LAYOUT — внешняя раскладка | 20 | структуры диска/протокола/загрузчика с типами, зависящими от ширины | multiboot, VirtualBox (VMMDev, shared folders), filter, `iovec_s_t`↔`iovec_t`, стек `exec` |
| VIR — размеры | 17 | `size_t`/`vir_bytes` → `int` | VFS read/write > 2 ГБ, `sys_umap`, libexec BSS |
| ERR — ошибка сборки | 14 | `int` ↔ `ssize_t` в прототипах, `Elf32_*` ↔ `Elf_*`, `_ASSERT_MSG_SIZE` | ext2, libfsdriver, memory/fbd/vnd/mmcblk, ipc, getdents, coredump |
| BITS — 32-битные маски | 7 | `conv4` на `long`, маски PTE | MFS/ext2 `conv4`, filter |
| TIME | 6 | 32-битные тики, `LONG_MAX` в проверках | `kclockinfo`, `do_settime`, `clock_time.c` |
| ASM | 5 | 4-байтовые слоты стека, размер сообщения в `klib.S`, VMware backdoor | `mpx.S`, `sconst.h`, `klib.S`, `backdoor.S` |
| ABI | 3 | `reg_t`, вход IPC | `stackframe.h`, `proc.c` |

По шагам и проявлению:

| Шаг | Мест | | Проявление | Мест |
|---|---|---|---|---|
| amd64 (в т.ч. aarch64) | 105 | | 64 — тихая ошибка в 64-битном режиме | 107 |
| драйв. (в т.ч. с А1) | 82 | | >4Г — физ. адреса выше 4 ГБ | 79 |
| MSG | 27 | | оба | 26 |
| А1 (в т.ч. с А2/А3) | 30 | | сборка | 37 |
| А2–А7, Б1–Б2, В1, В4, п. 8 | 32 | | — (заметка/безопасно) | 74 |
| — | 47 | | | |

## 5. Сообщения IPC

На LP64 128 из 256 типов полезной нагрузки больше 56 байт (полный список —
приложение А), сам `message` — 104 байта. Причины роста: `vir_bytes` — 61 тип,
`size_t` — 58, указатели — 26, `long` (`m*_l*`, `key_t`) — 22, `phys_bytes` — 7,
`ssize_t` — 1. При 64-битном `phys_bytes` на ILP32 (PAE/LPAE) не помещаются 7 типов
(`mess_krn_lsys_sys_umap`, `mess_lsys_krn_readbios`, `mess_lsys_krn_sys_copy`,
`mess_lsys_krn_sys_memset`, `mess_lsys_krn_sys_privctl`, `mess_lsys_krn_sys_sdevio`,
`mess_lsys_vm_map_phys`) и `message` целиком.

Кроме раскладки, протоколы переносят адреса и размеры в полях недостаточной ширины —
эти места сломаются и после того, как сообщение «влезет»:

| Протокол | Поле | Тип поля | Что переносится |
|---|---|---|---|
| `SYS_VMCTL` (`VMCTL_SETADDRSPACE`) | `SVMCTL_PTROOT` = `m1_i3` | `int` | физ. адрес корня таблицы страниц |
| `SYS_VMCTL` (`GET_PDBR`) | `SVMCTL_VALUE` = `m1_i3` | `int` | CR3/TTBR |
| `SYS_VMCTL` (`MEMREQ_GET`) | `SVMCTL_MRG_ADDR/LENGTH` = `m2_i2/i3` | `int` | адрес и длина в чужом АП |
| `SYS_VMCTL` (`KERN_MAP_REPLY` и др.) | `SVMCTL_MAP_PHYS_ADDR/LEN` = `m2_l1/l2` | `long` | физ. адреса отображений ядра |
| `VFS_PM_EXEC` / `PM_VFS_EXEC_REPLY` | `VFS_PM_PS_STR`, `VFS_PM_NEWPS_STR` = `m7_i5` | `int` | адрес `ps_strings` |
| `VM_VFS_MMAP` | `m_vm_vfs_mmap.vaddr/len` | `u32_t` | адрес и длина сегмента ELF |
| `VM_VFS_REPLY` | `VMV_INO` = `m10_l1` | `long` | `ino_t` (64 бита) |
| `PCI_GET_BAR` | `base` | `int` | адрес BAR |
| `SYS_UMAP` | `nr_bytes` | `int` | длина области |
| `VM_RS_MEMCTL` | `VM_RS_CTL_LEN` = `m2_i3` | `int` | длина области |
| `VM_GETPHYS` (`vm_getphys`) | `ret_addr` | `void *` | физ. адрес (ID сегмента shm) |
| `vm_map_phys` | `addr` | `void *` | физ. адрес MMIO |
| `SYS_SDEVIO` | `vec_addr` | `phys_bytes` | виртуальный адрес (тип перепутан) |

Размер сообщения зашит также в ассемблер (`klib.S` i386: копирование 15 слов по 4 байта
явными `mov`; earm: два блока по 32 байта) и в `__ALIGNED(16)`/`u8_t size[56]` в `ipc.h`.

Переработка сообщений — отдельный шаг MSG (столбец «Шаг» = MSG; `modernization.md`,
план). Решение — §9.2, правила — `docs/messages.md` §14а. Полное описание всех типов, их полей и направлений с пометками зависимых от
архитектуры полей — `docs/messages.md`.

## 6. Внешние раскладки

| Что | i386 → x86_64 | Вывод |
|---|---|---|
| multiboot: `multiboot_info`, `_mmap`, `_module` | 120→160, 24→32, 16→24 байт; поля после `mi_cmdline` сдвинуты, `mm_base_addr` 4→8 | **сломано** — нужны явные `uint32_t` и упаковка (В4) |
| таблица разделов `part_entry` | 16 = 16 | ок |
| MFS: `d2_inode`, дисковая часть суперблока (до `s_disk_version`) | без изменений | ок (растёт только хвост в памяти) |
| ext2: `d_inode`, `group_desc`, `ext2_disk_dir_desc`, суперблок до `s_reserved` | без изменений | ок |
| ISO 9660: первичный дескриптор (2048 байт) | без изменений | ок (указатель — в хвосте в памяти) |
| virtio: `vring_desc`, `vring_used_elem`, `virtio_blk_outhdr/config`, `virtio_net_hdr` | без изменений (меняется только выравнивание) | ок |
| e1000: дескрипторы rx/tx | без изменений | ок (но `buffer_h` = 0, §11) |
| at_wini `struct prdte` (в `.c`) | 8 → 16 | **сломано** (`phys_bytes` в PRD) |
| lance `lance_init_block` (в `.c`) | 24 → 32 | **сломано** (`unsigned long filter[2]`) |
| VirtualBox `VMMDevHGCMParam` | 12 → 24 | **сломано** (нужен 16-байтный HGCM64 или упаковка до 12) |
| VirtualBox shared folders (`libvboxfs`) | +4…8 байт | под вопросом — сверить с версией хоста |
| filter: контрольные суммы на диске | `long` в формате | **сломано** |
| `iovec_s_t` (libbdev) ↔ `iovec_t` (libblockdriver) | 8 = 8 на i386, 16 ≠ 16 по смыслу на LP64 | **сломано** (разные типы на двух концах) |

Тест abi64 проверяет все структуры из заголовков; структуры, объявленные внутри `.c`
драйверов (`prdte`, `lance_init_block`, …), пробнику недоступны и учтены только здесь.

## 7. Что уже хорошо

- `time_t`, `off_t`, `ino_t`, `dev_t` 64-битные и на i386; `off_t` к ФС и драйверам
  передаётся 64-битным.
- Дисковые форматы, таблица разделов, кольца virtio, дескрипторы e1000 от ширины не
  зависят.
- `procoffsets.cf` + genassym — смещения `struct proc` для ассемблера считаются
  автоматически.
- Битовые карты (`bitmap.h`, `bitchunk_t`) переносимы.
- RS (rprocpub, boot image, ipc_filter), DS, SCHED, DEVMAN, INPUT, libtimers, libasyn,
  libdevman, virtio_net — 64-битных дефектов нет; указатели-«контексты» драйверов в
  32-битных полях дескрипторов не найдены ни в одном сетевом драйвере.

## 8. Рекомендуемый порядок исправлений

1. **А1 (М1) — схема типов памяти (§9а) и `refcount`.** Объём: строки с
   шагом «А1» (§11): типы `phys_bytes`/`phys_clicks`, смешение линейных и физических
   адресов в ядре (`memory.c`, `createpde`, `vm_suspend`), VM (`alloc.c` до замены,
   `main.c`, `mem_*`, `pb.c`, `region.c`), сообщения с физ. адресами
   (`SVMCTL_PTROOT`, `SVMCTL_MAP_PHYS_*`, `vm_map_phys`, `vm_getphys`), константы
   «нет адреса» (`MAP_NONE`, `AM_AUTO`), форматы. Проверка: p64-режим `scan64.py`
   должен стать чистым. А1 идёт подшагами А1.0–А1.8 (`work-plan.md` §5): сначала
   типы §9а синонимами нынешних, переименования и форматы при неизменном машинном
   коде, переключение определений в конце.
2. **А2/А3** — bootmem и buddy убирают `alloc.c` (4 ГБ, `int`-номера страниц) и ранние
   таблицы `pg_utils.c`; earm `pg_utils.c:29,49` (обрезка `mm_base_addr`) — с Б2.
   В А3 же — **флаг выделения DMA-памяти ниже 4 ГБ** (`AC_LOWER4G` в libsys, зона
   `PMF_DMA` у buddy; перенесён из А1 решением владельца 2026-10-07), без которого
   64-битные физ. адреса опасны для всех 32-битных устройств. До А3
   `CONFIG_PHYS_ADDR_64` остаётся пробной сборкой (А1.8).
3. **А4** — API данных «на CPU» заменяет макрос `cpuid` через `u32_t` (и `sched_ipi_data`).
4. **А7 (М4)** — журнал АПТ убирает `pt.h`, `pagetable.c`, `vm/arch`, обход таблиц в
   ядре: эти строки помечены «уходит вместе с кодом» — их не исправлять отдельно.
5. **MSG — переработка сообщений IPC** (§9.2, правила — `messages.md` §14а): 64 байта,
   поля фиксированной ширины, отказ от `mess_1…mess_10`, `_ASSERT_MSG_SIZE` без ручных
   заполнителей; ассемблер копирования не меняется. Без этого шага ни один системный
   файл не соберётся на LP64.
6. **amd64** — `reg_t`, вход IPC, кадр сигнала, `exec` (`stack_utils.c`, `STACK_MIN_SZ`,
   ELF64 в libexec), `u64.h`, `usermapped`, ACPI XSDT, ассемблер и архитектурный слой
   ядра; ARCH-развилки (`const.h`, `arch/${MACHINE_ARCH}` в библиотеках, `_ucontext.c`,
   PM/MIB/IS); ошибки сборки `int`↔`ssize_t` (их можно исправить заранее — правки
   локальные и безопасные на i386).
7. **Драйверы** — при их пересмотре (после многопоточности и порта earm): DMA-адреса
   (старшие половины в e1000/rtl8169/ip1000/AHCI, `AC_LOWER4G` в 32-битных мастерах),
   аппаратные структуры (`prdte`, `lance_init_block`, VMMDev), PCI BAR 64, `mmio.h` и
   `hcd_reg4` (до порта aarch64), псевдогранты-указатели.

Тихие ошибки, которые стоит исправить раньше своего шага, потому что они дешёвые и не
зависят от решений: `u64.h` (`ex64lo` → `uint32_t`), `libfsdriver/call.c:241`
(`data.ptr = buf`), `iovec_s_t`/`iovec_t` в libbdev↔libblockdriver, `int`↔`ssize_t` в
прототипах, `kinfo.vm_allocated_bytes` (`int`). **Исправлено 2026-10-08**
(`work-plan.md` §4.2): `u64.h`, `int`↔`ssize_t` в прототипах (getdents, ipc, ext2,
libfsdriver, memory/fbd/vnd/mmcblk), `kinfo.vm_allocated_bytes` → `size_t`;
`call.c:241` и `iovec_s_t`/`iovec_t` — в А1.4.

## 9. Открытые вопросы

1. **Тип физического адреса — решено 2026-10-07: схема типов памяти (§9а).**
   Раньше физический адрес ходил через 9 разных типов (`phys_bytes`, `phys_clicks`,
   `vir_bytes`, `void *`, `long`, `int`, `u32_t`, `unsigned long`, `uint64_t` в
   multiboot); единый `uint64_t` везде отвергнут в пользу ширины по платформе с
   опцией для PAE/LPAE.
2. **Сообщения IPC на 64 битах — решено 2026-10-07:** сообщение остаётся 64 байта
   (56 — полезная нагрузка), поля фиксированной ширины, одна раскладка на всех
   архитектурах. Правила, расчёт и список затронутых типов — `docs/messages.md`
   §14а. Отвергнуто: сообщение 128 байт (вдвое больше копирования на каждом IPC,
   две кэш-строки, не помещается в регистры для будущего быстрого пути IPC, разные
   раскладки на 32 и 64 битах; узкие поля и смешение физических и виртуальных
   адресов всё равно пришлось бы исправлять).
3. **Ширина `reg_t` и ABI ядра — решено 2026-10-07:** `reg_t` = `register_t`
   платформы, делается сейчас, в составе А1 (§9а). Затрагивает `stackframe.h`,
   `sigcontext`, `mcontext`, ptrace (`T_GETUSER`, ширина слова).
4. **Счётчик тиков** — перейти на 64-битный `clock_t`/uptime в ядре (`kclockinfo` уже
   резервирует место) или оставить 32 бита с переполнением?
5. **DMA ниже 4 ГБ** — флаг `alloc_contig` (`AC_LOWER4G`) и зона VM, bounce-буферы или
   отказ драйвера? При IOMMU — трансляция «физический адрес → адрес шины»
   (`vm_1phys2bus` в rtl8139 — заглушка; `dma-ranges` в DTS на ARM).
6. **VirtualBox на 64-битном госте** — `VMMDevReq_HGCMCall64` или упаковка параметров до
   12 байт с Call32; раскладка shared folders — сверить с версией хоста.
7. **Драйверы без будущего** — `amddev` (AMD DEV, не IOMMU), ISA-сетевые (dp8390,
   dpeth, lance), floppy: исправлять или удалить при пересмотре драйверов.

## 9а. Схема типов памяти (решение владельца 2026-10-07)

Один тип — одно понятие. Решение закрывает §9.1 и §9.3, `memory.md` §10 вопрос 7;
реализуется шагом А1 (`modernization.md`). Раскладка сообщений (§9.2) решается
отдельно, но по правилу «внешние раскладки — фиксированная ширина» (ниже).

**Адреса.**

| Тип | Смысл | Ширина | Заменяет |
|---|---|---|---|
| `phys_addr_t` | физический адрес — RAM и регистров устройств | слово платформы; 64 бита при `CONFIG_PHYS_ADDR_64` (PAE/LPAE, по умолчанию выключена на 32-битных платформах) | `phys_bytes` в роли адреса; `void *`, `long`, `int`, `u32_t`, `vir_bytes` с физическим адресом |
| `vir_addr_t` | виртуальный адрес | слово платформы | `vir_bytes` в роли адреса |
| `dma_addr_t` | адрес буфера в RAM, как его видит устройство (адрес шины) | как `phys_addr_t` | физический адрес из `sys_umap`/`vumap_phys`, записываемый в устройство |
| `pfn_t` | номер физической страницы | слово платформы | `phys_clicks`, `int`/`unsigned`-номера страниц в VM |

- Код обязан быть корректен при обеих ширинах `phys_addr_t`: печать через
  `PRIxPHYS`/`PRIxVIR`/`PRIxDMA`, никаких приведений адреса к `u32_t`/`long`/указателю.
  Проверка — режим p64 прохода `scan64.py` (§2.1) должен быть чистым.
- **Форматы и `-Wformat`.** `vir_addr_t` = `uintptr_t` — на i386 это `unsigned int`, а
  нынешний `vir_bytes` — `unsigned long`. Все `%lx`/`%lu` для бывших `vir_bytes`
  при переключении становятся ошибками `-Wformat` (ядро собирается с `-Werror`;
  урок отменённой попытки 2026-10-07). Поэтому макросы `PRIxVIR`/`PRIxPHYS`/
  `PRIxDMA` вводятся первыми (А1.0) со значениями под нынешние типы, форматы
  переводятся на них при неизменном машинном коде (А1.1), а определения
  переключаются в А1.6 — тогда меняется только значение макросов.
- `dma_addr_t` получается из `phys_addr_t` **только** функцией `phys_to_dma()`: сейчас
  тождество, позже — `dma-ranges` из FDT или отображение в IOMMU. В устройство пишется
  только `dma_addr_t`.
- **Регистры устройств:** отображаются по `phys_addr_t` (`vm_map_phys` принимает
  `phys_addr_t` вместо `void *`), доступ — только через `volatile`-указатель на
  отображение. Так MMIO и DMA в коде различимы.

**Размеры и смещения.** Размеры и смещения в памяти (длины буферов, смещения в объекте
и в гранте, размеры структур) — `size_t`. Смещение в файле или на устройстве — `off_t`
(64 бита и на 32-битных платформах); «размер или ошибка» — `ssize_t`. **Внешние
раскладки** — сообщения IPC, `kinfo`, общие структуры ядра и VM, журнал APT, дисковые
форматы, структуры устройств — только типы фиксированной ширины (`uint32_t`,
`uint64_t`), чтобы раскладка не зависела от платформы.

**Регистр:** `reg_t` = `register_t` платформы. Указатели и адреса через `reg_t` не
передаются (аргументы IPC, `p_cr3`, стек, кадр сигнала получают `vir_addr_t` или
`phys_addr_t`).

**Страницы:** `CLICK_SIZE`/`CLICK_SHIFT` заменяются на `PAGE_SIZE`/`PAGE_SHIFT`.

**Таблицы страниц:** `pte_t`/`pde_t` — только внутри `minix/kernel/arch/i386/` (32 бита,
64 при PAE); за пределами архитектурного слоя не видны. У других архитектур свои
типы внутри их `arch/`.

**Адресация в ядре.** `struct vir_addr` и неиспользуемые `vir_cp_req`/`phys_cp_req`
удаляются; особого значения `NONE` = «физический адрес» больше нет. Виртуальный адрес
всегда передаётся парой «endpoint + `vir_addr_t`»; физический путь — отдельными
функциями ядра (`copy_vir_vir`, `copy_phys_vir`, `copy_vir_phys`, `copy_phys_phys`),
отдельными kernel calls и отдельным правом: `SYS_VIRCOPY` больше не даёт доступа к
физической памяти (сейчас даёт — `do_copy.c` обслуживает оба вызова одним кодом,
`vm/pagetable.c:1266` этим пользуется). В libsys `sys_physcopy(NONE, …)`, `sys_abscopy`
и `sys_memset(NONE, …)` заменяются явными `sys_copy_phys_*` и `sys_memset_phys`.

**Псевдогранты запрещены.** Приём «указатель в поле `cp_grant_id_t` при `endpt ==
SELF`/`KERNEL`» (и обратный — номер гранта в поле адреса) считается ошибкой
программирования. `cp_grant_id_t` хранит только номер гранта. Где нужен «либо чужой
буфер по гранту, либо свой», используется явный тип в интерфейсах libfsdriver и
libblockdriver:

```c
struct buf_ref {
	endpoint_t endpt;		/* SELF — свой буфер */
	union { cp_grant_id_t grant; void *ptr; };
};
```

`iovec_t` (адреса) и `iovec_s_t` (гранты) используются строго по назначению. Места —
§11.2–11.4 (tty, pty, rs232, vnd, floppy, libfsdriver `call.c:241`, `memory.c:449`,
tda19988, cat24c256, `do_umap_remote.c:64`, соглашение `SELF` в `bdr_transfer`).

**Удаляются:** `phys_bytes`, `vir_bytes`, `phys_clicks`, `vir_clicks`,
`struct vir_addr`, `vir_cp_req`/`phys_cp_req`, `CLICK_*`, псевдогранты.

**Минимальный x86** (решение 2026-10-07): Pentium M (i686, одно ядро, PAE не
гарантирован — Banias и ранние Dothan не выставляют флаг в CPUID). Поэтому PAE на i386
— только опция (`CONFIG_PHYS_ADDR_64` + транслятор PAE после А7), базовый транслятор —
двухуровневый с 32-битными записями.

## 10. Как повторить проверку

- Раскладки (быстро, без сборки системы):
  `make -C docker -f build.mk host-test HOST_TESTS=abi64` — на m32 всё `ok`, на m64 три
  `# TODO`; новое сломанное сообщение или внешняя структура — `FAIL`.
- Полный проход clang (нужны собранные `obj/i386` и `obj/i386-smp` — `hdimage` и
  `hdimage SMP=yes`, Docker-образ `reminix-build`, clang ≥ 18 на хосте):
  `minix/tests/host/abi64/scan64.py [--modes m32,m64,p64] [--no-layouts]` — около
  15 минут на 12 потоках; результат в `obj/abi64-scan/` (`new-m64.txt`,
  `new-p64.txt`, `ldiff-*.txt`, `summary.txt`). После шага А1 режим p64 должен стать
  чистым; после шага MSG и правок amd64 — режим m64.

## 11. Места по зонам

Формат строк: `Файл:строка | Что | Кат. | Почему проблема | Шаг | Проявл. | Сл.`
Категории и шаги — §4; «Проявл.»: `сборка` — ошибка компиляции, `64` — тихая ошибка в
64-битном режиме, `>4Г` — только при физ. адресах выше 4 ГБ (и на 64, и на PAE/LPAE),
`оба`; «Сл.»: S — локальная правка, M — несколько файлов или формат данных, L —
переработка интерфейса.

### 11.1 Граница ядро–серверы, ядро, ассемблер, VM

#### Граница ядро–серверы–процессы (minix/include)

| Файл:строка | Что | Кат. | Почему проблема | Шаг | Проявл. | Сл. |
|---|---|---|---|---|---|---|
| minix/include/minix/type.h:19,21 | `vir_clicks`, `phys_clicks` = `unsigned int` | PHYS | номер страницы в 32 битах: потолок 16 ТБ физ. памяти на 4К-страницах — на ближайшие порты достаточно, но `phys_bytes`↔`phys_clicks` смешиваются без явных приведений (сдвиг `<< CLICK_SHIFT` в 32 битах — см. `CLICK2ABS`) | А1 | >4Г | M |
| minix/include/minix/type.h:20 | `phys_bytes` = `unsigned long` | PHYS | ширина физ. адреса привязана к ширине `long`: на i386/earm — 32 бита (нет PAE/LPAE, нет памяти > 4 ГБ), на LP64 — 64. Вопрос о целевом типе открыт (см. «Открытые вопросы») | А1 | >4Г | L |
| minix/include/minix/type.h:24 | `vir_bytes` = `unsigned long` | VIR | на LP64 сам станет 64-битным (это правильно); но в сообщениях, общей памяти и `kinfo` он задаёт раскладку, зависящую от ширины (см. MSG) | MSG | 64 | — |
| minix/include/minix/type.h:40-53 | `struct vumap_vir`/`vumap_phys`: `vir_bytes`/`phys_bytes`/`size_t` | VIR, HW | `vumap_phys` используется драйвером AHCI как аппаратная PRD-таблица (`prd_t`, ahci.h:258) — на 64 битах раскладка 16 байт с другими смещениями, см. §11.4 | драйв. | 64 | M |
| minix/include/minix/type.h:104-119 | `struct kclockinfo`: `uptime`, `realtime` — `clock_t` (32 бита) с «резервом для 64-битного uptime» | TIME | 32-битный счётчик тиков переполняется (при 60–1000 Гц — за 49–828 сут.); резерв не используется | amd64 | оба | M |
| minix/include/minix/type.h:127 | `struct machine.acpi_rsdp` — `phys_bytes` | PHYS | адрес RSDP в 32 битах на ILP32 (на UEFI-машинах RSDP может лежать выше 4 ГБ) | А1 | >4Г | S |
| minix/include/minix/type.h:148-160 | `struct boot_image.start_addr/len`, `struct memory.base/size` — `phys_bytes` | PHYS | физ. адреса загрузочных модулей и карта памяти в 32 битах на ILP32 | А1/А2 | >4Г | S |
| minix/include/minix/type.h:197-201 | `struct arm_frclock.tcrr` — адрес регистра таймера в `u32_t` | PTR, HW | MMIO-адрес в 32 битах — на aarch64 устройства часто выше 4 ГБ | amd64 (aarch64) | 64 | S |
| minix/include/minix/type.h:214-244 | `struct minix_kerninfo` — «ABI-restricted», указатели на `kinfo`, `machine`, … | LAYOUT | раскладка, на которую опираются бинарники пользователя, на LP64 другая (56→88 байт); старая совместимость не нужна, но `kinfo.user_sp` «по смещению 2440» упоминается как легаси-ABI | amd64 | 64 | S |
| minix/include/minix/param.h:14-45 | `kinfo_t` содержит `multiboot_info_t`, `multiboot_module_t[]` с `char *`/`vaddr_t` | LAYOUT | ядро копирует структуры multiboot в `kinfo`; на 64 битах раскладка multiboot неверна (см. abi64: `multiboot_info` 120→160 байт, все поля после `mi_cmdline` сдвинуты; `multiboot_mmap.mm_base_addr` 4→8; `multiboot_module` 16→24) | Б2/В4 | 64 | M |
| minix/include/minix/param.h:43-45 | `kinfo.vm_allocated_bytes`, `kernel_allocated_bytes(_dynamic)` — `int` | VIR | счётчик байтов в `int` (2 ГБ) | А2 | оба | S |
| sys/arch/i386/include/multiboot.h:104-160,212-229 | `struct multiboot_info`/`mmap`/`module` с `char *`, `void *`, `vaddr_t`, неупакованный `uint64_t` | LAYOUT | формат задан спецификацией Multiboot 0.6.96 (упакованный, 32-битные адреса); на x86_64 все смещения после первого указателя сдвигаются, `mm_base_addr` уходит со смещения 4 на 8 — 64-битное ядро прочитает карту памяти и модули неверно (abi64: `external_layouts_known_bugs`) | В4 | 64 | S |
| minix/include/minix/u64.h:10-22 | `ex64lo()`/`ex64hi()`/`make64()` возвращают/принимают `unsigned long` | CONST | на LP64 `ex64lo(x)` вернёт **всё** 64-битное значение, а не младшие 32 бита — вызовы, раскладывающие 64-битное значение на половины (MSR, регистры, вывод `%lu:%lu`), молча ошибочны | amd64 | 64 | S |
| minix/include/arch/i386/include/stackframe.h:6 | `typedef unsigned reg_t` (earm: `u32_t`) | ABI | «машинный регистр» 32 бита: через него идут указатели — аргументы IPC (`do_ipc(r1,r2,r3)`, `proc.c:683,701`), `p_reg.sp/pc`, кадр сигнала, `get_stack_frame()`, `p_cr3`. На 64 битах всё обрезается | amd64 | 64 | M |
| minix/include/arch/i386/include/archtypes.h:33-34 | `segframe.p_cr3` (`reg_t`), `p_cr3_v` (`u32_t *` — 32-битные PTE) | PHYS, ARCH | корень таблицы страниц в 32 битах, PTE как `u32_t` (без PAE) | А7 | оба | M |
| minix/include/arch/i386/include/archtypes.h:48 | `atomic_t` = `u32_t` | — | 32-битная атомарность достаточна и на 64; заметка для А4 | А4 | — | — |
| minix/include/arch/i386/include/vm.h:18-19,65; earm/include/vm.h:58-59,105,178 | `I386_VM_ADDR_MASK 0xFFFFF000`, `ARM_VM_ADDR_MASK`, `ARM_TTBR_ADDR_MASK` | CONST, PHYS | маски физ. адреса на 32 бита (формат PTE без PAE/LPAE); используются и в VM (`vm/arch`) | А7 | >4Г | M |
| minix/include/minix/const.h:83-88 | `CLICK_SIZE`/`CLICK_SHIFT` только для `__i386__`/`__arm__`, иначе `#error Unsupported arch` | ARCH | первая ошибка сборки любой новой архитектуры (весь код, включающий `<minix/const.h>`) | amd64 | сборка | S |
| minix/include/minix/const.h:91-92,101 | `CLICK_FLOOR/CEIL` через `(vir_bytes)`, `CLICK2ABS(v) ((v) << CLICK_SHIFT)` | PHYS | округление физ. адреса через `vir_bytes`; сдвиг 32-битного `phys_clicks` переполняется для адресов ≥ 4 ГБ | А1 | >4Г | S |

#### Ядро: общий код (minix/kernel)

| Файл:строка | Что | Кат. | Почему проблема | Шаг | Проявл. | Сл. |
|---|---|---|---|---|---|---|
| minix/kernel/arch/i386/include/arch_smp.h:11-12 | `cpuid` = `((u32_t *)(((u32_t)get_stack_frame() + K_STACK_SIZE-1) & ~...))[-1]` | PTR | номер CPU вычисляется из адреса стека через `u32_t`; на 64 битах (ядро в верхней половине АП) адрес обрезается — разыменование падает. Через этот макрос идут `get_cpulocal_var()` и все данные «на CPU» SMP-ядра: 236 мест прохода clang (`clock.c`, `main.c`, `proc.c`, `system.c`, `memory.c`, `apic.c`, `arch_clock.c`, `smp.c`, …) | А4 | 64 | M |
| minix/kernel/arch/i386/include/arch_proto.h:241,244 | `get_stack_frame()` → `(reg_t)` | PTR | указатель кадра в 32-битном `reg_t` | А4 | 64 | S |
| minix/kernel/proc.c:683,701 | `(message *) r3`, `(asynmsg_t *) r3` — указатель из `reg_t` | PTR, ABI | адрес сообщения пользователя приходит в 32-битном `reg_t` | amd64 | 64 | S |
| minix/kernel/proc.c:710 | `arch_set_secondary_ipc_return(caller, minix_kerninfo_user)` — `vir_bytes` → `u32_t` | PTR | адрес `minix_kerninfo` в пространстве пользователя возвращается в 32-битном регистре | amd64 | 64 | S |
| minix/kernel/smp.c:14-19,207,294 | `sched_ipi_data[cpu].data` — `u32_t`, хранит `struct proc *` | PTR | указатель процесса в 32-битном поле при межпроцессорном IPI | А4 | 64 | S |
| minix/kernel/smp.c:77-80,99,115 | отладка BKL: обход кадров стека и `__builtin_return_address` через `u32_t` | PTR | отладочный код (`bkl_lock`), обрезка адресов | А5 | 64 | S |
| minix/kernel/system/do_sigsend.c:72,74,134 | `new_fp = (reg_t) &frp->sf_fp`, `fr.sf_ra_sigreturn` (`vir_bytes`→32), `p_reg.sp = (reg_t) frp` | PTR, ABI | кадр сигнала в стеке пользователя строится из 32-битных полей (`struct sigframe_sigcontext` i386) | amd64 | 64 | M |
| minix/kernel/system/do_vmctl.c:60-69 | `SVMCTL_MRG_ADDR/LENGTH` ← `vir_bytes`; `SVMCTL_MRG_REQUESTOR = (void *) endpoint` | MSG | адрес/длина в 32-битных полях сообщения `m_lsys_krn_sys_vmctl`; endpoint, упакованный в указатель | MSG | 64 | S |
| minix/kernel/system/do_settime.c:31,39-53 | `ticks = sec * system_hz` в 32 бита; `timediff_ticks` (`long`) → `clock_t` | TIME | переполнение тиков; на LP64 проверка `LONG_MIN/2..LONG_MAX/2` перестаёт отсекать значения, не влезающие в 32-битный `clock_t` | amd64 | оба | S |
| minix/kernel/system/do_setalarm.c:61; do_vtimer.c:37,64 | `long` → `clock_t`/`int` из полей сообщения (`VT_VALUE`, `VT_ENDPT` — `m2_l*`) | TIME, MSG | `long`-поля сообщения (`m2_l1`…) на LP64 8 байт; время в 32-битных тиках | MSG | 64 | S |
| minix/kernel/system/do_trace.c:146-154 | `(int) &((struct proc *) 0)->p_reg.cs` — смещения как `int` | PTR | безопасно (смещение мало), но это самодельный `offsetof` через приведение указателя | amd64 | — | S |
| minix/kernel/system.c:718; do_fork.c:84; do_memset.c:22 | `size_t`/`unsigned long` → `int` | VIR | безопасные сужения (длина фильтра IPC, имя процесса, шаблон memset) | — | — | — |
| minix/kernel/arch/i386/memory.c:189-190,237-238,566,661 | линейные адреса как `phys_bytes`, физ. адреса как `vir_bytes` (`createpde(…, phys_bytes *chunk)`, `lin_lin_copy`, `vm_suspend(…, ph/lin, …)`) | PHYS | ядро смешивает типы линейных и физических адресов; при 64-битном `phys_bytes` на ILP32 — обрезка и несовпадение указателей (`vir_bytes *` ↔ `phys_bytes *`) | А1 | >4Г | M |
| minix/kernel/arch/i386/memory.c:296,575,830 | `printf("%lx", phys_bytes)` | FMT | формат перестанет совпадать при 64-битном `phys_bytes` на ILP32 | А1 | >4Г | S |

#### Ядро: arch/i386 (minix/kernel/arch/i386)

Архитектурный слой i386 при порте amd64 пишется заново (GDT/IDT/TSS длинного режима,
`syscall`/`sysret` вместо `sysenter`, 4-уровневые таблицы страниц, другой кадр
исключения, 8-байтовые слоты стека). Ниже — классы мест; переносимые на amd64 части
(ACPI, IOAPIC, LAPIC, TSC, watchdog) — по отдельности.

| Файл:строка | Что | Кат. | Почему проблема | Шаг | Проявл. | Сл. |
|---|---|---|---|---|---|---|
| minix/kernel/arch/i386/protect.c:36,167,184-191,219,235,347,349 | базы GDT/IDT/TSS, `SYSENTER_EIP`, `set_star_cpu`, `sp0` — через `(u32_t)`/`(unsigned)` | ARCH, PTR | дескрипторы и MSR i386; в длинном режиме другие форматы (16-байтовые шлюзы IDT, 64-битный TSS) | amd64 | 64 | L |
| minix/kernel/arch/i386/protect.c:424,458-459 | загрузка модулей: `execi.hdr = (char *) mod->mod_start` (32-битный физ. адрес как указатель), `stack_high - sizeof(ps_strings)` в `u32_t` | PTR, PHYS | модули multiboot адресуются 32-битными физ. адресами, прямое отображение 1:1 | В4 | оба | M |
| minix/kernel/arch/i386/exception.c:80-86,206-228,307,311 | `frame->eip`, `p_reg.pc` ↔ адреса функций через `reg_t`; разбор кадров стека `v_bp` | ARCH, PTR | кадр исключения i386 (`eip`), 32-битный `reg_t` | amd64 | 64 | M |
| minix/kernel/arch/i386/arch_system.c:67; pg_utils.c:217,227 | `u32_t cr4 = read_cr4()` | ARCH | CR4 64-битный в длинном режиме (биты выше 31 зарезервированы — безопасно, но тип неверный) | amd64 | — | S |
| minix/kernel/arch/i386/arch_system.c:503,507 | `stk = (char *)tss[0].sp0`, `*((reg_t *)stk) = (reg_t) p` | PTR | указатель `struct proc *` кладётся в 32-битный слот вершины стека ядра (на него опирается и `cpuid`) | А4 | 64 | S |
| minix/kernel/arch/i386/include/archconst.h:149; sconst.h:11,81; mpx.S, klib.S (повсеместно) | `X86_STACK_TOP_RESERVED (2 * sizeof(reg_t))`, `CURR_PROC_PTR 20`, смещения `8*4(%esp)`, `10*4(%esp)`, «P_STACKBASE == 0» | ASM | ассемблер рассчитан на 4-байтовые слоты и на `p_reg` со смещением 0 в `struct proc`; смещения `struct proc` генерируются (`procoffsets.cf` + genassym) и от ширины не зависят, а ручные — зависят | amd64 | 64 | L |
| minix/kernel/arch/i386/procoffsets.cf | смещения `p_reg.*`, `p_seg.p_cr3`, `p_magic` | ASM | генерируются для текущей раскладки — при amd64 нужен свой `procoffsets.cf` (регистры r8–r15, 64-битный `p_cr3`) | amd64 | — | S |
| minix/kernel/arch/i386/pg_utils.c:182,189,193,294,314 | ранние таблицы страниц: `pagedir[i] = phys \| flags` (`u32_t`), `u32_t mapped, kern_phys`, `I386_VM_PDE(vaddr)` | PHYS, ARCH | двухуровневый формат без PAE; физ. адреса ядра в `u32_t` | А2/А7 | оба | M |
| minix/kernel/arch/i386/pg_utils.c:14-16 | инициализаторы `kern_vir_start = (phys_bytes) &_kern_vir_base` — при 64-битном `phys_bytes` на i386 не константа | ERR, PHYS | ошибка сборки при `phys_bytes` = `uint64_t` на ILP32 (p64) | А1 | сборка | S |
| minix/kernel/arch/i386/memory.c:338-359 | `vm_lookup`: `root = (u32_t *) p_cr3`, `phys_get32((u32_t)(root + pde))`, `pt = (u32_t *) I386_VM_PFA(pde_v)` | PHYS, ARCH | обход двухуровневой таблицы i386 с физ. адресами в `u32_t`; переходит в транслятор журнала (М4) | А7 | оба | M |
| minix/kernel/arch/i386/memory.c:25,754-755,802-803,852,874-913 | `video_mem_vaddr` (`phys_bytes`), `usermapped_*` и `ASSIGN`/`FIXPTR`/`FIXEDPTR` — указатели через `(u32_t)`; `lapic_addr_vaddr` (`u32_t`) | PTR | указатели структур, отображаемых в пространство пользователя (`minix_kerninfo`, `ipcvecs`), пересчитываются в 32-битной арифметике | amd64 | 64 | S |
| minix/kernel/arch/i386/apic.h:122; apic.c:179-205,229-230,898,1247 | `lapic_addr_vaddr` — `u32_t`; `ioapic_read/write(u32_t ioa_base, …)`, `(u32_t)ioapic_addr` | PTR, HW | MMIO-адреса LAPIC/IOAPIC в 32 битах (в 64-битном ядре — виртуальные адреса верхней половины) | amd64 | 64 | S |
| minix/kernel/arch/i386/apic.c:481; arch_clock.c:148 | `cpu_info[cpu].freq = (unsigned long)(cpu_freq / 1000000)` в `u32_t`; `tsc_per_ms` `unsigned int` | TIME | частоты в МГц/тактах на мс — влезают; безопасно | — | — | — |
| minix/kernel/arch/i386/arch_watchdog.c:38,50,202,215 | `ia32_msr_write(…, ex64hi(v), ex64lo(v))` | CONST | на LP64 `ex64lo` возвращает всё значение (см. `u64.h`); `u32_t`-параметр спасает обрезкой — работает случайно | amd64 | 64 | S |
| minix/kernel/arch/i386/arch_smp.c:66,74,79 | `(u32_t) vaddr - (u32_t) &trampoline + trampoline_base`, `(unsigned)&trampoline` | PTR | трамплин запуска AP: адреса в 32 битах (трамплин должен лежать ниже 1 МБ — для реального режима это верно и на amd64, но расчёт через `u32_t` от адреса ядра — нет) | amd64 | 64 | S |
| minix/kernel/arch/i386/acpi.h:12,99,131; acpi.c:59,122-132,241-249,320,351-374 | только RSDT (`rsdt_addr` `u32_t`), `acpi_phys2vir(u32_t)`, `local_apic_address`/`address` в `u32_t`, `(phys_bytes) fadt->dsdt` | PHYS, HW | XSDT (64-битные указатели ACPI 2.0+) не поддерживается; таблицы и `X_DSDT` выше 4 ГБ недоступны; на UEFI-машинах RSDT может отсутствовать; запись MADT «Local APIC Address Override» (64 бит) игнорируется | amd64 | оба | M |
| minix/kernel/arch/i386/do_sdevio.c:71; do_readbios.c | адрес вектора в `phys_bytes`, BIOS-область | PHYS | `sys_sdevio` берёт буфер как физ./вирт. адрес в 32-битном поле сообщения | MSG | оба | S |

#### Ядро: arch/earm (minix/kernel/arch/earm) — в автоматический проход не попал, разобран вручную

| Файл:строка | Что | Кат. | Почему проблема | Шаг | Проявл. | Сл. |
|---|---|---|---|---|---|---|
| minix/kernel/arch/earm/pg_utils.c:29,49 | `phys_bytes addr = memmap[m].mm_base_addr` (`uint64_t` → 32-битный `phys_bytes`) | PHYS | молча обрезает адрес банка памяти; при LPAE/памяти выше 4 ГБ — неверная карта | А2/Б2 | >4Г | S |
| minix/kernel/arch/earm/pre_init.c:196,207,209,381-382 | `mbi->mods_addr = (u32_t)&mb_modlist`, `cmdline = (u32_t) bootargs`, `(u32_t)&_end - (u32_t)&_edata` | PTR | earm заполняет multiboot-подобную `mbi` 32-битными указателями — на aarch64 не годится; заменяется разбором FDT | Б2 | 64 | M |
| minix/kernel/arch/earm/pre_init.c:30; protect.c:24-30 | `vir2phys(void *)` → `(phys_bytes)addr` (1:1 до включения MMU) | PHYS | указатель → физ. адрес через приведение | Б2 | — | S |
| minix/kernel/arch/earm/memory.c:315-337 | `vm_lookup`: `root = (u32_t *)(p_ttbr & ARM_TTBR_ADDR_MASK)`, `phys_get32((u32_t)(root + pde))` | PHYS, ARCH | обход short-descriptor таблиц ARMv7 в `u32_t`; уходит в транслятор (В1) | В1 | оба | M |
| minix/kernel/arch/earm/memory.c:655-716 | `usermapped`/`FIXEDPTR(ptr) (void *)((u32_t)ptr + usermapped_offset)` | PTR | как на i386 | amd64 (aarch64) | 64 | S |
| minix/kernel/arch/earm/protect.c:42,80; arch_system.c:162; exception.c:66,85,154 | `(unsigned) kernel_stack`, `write_vbar((reg_t)&exc_vector_table)`, `svc_stack = (reg_t)stk`, `printf("pc = 0x%x", …)` | PTR, FMT, ARCH | 32-битные регистры и адреса (на aarch64 — другие механизмы исключений: VBAR_EL1, SP_EL1) | amd64 (aarch64) | 64 | M |
| minix/kernel/arch/earm/arch_do_vmctl.c:46 | `set_ttbr(p, SVMCTL_PTROOT, (u32_t *) SVMCTL_PTROOT_V)` | PTR, MSG | корень таблицы и его виртуальный адрес из 32-битных полей сообщения | А7 | оба | S |
| minix/kernel/arch/earm/include/archconst.h:49 | `PG_ALLOCATEME ((phys_bytes)-1)` | CONST | маркер «выделить» = все единицы `phys_bytes` — меняется вместе с типом | А2 | — | S |
| minix/kernel/arch/earm/bsp/ti/*.c (omap_timer, omap_serial, omap_intr, omap_rtc, omap_reset) | `vir_bytes base` = физ. адрес MMIO, жёстко заданные адреса TI | PHYS, HW | физ. адрес устройства в `vir_bytes`, затем перезаписывается виртуальным — смешение типов; адреса из FDT заменят константы | Б1 | — | M |
| minix/kernel/arch/earm/bsp/ti/omap_timer.c:312-314 | `0xffffffff - (32768 / freq) + 1` | CONST | 32-битный счётчик таймера — аппаратный, корректно | — | — | — |
| minix/kernel/arch/earm/arch_clock.c:223 | `load = ex64lo(busy / tsc_delta)` | CONST | см. `u64.h` | amd64 (aarch64) | 64 | S |

#### Ядро: прочие интерфейсы (minix/kernel/*.h)

| Файл:строка | Что | Кат. | Почему проблема | Шаг | Проявл. | Сл. |
|---|---|---|---|---|---|---|
| minix/kernel/proto.h:217 | `arch_proc_init(struct proc *, u32_t pc, u32_t sp, u32_t ps_str, char *)` | PTR | точка входа, стек и `ps_strings` загрузочных процессов в 32 битах | amd64 | 64 | S |
| minix/kernel/proto.h:175 | `arch_set_secondary_ipc_return(struct proc *, u32_t val)` | PTR | второй возвращаемый регистр IPC — 32 бита (через него отдаётся адрес `minix_kerninfo`) | amd64 | 64 | S |
| minix/kernel/proc.h:37,40; priv.h:38; type.h:12 | `bitchunk_t` (`u32_t`) — маски CPU, `p_stale_tlb`, вызовов ядра, `sys_map` | BITS | от ширины не зависят (порции по 32 бита, `bitmap.h` исправлен в §4.16) | — | — | — |
| minix/kernel/proc.h:86,114; priv.h:28,61,64; profile.h:17 | `p_delivermsg_vir`, `p_vmrequest…start/length`, `s_asyntab`, `s_grant_table`, `s_state_table`, `sprof_data_addr_vir` — `vir_bytes` | VIR | корректно расширяются; на границе уходят в поля сообщений (см. MSG) | MSG | 64 | — |

#### VM (minix/servers/vm)

| Файл:строка | Что | Кат. | Почему проблема | Шаг | Проявл. | Сл. |
|---|---|---|---|---|---|---|
| minix/servers/vm/alloc.c:33-51 | `NUMBER_PHYSICAL_PAGES (int)(0x100000000ULL/VM_PAGE_SIZE)`, битовая карта и `pagemap[]` на 4 ГБ | PHYS, CONST | физ. память выше 4 ГБ не учитывается; таблица на 1 М страниц статически | А3 | >4Г | L (уходит вместе с кодом) |
| minix/servers/vm/alloc.c:43-44,166,261,330,369,404-475,511-528; proto.h:32,39,98,113,115 | номера и число страниц в `int`/`u32_t`, `alloc_mem(phys_clicks)`, `free_mem(phys_clicks)`, `mem_add_total_pages(int)`, `vm_freepages(vir_bytes, int)`; `phys_bytes`→`phys_clicks` без приведения (`free_mem(ABS2CLICK(…))`) | PHYS | смешение адресов и номеров страниц; сужение 64→32 (p64, m64) | А1/А3 | >4Г | M |
| minix/servers/vm/alloc.c:414-416 | `maxpage = boundary16 - 1` / `boundary1 - 1` (16 МБ / 1 МБ для ISA DMA) из `phys_bytes` в `int` | PHYS | безопасно по величине; зоны DMA — в buddy (М3, §4.4 memory.md) | А3 | — | S |
| minix/servers/vm/alloc.c:470 | `sys_memset(NONE, 0xa5a5a5a5, VM_PAGE_SIZE * pageno, …)` | PHYS | физ. адрес как `VM_PAGE_SIZE * int` — переполнение `int` при адресах ≥ 2 ГБ (уже сейчас, в отладочной ветке) | А3 | оба | S |
| minix/servers/vm/region.h:31 | `phys_block.refcount` — `u8_t` | — | ≤ 255 ссылок на страницу (известно, memory.md §1.6) | А1 | оба | S |
| minix/servers/vm/region.h:41,59; proto.h:181,83 | `u16_t flags`, `clearend`; `map_get_ref(…, u8_t *cnt)`, `do_get_refcount` | VIR | `refcount` наружу (`VM_GETREF`) тоже `u8_t` — меняется вместе с ним | А1 | — | S |
| minix/servers/vm/pt.h:13-24 | `pt_t`: `u32_t *pt_dir`, `u32_t pt_dir_phys`, `u32_t *pt_pt[ARCH_VM_DIR_ENTRIES]`, `u32_t pt_virtop` | PHYS, PTR, ARCH | двухуровневая таблица, 32-битные PTE и физ. адрес каталога; `pt_virtop` — виртуальный адрес в `u32_t` (известно) | А7 | оба | L (уходит) |
| minix/servers/vm/pagetable.c:40-42,73,155-223,339,408,494-499,546,587,763-1361 | вся работа с PTE на `u32_t`; `findhole()`: поиск свободного места в АП самого VM в `u32_t` (`curv = (u32_t) lastv`); `pdes = (void *)(pagedir_pde*ARCH_BIG_PAGE_SIZE + …)` (`int`); `ARCH_VM_PDE(v)` → `int` | PTR, PHYS, ARCH | формат PTE i386/ARMv7 и адреса VM в 32 битах | А7 | оба | L (уходит) |
| minix/servers/vm/pagetable.c:1011,1097-1103,1179,1279,1361,1459,1490 | `pt_dir_phys`, `mypdbr`, `myttbr`, `kern_mappings[i].phys/len` — `u32_t`/`phys_bytes` в PTE | PHYS | физ. адреса отображений ядра в 32-битных PTE | А7 | >4Г | M |
| minix/servers/vm/vm.h:32,61 | `AM_AUTO ((u32_t) -1)`, `MAP_NONE 0xFFFFFFFE` | CONST | «нет физ. адреса» как 32-битные константы, сравниваются с `phys_bytes` — при 64-битном `phys_bytes` перестанут совпадать с `(phys_bytes)-1` | А1 | оба | S |
| minix/servers/vm/vm.h:65-86 | `VM_DATATOP = kinfo.user_end`, `VM_OWN_MMAPBASE = HEAPBASE + 1 ГБ`, `+100 МБ` | CONST | раскладка АП задаётся ядром через `kinfo`; размеры влезают и на 64 — заметка | amd64 | — | — |
| minix/servers/vm/main.c:299-301,358,489,495,519 | `ip->start_addr + ip->len` (`phys_bytes`) → `vir_bytes`/`int`; `mem_add_total_pages(len/VM_PAGE_SIZE)` | PHYS | загрузочные модули: физ. адреса в 32-битных переменных и `int`-числе страниц | А1/А2 | >4Г | S |
| minix/servers/vm/main.c:410 | `vsp + ((int)psp - (int)frame)` | PTR | разность указателей через `int` при сборке стека загрузочного процесса (`ps_strings`) — на 64 битах обрезка | amd64 | 64 | S |
| minix/servers/vm/mem_cache.c:131,150,161,221-225 | смещение блока кэша как `phys_bytes`, `(vir_bytes) msg->m_vmmcp.block + offset` | PHYS, MSG | физ. и вирт. типы перепутаны; `block` — указатель в сообщении | А1 | оба | S |
| minix/servers/vm/mem_anon_contig.c:68-86; region.c:54,66,79,421,871; pb.c:57,159; mem_anon.c:60; mem_file.c:55; utility.c:369-409 | `p * VM_PAGE_SIZE`, `offset / VM_PAGE_SIZE` → `int`; `phys_bytes` → `phys_clicks`; `printf("%lx", ph->phys)` | PHYS, FMT | сужения номеров страниц и форматы при 64-битном `phys_bytes` на ILP32 | А1 | >4Г | S |
| minix/servers/vm/cache.c:78,157-270 | `u32_t offlo = ex64lo(p2), offhi = ex64hi(p2)`; `makehash(dev_t, ino_t)` → `int` | CONST | хэш — безопасное сужение; `ex64lo` на LP64 см. `u64.h` (здесь результат в `u32_t` — работает) | amd64 | — | S |
| minix/servers/vm/exit.c:122,145; mmap.c:180,338 | `VMPCTL_WHO`, `VMPCTL_LEN`, `VMV_FD` (`long`-поля `m*_l*` сообщения) → `endpoint_t`/`int`; формат `%lx` для `phys_bytes` | MSG, FMT | `long`-поля сообщений на LP64 8 байт (безопасные сужения значений); формат — при 64-битном `phys_bytes` | MSG | 64 | S |
| minix/include/minix/com.h:712; vm/mmap.c:180-182 | `VMV_INO` = `m10_l1` (`long`), а `ino_t` — 64 бита | MSG, VIR | номер inode при `mmap` файла передаётся VFS→VM в `long`: на ILP32 обрезается до 32 бит (ФС с 64-битными inode — ext2/ISO — пока 32-битные, но тип уже 64) | MSG | оба | S |
| minix/servers/vm/slaballoc.c:388 | `i = (char *) mem - (char *) f->data` → `int` | — | безопасно (внутри одной страницы) | — | — | — |

#### Ядро: явные сужающие приведения, невидимые для clang

| Файл:строка | Что | Кат. | Почему проблема | Шаг | Проявл. | Сл. |
|---|---|---|---|---|---|---|
| minix/kernel/system/do_exec.c:46-48 | `(u32_t) m_lsys_krn_sys_exec.ip/stack/ps_str` → `arch_proc_init` | PTR | точка входа, стек и `ps_strings` нового образа явно обрезаются до 32 бит (clang молчит из-за явного приведения) | amd64 | 64 | S |
| minix/kernel/system/do_trace.c:140 | `i = (int) tr_addr` | PTR | смещение в `struct proc` для `T_GETUSER/T_SETUSER` — безопасно по величине | — | — | — |
| minix/kernel/smp.c:60,118 | `bkl_owner_cpu = (u32_t) -1` | — | номер CPU, не адрес — безопасно | — | — | — |

#### Сообщения IPC (minix/include/minix/ipc.h)

Автоматически (abi64, `messages_56_bytes`): на LP64 **128 из 256** типов полезной
нагрузки больше 56 байт, сам `message` — 104 байта вместо 64. Причины (поле может
относиться к нескольким): `vir_bytes` — 61 тип, `size_t` — 58, указатели — 26,
`long` (`m*_l*`) — 20, `phys_bytes` — 7, `key_t` (`long`) — 2, `ssize_t` — 1.
При 64-битном `phys_bytes` на ILP32 (p64) не помещаются 7 типов:
`mess_krn_lsys_sys_umap`, `mess_lsys_krn_readbios`, `mess_lsys_krn_sys_copy`,
`mess_lsys_krn_sys_memset`, `mess_lsys_krn_sys_privctl`, `mess_lsys_krn_sys_sdevio`,
`mess_lsys_vm_map_phys` — и `message` целиком. Полный список — приложение А.

Сверх раскладки — что в коде опирается на 64-байтовое сообщение:

| Файл:строка | Что | Кат. | Почему проблема | Шаг | Проявл. | Сл. |
|---|---|---|---|---|---|---|
| minix/include/minix/ipcconst.h:17-18; ipc.h:2669-2675 | `_ASSERT_MSG_SIZE` (== 56), `u8_t size[56]`, `_ASSERT_message` (== 64), `__ALIGNED(16)` | MSG, ERR | на LP64 — ошибка сборки в каждой единице трансляции, включающей `<minix/ipc.h>` (весь системный код) | MSG | сборка | L |
| minix/kernel/arch/i386/klib.S, earm/klib.S (`copy_msg_from_user`/`copy_msg_to_user`) | копирование сообщения фиксированным числом слов | ASM | размер сообщения зашит в ассемблер | MSG | 64 | S |
| minix/include/minix/ipc.h:35-100 (`mess_1`…`mess_10`) | универсальные раскладки с `char *m1p1…`, `long m2l1…`, `m9l5`, `m10l3` | MSG | общие `m*_p*`/`m*_l*` используются десятками протоколов через `#define` (в т.ч. `VMV_*`, `VT_*`, `VMPCTL_*`) — меняя их, меняем все протоколы сразу | MSG | 64 | L |

### 11.2 Системные серверы (кроме VM) и базовые библиотеки

Сырьё `scan64.py` по разделу: m64 — 202 диагностики, p64 — 3, разные раскладки — 73 структуры.
Важная оговорка по сырью: прогон m64 шёл с машинными заголовками i386 (`reg_t`/`clock_t` = `unsigned int`,
`ELFSIZE=32`), поэтому часть диагностик (`Elf32_*` в coredump/AuxInfo, `reg_t` в stacktrace) — артефакт и
с настоящими amd64-заголовками превратится в другие проблемы (см. ERR/ARCH ниже).
Структуры из ldiff: все 73 — внутренние (указатели/`size_t`/`long` в таблицах серверов, колбэки SEF, TAILQ),
либо передаются между процессами одной архитектуры (`struct exec_info` VFS/RS→PM, `devman_dev` сериализуется
по смещениям). Внешней раскладки (диск/сеть/железо) среди них нет.

#### libc — обёртки системных вызовов MINIX (minix/lib/libc/sys, minix/lib/libc/arch)

| Файл:строка | Что | Кат. | Почему проблема | Шаг | Проявл. | Сл. |
|---|---|---|---|---|---|---|
| minix/lib/libc/sys/stack_utils.c:121,133,153,160,168 | `int *vsp`; `*vsp = minix_get_user_sp() - stack_size`; `(char *)(*vsp + …)` | PTR | вершина стека нового процесса хранится в `int`: адрес ≥2^31 обрезается/знакорасширяется; все argv/envp/ps_strings в кадре получают неверные адреса. Вызывающие тоже держат `int vsp`: execve.c:23, servers/rs/exec.c:27 (и vm/main.c — раздел VM) | amd64 | 64 | S |
| minix/lib/libc/sys/stack_utils.c:68,137,168 | `STACK_MIN_SZ` = `sizeof(int)+…`; `*fpw++ = (char *)argc` (слот размера указателя); `ps_argvstr = vsp + sizeof(argc)` | LAYOUT | argc записывается словом размера указателя (8 байт), а `ps_argvstr`/`STACK_MIN_SZ` считают его 4-байтным: на LP64 `ps_argvstr`/`ps_envstr` смещены на 4 байта, VFS ищет AuxInfo по `ps_envstr` → exec любого динамического бинарника сломан, плюс кадр недорезервирован на 4 байта. Копия макроса — servers/vfs/exec.c:406-411 | amd64 | 64 | S |
| minix/lib/libc/sys/getdents.c:8 | `ssize_t getdents(...)` vs `int getdents(int,char*,size_t)` в include/dirent.h:114 | ERR | на ILP32 `ssize_t`==`int`, на LP64 конфликт типов — ошибка компиляции | amd64 | сборка | S |
| minix/lib/libc/sys/mmap.c:49-62 | `minix_vfs_mmap(..., u32_t vaddr, ...)`; `m_vm_vfs_mmap.vaddr/len` — `u32_t` (minix/include/minix/ipc.h:2373-2375, minix/vm.h:23,29) | MSG | адрес отображения сегмента ELF и его длина обрезаются до 32 бит; VFS вызывает это при exec (servers/vfs/exec.c:172-173) — сегменты выше 4 ГБ мапятся не туда | MSG | 64 | M |
| minix/lib/libc/sys/mmap.c:143-155 | `unsigned long vm_getphys()`; физ. адрес в `void *ret_addr` (ipc.h:928-933), прототип sys/sys/mman.h:210 | PHYS | на ILP32 с 64-битным `phys_bytes` физ. адрес не помещается ни в `void *`, ни в `unsigned long`; IPC-сервер использует его как идентификатор сегмента shm (servers/ipc/shm.c:119,211) | А1 | >4Г | M |
| minix/lib/libc/sys/_ucontext.c:92-214 | `makecontext` только для `__i386__`/`__arm__`, иначе `#error`; `unsigned int *stack_top`, `(reg_t) stack_top`, `*stack_top++ = (uintptr_t)func` | ARCH | на новой архитектуре не соберётся; шаблон кладёт указатели в 32-битные слоты стека — для amd64/aarch64 переписывать с `uintptr_t`/`register_t` (на amd64 аргументы ещё и в регистрах) | amd64 | сборка | M |
| minix/lib/libc/arch/{i386,arm}/sys/*.S | `_ipc.S`, `_do_kernel_call_intr.S`, `ucontext.S`, `__sigreturn.S`, `brksize.S`, `ipc_minix_kerninfo.S`; minix/lib/libminc/Makefile:27-31,189,319,344 по `${MACHINE_CPU}` | ARCH | точки входа IPC/ядра, контексты и sigreturn есть только для i386/arm | amd64 | сборка | M |
| minix/lib/libc/sys/ptrace.c:24 | `int ptrace()` возвращает `m_pm_lc_ptrace.data` (`long`) | VIR | на LP64 T_READ*/T_GETUSER отдают `long` от PM/ядра, а пользователь получает младшие 32 бита (NetBSD-семантика PT_READ_D — `int`, но ядро MINIX читает `long`) | amd64 | 64 | S |
| minix/lib/libc/sys/mount.c:84 | `%llx`/`%llu` для `dev_t`/`ino_t` | FMT | на LP64 это `unsigned long` — предупреждение -Wformat (ошибка при -Werror) | amd64 | сборка | S |

Итог: на LP64 сломан сам exec (int-стек + argc 4/8 байт в `stack_utils.c`), не соберутся `getdents` и `_ucontext.c`,
нет arch-asm. `minix_vfs_mmap` с `u32_t vaddr` — тихая обрезка адресов сегментов. `vm_getphys` не переживёт PAE.

#### libsys (minix/lib/libsys)

| Файл:строка | Что | Кат. | Почему проблема | Шаг | Проявл. | Сл. |
|---|---|---|---|---|---|---|
| minix/lib/libsys/sys_vmctl.c:30-39 | `m.SVMCTL_PTROOT = ptroot` — `SVMCTL_PTROOT` = `m1_i3` (`int`, com.h:381) | PHYS | корень таблицы страниц (CR3/TTBR) передаётся ядру в `int`: на amd64 PML4 может лежать выше 4 ГБ, на PAE/LPAE — тоже; p64-прогон подтверждает сужение | А1 | оба | S |
| minix/lib/libsys/sys_vmctl.c:16-27 | `sys_vmctl_get_pdbr(..., u32_t *pdbr)` ← `SVMCTL_VALUE` (`m1_i3`) | PHYS | PDBR/CR3 в 32 битах | А7 | оба | S |
| minix/lib/libsys/sys_vmctl.c:3,10 | `sys_vmctl(endpoint_t, int, u32_t value)` → `m1_i3` | MSG | любое значение-адрес через общий `sys_vmctl` обрезается; сейчас VM передаёт 0/коды, но интерфейс 32-битный | MSG | 64 | S |
| minix/lib/libsys/sys_vmctl.c:56-62 | `*mem = m.SVMCTL_MRG_ADDR` (`m2_i2`, `int`), `*len = SVMCTL_MRG_LENGTH` (`m2_i3`); `requestor` в `m2_p1` (`char *`) | MSG | адрес/длина запроса памяти (MEMREQ, копирование ядром в чужое АП) — `int`, знакорасширяется в `vir_bytes`; endpoint в поле-указателе (clang: sys_vmctl.c:60,62) | MSG | 64 | S |
| minix/lib/libsys/sys_vmctl.c:83-84 | `SVMCTL_MAP_PHYS_ADDR/LEN` = `m2_l1/m2_l2` (`long`) → `phys_bytes` | PHYS | на ILP32 `long` 32-битный — физ. адреса отображений ядра (APIC, framebuffer…) выше 4 ГБ теряются | А1 | >4Г | S |
| minix/lib/libsys/vm_map_phys.c:9,16 | `vm_map_phys(endpoint_t, void *phaddr, size_t)`; `(phys_bytes)phaddr` (прототип minix/include/minix/vm.h:16) | PHYS | физ. адрес MMIO передаётся как `void *`: на ILP32+PAE/LPAE адрес >4 ГБ (64-битные BAR, периферия ARM LPAE) невыразим. 25 файлов-потребителей в drivers/servers | А1 | >4Г | M |
| minix/lib/libsys/alloc_util.c:8-30 | `alloc_contig()` — флаги только `AC_LOWER16M`/`AC_LOWER1M`/`AC_ALIGN64K` (syslib.h:100-101), `MAP_CONTIG` без ограничения сверху | HW | нет способа запросить DMA-буфер ниже 4 ГБ: устройства с 32-битным DMA (большинство драйверов MINIX пишут только младшую половину адреса) получат буфер выше 4 ГБ | А3 | >4Г | M |
| minix/lib/libsys/pci_get_bar.c:20-38 | `pci_get_bar(int, int, u32_t *base, u32_t *size, …)`; в сообщении `int base` (ipc.h:1722-1728) | HW | 64-битные BAR (память устройства выше 4 ГБ) не представимы ни в API, ни в сообщении от PCI | драйв. | >4Г | M |
| minix/lib/libsys/sys_umap.c:19; minix/lib/libsys/arch/i386/sys_umap_remote.c:29 | `m_lsys_krn_sys_umap.nr_bytes` — `int` ← `vir_bytes bytes` | VIR | длина области >2 ГБ обрезается (alloc_contig вызывает umap на весь буфер) | MSG | 64 | S |
| minix/lib/libsys/vm_memctl.c:15,23 | `VM_RS_CTL_LEN` = `m2_i3` (`int`) ← `size_t *len`; `VM_RS_CTL_ADDR` `m2_p1` | VIR | длина предразмещённой области RS↔VM в `int` | MSG | 64 | S |
| minix/lib/libsys/arch/i386/sys_sdevio.c:19 | `vec_addr = (vir_bytes)buffer`, поле `phys_bytes vec_addr` (ipc.h, mess_lsys_krn_sys_sdevio) | MSG | виртуальный адрес в поле физического типа; при `phys_bytes` 64 на ILP32 работает, но тип неверный (x86-only) | MSG | — | S |
| minix/lib/libsys/clock_time.c:23-37 | «We assume atomic 32-bit field retrieval. TODO: 64-bit»; `if (system_hz < LONG_MAX / 40000)` | TIME | `realtime` — 32-битный `clock_t`; на LP64 проверка всегда истинна, защита от переполнения `(realtime % hz) * 40000` при hz > 107374 исчезает | amd64 | 64 | S |
| minix/lib/libsys/stacktrace.c:20-30 | обход кадров через `reg_t` как указатель | ARCH | если `reg_t` на новой архитектуре не станет 64-битным — обрезка указателей; сейчас `get_bp` только i386/arm | amd64 | 64 | S |
| minix/lib/libsys/vm_cache.c:25,29 | `%lld` для `off_t` | FMT | на LP64 `off_t` = `long` | amd64 | сборка | S |
| minix/lib/libsys/Makefile:10; minix/lib/libsys/arch/{i386,earm}/ | `.include "arch/${MACHINE_ARCH}/Makefile.inc"` — `sys_in/out/vin*/vout*`, `tsc_util`, `vbox`, `spin`, `get_randomness` | ARCH | для новой архитектуры каталога нет — сборка упадёт | amd64 | сборка | M |

Итог: libsys — главный источник проблем >4Г: корень таблиц страниц в `int` (`SVMCTL_PTROOT`), физ. адреса
через `void *`/`long` (`vm_map_phys`, `SVMCTL_MAP_PHYS_*`), нет флага «ниже 4 ГБ» у `alloc_contig`, 32-битные BAR.
На 64 — адрес MEMREQ в `int`.

#### libexec (minix/lib/libexec)

| Файл:строка | Что | Кат. | Почему проблема | Шаг | Проявл. | Сл. |
|---|---|---|---|---|---|---|
| minix/lib/libexec/exec_elf.c:22-23,83-92 | `#define __ELF_WORD_SIZE 32` (FreeBSD-макрос, в NetBSD мёртв); `Elf_*` берётся из `ELFSIZE` машинных заголовков; `check_header` не проверяет `EI_CLASS` и `e_machine` | ARCH | на amd64 загрузчик молча станет ELF64-only, а 32-битный ELF (и наоборот) будет разобран как чужой класс вместо ENOEXEC; нужна явная проверка класса/машины | amd64 | 64 | S |
| minix/lib/libexec/exec_elf.c:39-40 | `e_phoff + e_phentsize*e_phnum ≤ SECTOR_SIZE (512)` | CONST | для ELF64 (64 + 56·n) помещается ≤8 phdr; типичные amd64/aarch64-бинарники (PHDR, INTERP, 2–4 LOAD, DYNAMIC, NOTE, GNU_STACK, GNU_RELRO, TLS) — 9–12 → ENOEXEC | amd64 | 64 | S |
| minix/lib/libexec/exec_elf.c:249-250 | `int rem_mem = seg_membytes - fbytes` | VIR | BSS > 2 ГБ обрезается | amd64 | 64 | S |
| minix/lib/libexec/exec_elf.c:183 | `printf("%x", (uint32_t)ph)` под `ELF_DEBUG` | PTR | приведение указателя к 32 битам (только в отладке) | amd64 | — | S |

Итог: ELF32-only жёстко не прописан, но лимит 512 байт на phdr отсечёт обычные 64-битные бинарники, и класс ELF не проверяется.

#### libmthread, libvassert, libminc, libtimers, libasyn, libdevman

| Файл:строка | Что | Кат. | Почему проблема | Шаг | Проявл. | Сл. |
|---|---|---|---|---|---|---|
| minix/lib/libmthread/allocate.c:413,431,476 | раскладка guard-страницы стека только `__i386__ \|\| __arm__`, иначе `#error` | ARCH | не соберётся на новой архитектуре (логика переносима) | п.8 | сборка | S |
| minix/lib/libmthread/misc.c:119 | `mthread_stacktrace` только `__i386__` (`_UC_MACHINE_EBP`) | ARCH | молча пустой на других | п.8 | — | S |
| minix/lib/libvassert/vassert.c:33-40,132,205 | `typedef uint32 VA` если не `VM_X86_64`; `(VA)&vassert_state`; inline asm `rep outsb` с eax/ebx/ecx/edx | ARCH | VMware-backdoor x86-only; на amd64 адрес обрезается до 32 бит (clang: :132) | amd64 | 64 | S |
| minix/lib/libminc/arch/{i386,earm} | `Makefile.libc.inc` только для двух архитектур | ARCH | сборка libminc (серверы, загрузка) на новой архитектуре | amd64 | сборка | S |

Итог: libtimers, libasyn, libdevman проблем не имеют (libdevman сериализует по смещениям, не по указателям).
libmthread/libminc/libvassert — только ARCH-заглушки.

#### PM (minix/servers/pm)

| Файл:строка | Что | Кат. | Почему проблема | Шаг | Проявл. | Сл. |
|---|---|---|---|---|---|---|
| minix/servers/pm/exec.c:50; minix/servers/pm/main.c:349-352 | `m.VFS_PM_PS_STR = ps_str` / `(vir_bytes)m_in.VFS_PM_NEWPS_STR` — оба `m7_i5` (`int`, com.h:568,574) | MSG | адрес `ps_strings` пользователя в `int`: обрезается, на обратном пути знакорасширяется (0xffffffff8…); `sys_exec` получает неверный ps_str → ps/MIB/ld.so. Парная сторона — servers/vfs/main.c:686,699 | MSG | 64 | S |
| minix/servers/pm/misc.c:36-50 | `uts_val.machine`/`uts_tbl` — `#if __i386__ / __arm__`, иначе `#error` | ARCH | uname не соберётся на новой архитектуре | amd64 | сборка | S |
| minix/servers/pm/alarm.c:55-62 | `ticks = LONG_MAX` при возврате `clock_t` (32 бит) | CONST | на LP64 мёртвый код (LONG_MAX → 0xffffffff, сравнение всегда ложно); вред сейчас блокирует `is_sane_timeval` (MAX_SECS = TMRDIFF_MAX/hz), но защита держится только на нём | amd64 | — | S |

Итог: в PM главное — `ps_strings` через `int`-поле `m7_i5`. Сигналы (sigframe/mcontext строит ядро; PM
кладёт `vir_bytes`-поля в `struct sigmsg`) и время (`time_t` в сообщениях) в PM корректны.

#### VFS (minix/servers/vfs)

| Файл:строка | Что | Кат. | Почему проблема | Шаг | Проявл. | Сл. |
|---|---|---|---|---|---|---|
| minix/servers/vfs/main.c:686,699 | `ps_str = (vir_bytes)job_m_in.VFS_PM_PS_STR`; `m_out.VFS_PM_NEWPS_STR = ps_str` (`m7_i5`, `int`) | MSG | см. PM: адрес `ps_strings` через `int` | MSG | 64 | S |
| minix/servers/vfs/exec.c:172-173 | `minix_vfs_mmap(..., vaddr /*vir_bytes→u32_t*/, ...)` | MSG | адрес сегмента ELF и `len` усекаются до 32 бит в сообщении к VM (ipc.h:2373-2375) | MSG | 64 | M |
| minix/servers/vfs/exec.c:406-411 | копия `STACK_MIN_SZ` с `sizeof(int)` для argc | LAYOUT | то же расхождение argc 4/8 байт, что в stack_utils.c (проверка «кадр слишком мал» и поиск AuxInfo) | amd64 | 64 | S |
| minix/servers/vfs/exec.c:626,669 | `int a0 = (int)(((char **)stack)[1] - *vsp)`; `((char **)stack)[1] = (char *)a0 - arg_len + *vsp` | PTR | указатель/смещение через `int` (работает, пока кадр < 2 ГБ, но UB и предупреждения pointer↔int) | amd64 | 64 | S |
| minix/servers/vfs/read.c:136-150,220-243,274-392; proto.h:206-208,247-248 | `int read_write(...)` возвращает `size_t cum_io` при `size ≤ SSIZE_MAX`; `req_peek/req_bpeek(..., unsigned int bytes)` | VIR | на ILP32 `SSIZE_MAX`=`INT_MAX`, на LP64 — 2^63: read/write/peek > 2 ГБ возвращают обрезанный счётчик (ответ VFS идёт через `int m_type`); peek > 4 ГБ усекается. Нужен клэмп `nbytes ≤ INT_MAX` или 64-битный ответ | amd64 | 64 | M |
| minix/servers/vfs/coredump.c:12-25 | прототипы с `Elf32_Ehdr/Elf32_Phdr/Elf32_Word/Elf32_Off/Elf32_Addr/Elf32_Nhdr`, определения с `Elf_*` | ERR | при `ELFSIZE=64` конфликт типов → ошибка компиляции; заодно `ELFOSABI_FREEBSD` и `gregset_t`-note нужно определить для новой архитектуры | amd64 | сборка | S |
| minix/servers/vfs/filedes.c:511; misc.c:354; mount.c:182,482,582; open.c:271; read.c:177; select.c:1040,1091; vmnt.c:53 | `%llx`/`%llu` для `dev_t`/`ino_t` | FMT | на LP64 это `unsigned long` | amd64 | сборка | S |

Итог: VFS на 64 битах соберётся после правки coredump.c и форматов; тихие ошибки — `ps_strings` в `int`, `u32_t vaddr`
в `VM_VFS_MMAP`, счётчики read/write >2 ГБ. `off_t` к ФС/драйверам везде 64-битный (`RES_64BIT`), select/ioctl
передают указатели как `vir_bytes` — корректно.

#### IPC-сервер (minix/servers/ipc)

| Файл:строка | Что | Кат. | Почему проблема | Шаг | Проявл. | Сл. |
|---|---|---|---|---|---|---|
| minix/servers/ipc/sem.c:787; minix/servers/ipc/shm.c:379; inc.h:52,60 | определения `ssize_t get_{sem,shm}_mib_info()`, прототипы `int` | ERR | на LP64 конфликт типов | amd64 | сборка | S |
| minix/servers/ipc/shm.c:9,119,211,237 | `phys_bytes vm_id = vm_getphys(...)` как идентификатор сегмента; `printf("%lu", vm_id)` | PHYS | идентификатор = физ. адрес через `unsigned long` (см. libc mmap.c:143); при 64-битном `phys_bytes` на ILP32 обрезается (разные сегменты выше 4 ГБ могут совпасть) и `%lu` неверен (p64) | А1 | >4Г | S |

Итог: две ошибки сборки; shm опирается на физ. адрес как ID.

#### IS, MIB, RS, DS, SCHED, DEVMAN, INPUT

| Файл:строка | Что | Кат. | Почему проблема | Шаг | Проявл. | Сл. |
|---|---|---|---|---|---|---|
| minix/servers/is/dmp_kernel.c:318-353 | `proctab_dmp()` определена только под `__i386__`/`__arm__` | ARCH | на новой архитектуре неопределённый символ → ошибка линковки | amd64 | сборка | S |
| minix/servers/mib/hw.c:5-13 | `mach`/`arch` — `#if __i386__/__arm__`, иначе `#error` | ARCH | не соберётся | amd64 | сборка | S |
| minix/servers/mib/mib.h:9-15 | `CONFIG_MAX_CPUS` берётся из `kernel/arch/i386/include/archconst.h`, иначе 1 | ARCH | на других архитектурах MIB молча считает 1 CPU (уже так на earm) | amd64 | — | S |

Итог: RS (rprocpub, boot image, ipc_filter), DS, SCHED, DEVMAN, INPUT 64-битных дефектов не имеют — все их
диагностики безопасные сужения. MIB `kinfo_proc2`/`kinfo_lwp` заполняются 32-битными полями по ABI NetBSD (так задумано).

#### Безопасные сужения (не в таблицах)

~144 из 202 m64-диагностик и 1 из 3 p64:
- ~70 мест — `ssize_t/size_t`→`int` у длин строк, меток, ключей, результатов sysctl/rmib, сокетов (`recvfrom/sendto/recvmsg/sendmsg`, `rmib.c`, `mib/tree.c`, `mib/main.c`, `mib/remote.c`, `rs/manager.c`, `ds.c`, `ds/store.c`, `vbox.c`, `minix_rs.c`, `__getlogin.c`, `posix_spawn.c`, `mtab.c`, `vfs/path.c`, `vfs/vmnt.c`, `vfs/request.c:334,745`, `input.c`, `devman`, `libdevman/generic.c`): значения ограничены ARG_MAX/PATH_MAX/буферами.
- ~25 мест — `long`→`int`/`endpoint_t`/`devmajor_t`/`cp_grant_id_t` для разности указателей на элементы таблиц, endpoint'ов и `m4_l*`/`m9_l*`-полей (`pm/forkexit.c:237`, `pm/trace.c`, `rs/utility.c`, `vfs/select.c:1017,1189`, `devman/*`, `ipc/sem.c:111`, `ipc/shm.c:71`, `sys_vmctl.c:60`).
- ~15 мест — `clock_t` (32 бита по ABI NetBSD) ← `getticks()`/`u64` и `time_t`/`dev_t`→`u32` в `kinfo_proc2` (`pm/main.c:67`, `rs/main.c:87`, `rs/request.c:948`, `rs/update.c:374`, `vfs/main.c:112`, `vfs/select.c:330`, `sys_vtimer.c:24`, `mib/proc.c:444-750`, `mib/vm.c:67`): поведение то же, что на i386.
- ~12 мест — `tv_usec/tv_nsec`→`suseconds_t`, `uint32 nsec` (`gettimeofday.c`, `nanosleep.c`, `pm/alarm.c:75`, `mib/proc.c:171`, `vfs/request.c:1208-1209`), `fstatfs.c:17`, `stadir.c:280`, `sqrt_approx.c`, `tsc_util.c:93-95`, `pci_attr_r32.c:30`, `sys_getinfo.c:46`, `getsockopt.c:124`, `sem.c:107`, `ioctl.c:325` (`int request_save` — работает за счёт преобразования меток switch).
- ~10 мест — `vfs/exec.c:174,480,481,510`, `vfs/coredump.c:139,168,215,216`, `exec.c:369` (p64): артефакт `ELFSIZE=32` в прогоне либо длины < ARG_MAX; реальная проблема coredump — ERR выше.
- ~8 — `ipc/shm.c:256-257` (`shminfo` по ABI NetBSD), `vfs/misc.c:392,436,440` (`VFS_VMCALL_LENGTH` — длины страничных запросов VM; `VMV_DEV`/`VMV_INO` — так же на i386), `vfs/pipe.c:251,266,528` (≤ PIPE_BUF), `vfs/exec.c:563,623,641,705,756`, `sef_liveupdate.c:299`.

#### Сводка раздела

| Категория | Мест (строк таблиц) |
|---|---|
| PTR | 3 |
| PHYS | 6 |
| VIR | 5 |
| MSG | 7 |
| LAYOUT | 2 |
| HW | 2 |
| CONST | 2 |
| TIME | 1 |
| FMT | 3 |
| ERR | 3 |
| ARCH | 13 |
| **Всего** | **47** |

Самые опасные места:
1. `minix/lib/libc/sys/stack_utils.c:68,121-168` (+ `vfs/exec.c:406`) — `int vsp` и argc 4 vs 8 байт: на LP64 не работает exec вообще.
2. `minix/lib/libsys/sys_vmctl.c:38` — `SVMCTL_PTROOT` (`int`): корень таблицы страниц выше 4 ГБ (оба режима).
3. `minix/lib/libsys/alloc_util.c:8-30` — `alloc_contig` без «ниже 4 ГБ»: DMA-буферы для 32-битных устройств (HW, >4Г).
4. `minix/lib/libsys/vm_map_phys.c:9,16` — физ. адрес MMIO как `void *` (>4Г на ILP32, 25 потребителей).
5. `minix/servers/pm/exec.c:50`, `pm/main.c:352`, `vfs/main.c:686,699` — `ps_strings` через `m7_i5` (`int`).
6. `minix/lib/libc/sys/mmap.c:49-62`, `vfs/exec.c:172` — `VM_VFS_MMAP` с `u32_t vaddr/len`.
7. `minix/lib/libsys/sys_vmctl.c:56-57` — адрес/длина MEMREQ в `int` (копирование ядром в чужое АП).
8. `minix/lib/libsys/pci_get_bar.c` + ipc.h:1723 — 64-битные BAR невыразимы (HW, >4Г).
9. `minix/lib/libsys/sys_vmctl.c:83-84`, `sys_vmctl.c:16-27` — `SVMCTL_MAP_PHYS_*` в `long`, PDBR в `u32_t` (>4Г).
10. `minix/lib/libexec/exec_elf.c:39-40` — phdr ≤ 512 байт: для ELF64 только ≤8 заголовков программы.

Не уверен:
- `minix/servers/vfs/misc.c:438` — `VMV_SIZE_PAGES = LONG_MAX` для блочных устройств: на LP64 это 2^63 страниц; надо проверить, не умножает ли VM на `PAGE_SIZE` (переполнение) — сторона VM.
- `minix/servers/vfs/worker.c:15-19` — стек рабочего потока VFS 28 КБ: на LP64 кадры растут (`vfs_exec_info` 1288→1408 Б, `lookup`, `struct stat`, `char buf[PAGE_SIZE]` в `patch_stack`); риск переполнения стека потока, нужен замер.
- `minix/lib/libc/sys/ptrace.c:24` — какая ширина слова T_READ*/T_WRITE* будет у ядра amd64 (`long` или `int`); от этого зависит, баг ли возврат `int`.
- `minix/lib/libsys/env_parse.c:72` — `strtol` в `long`: на i386 шестнадцатеричные значения >0x7fffffff (адреса MMIO из параметров загрузки) насыщаются до LONG_MAX, на LP64 — нет; поведение драйверов, читающих адреса через `env_parse`, разойдётся между портами.
- `minix/lib/libsys/sef_init.c:63` — грант `cpf_grant_direct(self, 0, ULONG_MAX, CPF_READ)` на всё АП: на LP64 длина 2^64−1; проверить, что ядро не переполняет `addr+len` при проверке гранта.

### 11.3 Файловые системы, сетевые службы, библиотеки драйверов и ФС

Сырьё `scan64.py` по разделу: m64 — 295 строк, p64 — 11, разные раскладки — 184.
Всё ниже проверено чтением кода. Пути — от корня дерева.

Общая заметка по сырью: в `sys/sys/types.h` `ino_t`, `dev_t`, `off_t`, `time_t`, `u64_t`
уже 64-битные и на i386 (`long long`). Поэтому 54 сужения вида `ino_t/dev_t/time_t/__off_t/
__uint64_t/block64_t → 32` попали в «new» только из-за другого текста `aka` в сообщении. Это
**не 64-специфичные** места (лимиты формата MFS/ext2: `d2_size` i32, `i_size` u32, время u32,
`m_notify.timestamp` → `clock_t`). Они такие же на i386, поэтому в таблицы не внесены.

#### MFS (minix/fs/mfs)

Дисковый формат проверен: `d2_inode` (`type.h`), `struct direct` (`mfsdir.h`: `uint32_t` + 60 байт,
`__packed`), дисковая часть `super_block` (`super.h`, до `s_disk_version`), косвенные блоки
`zone_t[]`, bitmap `bitchunk_t[]` (`buf.h`). Типы `zone_t`/`block_t`/`bit_t`/`bitchunk_t`
жёстко заданы как `uint32_t` (`sys/sys/types.h:119-124`), поэтому раскладка на 64 не меняется.

| Файл:строка | Что | Кат. | Почему проблема | Шаг | Проявл. | Сл. |
|---|---|---|---|---|---|---|
| minix/fs/mfs/super.c:324-325 | `if ((unsigned long)sp->s_max_size > LONG_MAX) sp->s_max_size = LONG_MAX;` (`s_max_size` — `i32_t`) | CONST | На LP64 `LONG_MAX` = 2^63−1. Записанное в `i32_t`, оно становится −1 (clang: `-Wconstant-conversion`). Отрицательное значение с диска при знаковом расширении всегда даёт истинное условие, после чего `s_max_size = −1`, и `link.c:470` и `read.c:53` отказывают всем записям. На i386 было 0x7fffffff. Нужно ограничивать `INT32_MAX` | amd64 | 64 | S |
| minix/fs/mfs/utility.c:23-36; minix/fs/ext2/utility.c:49-62 | `long conv4(int norm, long x)` — «32-bit long» | BITS | Рассчитано на `long` = 32 бита. На LP64 работает только потому, что результат всюду сразу присваивается 32-битному полю (51 предупреждение `long → __uint32_t` в ext2 `inode.c:365-403` и `super.c:380-440`; MFS `inode.c:442-447`, `write.c:207`, `path.c:184,229`). Любое использование результата без усечения (сравнение, 64-битная переменная) даст мусор в старших битах. Перевести на `u32_t` | amd64 | — | S |
| minix/fs/mfs/cache.c:32; inode.c:48; mount.c:49; open.c:114; read.c:181,294,371,440,446; write.c:302 | `%llu/%llx` для `dev_t`/`ino_t` | FMT | На LP64 это `unsigned long`. На amd64 вывод верный, но `-Wformat` (с `-Werror` — ошибка сборки). Нужно `PRIu64` и приведение `(uint64_t)` | amd64 | сборка | S |

Итог: дисковый формат MFS на 64 не меняется. Реальная ошибка одна — ограничение `s_max_size`
(`LONG_MAX` в `i32_t`). Остальное — форматные строки и `conv4` на `long`.

#### ext2 (minix/fs/ext2)

Проверено: `d_inode`, дисковая часть `super_block`, `group_desc` (совпадают, это подтверждают
автоматическая проверка и `type.h`), `struct ext2_disk_dir_desc` (`u32/u16/u8`), косвенные блоки
`block_t[]` (u32). Перестановки индексов в `read.c:233-268` и `write.c:83-165` ограничены
размером блока и безопасны.

| Файл:строка | Что | Кат. | Почему проблема | Шаг | Проявл. | Сл. |
|---|---|---|---|---|---|---|
| minix/fs/ext2/proto.h:37 vs link.c:153; table.c:34 | `int fs_rdlink(...)` в прототипе, `ssize_t fs_rdlink(...)` в определении | ERR | На LP64 `ssize_t` ≠ `int`: «conflicting types», а в `table.c` — несовместимый указатель на функцию для `.fdr_rdlink` (ошибка в clang ≥ 16) | amd64 | сборка | S |
| minix/fs/ext2/inode.c:44; link.c:652; read.c:171,294,355,423,430; utility.c:24; write.c:361 | `%llu/%llx/%lld` для `dev_t`/`ino_t`/`off_t` | FMT | На LP64 это `long`/`unsigned long` | amd64 | сборка | S |
| minix/fs/ext2/utility.c:120,132,195 | `k & (1 << i)`, `k \|= 1 << i`, `mask = 1 << bit` при i ≤ 31 | BITS | Сдвиг знаковой 1 в бит 31 — UB (тот же класс, что в testing.md 4.16). Не 64-специфично | — | — | S |
| minix/fs/ext2/utility.c:95-200; balloc.c; ialloc.c | bitmap ext2 читается словами `bitchunk_t` (u32) | ARCH | Bitmap ext2 на диске побайтовый (LE). Чтение словами u32 корректно только на LE; на BE-цели (mips64eb) порядок битов будет неверным. К 64 битам не относится | — | — | M |

Итог: на 64 не соберётся из-за `fs_rdlink`. Дисковые структуры переносимы. Ext2 и MFS (кроме
`s_max_size`) на amd64 будут работать после исправления типов.

#### ISO9660 (minix/fs/isofs)

Без находок. `struct iso9660_vol_pri_desc` (`super.h`) — `packed`; её размер различается
(2056/2064) только из-за хвостовых указателей в памяти. С диска копируется ровно 2048 байт
(`super.c:38`). `struct iso9660_dir_record` — `packed`, все поля фиксированной ширины.
`dir_extent`, `inode_dir_entry`, `inode_cache` и uthash — внутренние структуры.
Сужения `size_t/ssize_t → int` в `inode.c:433,439`, `read.c:90-101`, `super.c:91` и
`susp_rock_ridge.c:61,69,196` ограничены размером сектора и имени.

Итог: ISO9660 на 64 переносим.

#### PFS, PTYFS (minix/fs/pfs, minix/fs/ptyfs)

Без находок: структуры внутренние, на диск и в протоколы не уходят.

#### procfs (minix/fs/procfs)

| Файл:строка | Что | Кат. | Почему проблема | Шаг | Проявл. | Сл. |
|---|---|---|---|---|---|---|
| minix/fs/procfs/service.c:256; tree.c:188,218 | `(cbdata_t)slot`, `(pid_t)cbdata`, `(cbdata_t)pid` — `int` ↔ `void *` | PTR | Значение туда и обратно сохраняется (малые целые), но на 64 это предупреждения `-Wint-to-void-pointer-cast` и наоборот. Нужно через `(intptr_t)` | amd64 | сборка | S |
| minix/fs/procfs/root.c:5-154; cpuinfo.c:5-13,108 | `#if defined(__i386__)`; `cpuinfo.c` включает `../../kernel/arch/i386/include/archconst.h` | ARCH | На новой архитектуре `/proc/cpuinfo` и вывод PCI/IPC-векторов молча выпадают | amd64 | — | S |

Итог: вывод адресов уже LP64-корректен (`pid.c:220` и `root.c:198` используют `%08lx` для
`vir_bytes`/`unsigned long`, `pid.c:115` — `PRIu64`). Остальное — только предупреждения и
привязка к i386.

#### hgfs (minix/fs/hgfs + minix/lib/libhgfs) — VMware backdoor

| Файл:строка | Что | Кат. | Почему проблема | Шаг | Проявл. | Сл. |
|---|---|---|---|---|---|---|
| minix/lib/libhgfs/backdoor.S:10-… | `__libhgfs_backdoor{,_in,_out}`: `pushl/movl`, 32-битные `%esi/%edi/%ebp`, аргумент через `4+16(%esp)` | ASM | Только i386, на amd64 не соберётся. В 64-битном протоколе backdoor (high-bandwidth `rep insb/outsb`) адрес буфера передаётся в `%rsi/%rdi` целиком | amd64 | сборка | M |
| minix/lib/libhgfs/channel.c:95,136 | `ptr[4] = (u32_t)buf;` / `ptr[5] = (u32_t)buf;` (`u32_t ptr[7]`) | PTR | Адрес буфера RPC для гипервизора обрезается до 32 бит. На 64 массив регистров должен быть 64-битным (`uintptr_t`) | amd64 | 64 | M |
| minix/lib/libhgfs/dir.c:21,41,69; file.c:46,67,102,130 | `(sffs_dir_t)RPC_NEXT32`, `(u32_t)handle` — хэндл хоста u32 ↔ `void *` | PTR | Значение сохраняется, но на 64 это предупреждения. Нужно через `(uintptr_t)` | amd64 | сборка | S |
| minix/fs/Makefile:12-15; minix/lib/Makefile:32 | `hgfs`, `vbfs` (и `libhgfs`/`libvboxfs`) собираются только при `MACHINE_ARCH == "i386"` | ARCH | На amd64 выпадут молча (заметка) | amd64 | — | S |

Итог: hgfs на 64 нужно переписать в части backdoor (ASM и адрес в регистрах). Протокол RPC
(`RPC_NEXT32`, `ex64lo/ex64hi`, байтовая сериализация) от разрядности не зависит.

#### vbfs + libvboxfs (minix/fs/vbfs, minix/lib/libvboxfs) — VirtualBox Shared Folders

| Файл:строка | Что | Кат. | Почему проблема | Шаг | Проявл. | Сл. |
|---|---|---|---|---|---|---|
| minix/lib/libvboxfs/vboxfs.h:40-58,64-72,88-93,95-100 | `vboxfs_objattr_t` (44→48), `vboxfs_objinfo_t` (92→96), `vboxfs_crinfo_t` (108→112), `vboxfs_dirinfo_t` (1152→1160) | LAYOUT | Эти структуры отдаются хосту по `vbox_set_ptr` (`handle.c:79`, `attr.c:69,108,164`, `info.c:20`). Их раскладку задаёт протокол VirtualBox (`SHFLCREATEPARMS`, `RTFSOBJINFO`). На x86_64 поля `u64_t` выравниваются по 8: union в `objattr` вырастает 36→40, сдвигаются `info`/`attr` и `name`. i386 и x86_64 дают разные раскладки, и хосту соответствует только одна из них. Нужны явные `__packed`/`aligned(4)` или явный паддинг + `_Static_assert` размеров | amd64 | 64 | M |
| minix/lib/libvboxfs/dir.c:196-211 | разбор ответа LIST: `bpos += offsetof(vboxfs_dirinfo_t, name) + offsetof(vboxfs_path_t, data) + name.size` | LAYOUT | Шаг по буферу хоста считается через `offsetof` структуры с 64-битной раскладкой, поэтому на 64 со второй записи начнётся мусор | amd64 | 64 | S |

Итог: на 64 vbfs не работает без исправления раскладки. Отдельно (не этот раздел):
`vbox_param_t` (`minix/include/minix/vboxtype.h`) и драйвер `minix/drivers/vmm_guest/vbox`.
64-битный гость должен использовать `VMMDevReq_HGCMCall64`/`HGCMFunctionParameter64` с 64-битным
LinAddr, проверить в разделе драйверов. `vboxfs_volinfo_t` (`vboxfs.h:128`, 40/40) и
`vboxfs_fsprops_t` совпадают.

#### libfsdriver (minix/lib/libfsdriver)

| Файл:строка | Что | Кат. | Почему проблема | Шаг | Проявл. | Сл. |
|---|---|---|---|---|---|---|
| minix/lib/libfsdriver/call.c:241 (+ utility.c:20,46,72) | `data.grant = (cp_grant_id_t)buf;` при `data.endpt = SELF`, а `fsdriver_copyin/copyout/zero` читают `data->ptr` | PTR | `struct fsdriver_data` (`minix/include/minix/fsdriver.h:19-26`) — это union `{cp_grant_id_t grant; char *ptr;}`. Записываются только младшие 32 бита указателя от `mmap`, старшая половина `ptr` — мусор со стека. На 64 падение (или порча памяти) в `builtin_peek`, то есть на каждом page-in из VM через FS без `fdr_peek` (MFS/ext2 через `lmfs`). Нужно `data.ptr = buf;` | amd64 | 64 | S |
| minix/lib/libfsdriver/fsdriver.h:28,30 vs call.c:941,955 | `extern int fsdriver_bread/bwrite(...)` при определении `ssize_t` | ERR | conflicting types на LP64 | amd64 | сборка | S |

Итог: сборка ломается на прототипах. В работе критична `builtin_peek` (указатель через
`grant`). Сужения `ssize_t → int` (`call.c:187,314,352,711,995`, `lookup.c:100`) ограничены
проверкой `count > INT_MAX` (`call.c:105`) и безопасны.

#### libminixfs (minix/lib/libminixfs)

| Файл:строка | Что | Кат. | Почему проблема | Шаг | Проявл. | Сл. |
|---|---|---|---|---|---|---|
| minix/lib/libminixfs/cache.c:339,583 | `%llu`/`%llx` для `u64_t`/`dev_t` | FMT | На LP64 это `unsigned long`. Нужно `PRIu64`/`PRIx64` (в `cache.c:765` уже так) | amd64 | сборка | S |

Итог: кэш блоков переносим. `BUFHASH` (`cache.c:41`) считается от номера блока, не от
указателя. iovec для `bdev_gather/scatter` (`cache.c:893-923`) строится в `vir_bytes`. Размеры
блоков, `munmap_t(…, size_t → int)` и эвристика размера кэша в KB (`cache.c:95-100`) безопасны.

#### libbdev / libblockdriver (minix/lib/libbdev, minix/lib/libblockdriver)

| Файл:строка | Что | Кат. | Почему проблема | Шаг | Проявл. | Сл. |
|---|---|---|---|---|---|---|
| minix/lib/libbdev/bdev.c:205-214 → minix/lib/libblockdriver/driver.c:225-236 | Клиент шлёт вектор `iovec_s_t {cp_grant_id_t iov_grant; vir_bytes iov_size;}`, драйвер читает его как `iovec_t {vir_bytes iov_addr; vir_bytes iov_size;}` и затем приводит `(cp_grant_id_t)iov->iov_addr` (например, `drivers/storage/memory/memory.c:449`) | LAYOUT | На ILP32 оба типа — 8 байт, на LP64 — 16, и `iov_size` совпадает по смещению. Но `iov_addr` = grant (4 байта) + 4 байта неинициализированного паддинга со стека `bdev_vrdwt`. На LE усечение спасает, на BE-64 (mips64eb) grant окажется в старшей половине и будет неверным. Нужно единое определение протокола (`iovec_s_t` с обеих сторон или явная ширина `addr`) | MSG | 64 | M |
| minix/lib/libbdev/bdev.c:139,230 | `m_lbdev_lblockdriver_msg.count = count` (`size_t → int`) | MSG | Запрос > 2 ГБ даст отрицательный счётчик. Сейчас вызывающие ограничены кэшем, но на 64 предела типа нет | MSG | 64 | S |

Итог: работает на amd64 (LE), но с опорой на случайную раскладку iovec. Прочее безопасно:
`(vir_bytes)grant` в `driver.c:235`, `drvlib.c:76-117` (секторы в `unsigned long`, на LP64
проверка переполнения в `:112` становится мёртвой, но итог тот же), `trace.c`.
`partition.h` существует только для `arch/i386` и `arch/earm` (заметка ARCH для других разделов).

#### libchardriver, libinputdriver, libsockdriver, libsockevent

Без находок. Всё сводится к сужениям `ssize_t/size_t → int/socklen_t` для размеров, ограниченных
протоколом (`sockdriver.c:670,739` ограничивают `INT_MAX`), и к `m_notify.timestamp → clock_t`
(не 64-специфично).

#### libaudiodriver (minix/lib/libaudiodriver)

| Файл:строка | Что | Кат. | Почему проблема | Шаг | Проявл. | Сл. |
|---|---|---|---|---|---|---|
| minix/lib/libaudiodriver/audio_fw.c:799 (+ minix/include/minix/audio_fw.h:14) | `drv_set_dma(sub_dev_ptr->DmaPhys, …)` — `int drv_set_dma(u32_t dma, …)` | HW | Физический адрес DMA-буфера передаётся в драйверы звука как `u32_t`. Сейчас безопасно, потому что буфер выделяется с `AC_ALIGN64K\|AC_LOWER16M` (`audio_fw.c:764`). Корректность держится только на флаге: при 64-битном `phys_bytes`, PAE/LPAE или если VM не соблюдёт ограничение, адрес молча обрежется. Нужен `phys_bytes` + проверка `< 4G` | драйв. | >4Г | S |
| minix/lib/libaudiodriver/audio_fw.c:50,755-806,831 | `#if defined(__i386__)`, иначе `init_buffers` возвращает EIO | ARCH | Звук на любой другой архитектуре молча не работает | драйв. | — | S |

Итог: опасна только передача DMA-адреса в `u32_t`, и её защищает `AC_LOWER16M`. p64-сужения
`(phys_bytes)FragSize → size_t` (`audio_fw.c:664,679,721,736,784`) и `(vir_bytes)ReviveGrant`
(`:661,676,718,733`) безопасны.

#### libnetdriver (minix/lib/libnetdriver)

| Файл:строка | Что | Кат. | Почему проблема | Шаг | Проявл. | Сл. |
|---|---|---|---|---|---|---|
| minix/lib/libnetdriver/portio.c:33 | `sys_sdevio(req, port, endpt, (void *)data->iovec[i].iov_grant, …)` | PTR | Grant передаётся в ядро под видом указателя: int → `void *` со знаковым расширением, ядро приводит обратно. Работает, но это каламбур типов в интерфейсе libsys (`sys_sdevio`). Нужно отдельное поле grant (вместе с `do_sdevio`, раздел ядра) | amd64 | сборка | S |
| minix/lib/libnetdriver/netdriver.c:289 | `panic("…%d", r)`, где `r` — `ssize_t` | FMT | Несоответствие формата на LP64. Нужно `%zd` | amd64 | сборка | S |

Итог: на 64 переносим. Векторы `iovec_s_t` с lwip согласованы (обе стороны используют
`iovec_s_t`).

#### lwip (minix/net/lwip + minix/lib/liblwip/lib)

| Файл:строка | Что | Кат. | Почему проблема | Шаг | Проявл. | Сл. |
|---|---|---|---|---|---|---|
| minix/lib/liblwip/lib/lwipopts.h:58; minix/net/lwip/mempool.c:449 | `#define MEM_ALIGNMENT 4` (комментарий: «should be sizeof(void *) … TODO: 64-bit support»); `assert(sizeof(void *) == MEM_ALIGNMENT)` | CONST | На 64 без `NDEBUG` — panic в `mempool_init` при старте. С `NDEBUG` пулы `memp` и `mem` lwIP выровнены по 4, поэтому указатели и `u64` в pcb/pbuf невыровнены: на mips64 и riscv64 это исключение или эмуляция, на amd64 работает. Нужно 8 + обойти статические `#if`-проверки lwIP | amd64 | 64 | M |

Итог: глобально мешает только `MEM_ALIGNMENT`. Сетевые заголовки (lwIP `PACK_STRUCT_STRUCT =
__packed`, поля фиксированной ширины) и `bpf_hdr` (ABI с userland той же разрядности) переносимы.
Сужения в `bpfdev.c`, `ifconf.c`, `rtsock.c`, `ndev.c`, `util.c`, `tcpisn.c` безопасны
(ограниченные размеры; `ifdev_get_metric/mtu` дают u64 и на i386). В апстриме `dist/` ничего
64-специфичного не замечено: различия раскладки только у внутренних структур с указателями.

#### UDS (minix/net/uds)

Без находок. `CMSG_*` используются штатно, fd кладутся как `int`, `sockcred` фиксированного
размера, `stat.c:22` уже через `(uint64_t)(uintptr_t)`. Сужения `io.c:742,1435-1436`,
`uds.c:477,512` и `stat.c:109` ограничены `UDS_PATH_MAX`/`UDS_CTL_MAX`.

#### libsffs / libvtreefs

| Файл:строка | Что | Кат. | Почему проблема | Шаг | Проявл. | Сл. |
|---|---|---|---|---|---|---|
| minix/lib/libsffs/read.c:86,131 | `if (*posp < 0 \|\| *posp >= ULONG_MAX) return EINVAL;`, затем `t_readdir(…, pos - 2, …)` с `unsigned int index` | CONST | На LP64 граница `ULONG_MAX` (2^64−1) недостижима. Огромная позиция из `lseek` обрежется до `unsigned int`, и вместо EINVAL вернутся записи с другого индекса. Нужно `UINT_MAX` | amd64 | 64 | S |
| minix/lib/libvtreefs/file.c:204,242,259 | `if (*posp >= ULONG_MAX) return EIO;`, затем `get_inode_by_index(node, pos - 2)` (`index_t` = `int`), `skip = pos - indexed - 2` (`int`) | CONST | То же: проверка на LP64 мертва, позиция обрезается до `int` (возможен отрицательный индекс). Нужно `INT_MAX` | amd64 | 64 | S |

Итог: ошибки только в граничных проверках позиции каталога. Структуры внутренние.

#### Сводка раздела

| Категория | Мест в таблицах |
|---|---|
| PTR | 5 |
| CONST | 4 |
| ARCH | 4 |
| FMT | 4 |
| LAYOUT | 3 |
| BITS | 2 |
| ERR | 2 |
| ASM | 1 |
| MSG | 1 |
| HW | 1 |
| PHYS, VIR, TIME | 0 |
| **Всего строк** | **27** |

Сырьё, отнесённое к безопасному или не 64-специфичному (в таблицы не вошло):
- 54 сужения `ino_t/dev_t/time_t/off_t/u64/block64_t → 32` — не 64-специфичны: эти типы 64-битные и на i386 (лимиты форматов, `clock_t`).
- 51 `long → u32` из `conv4` (ext2 `inode.c` и `super.c`) — покрыты одной строкой BITS.
- 38 `ssize_t → int` — коды возврата, ограниченные размерами запросов.
- 57 `size_t → int/unsigned/socklen_t` — длины имён, блоков, адресов сокетов.
- ~40 прочих `unsigned long/long/vir_bytes → 32`: `(vir_bytes)grant`, `ex64lo/hi` → u32, индексы косвенных блоков, KB в эвристике кэша, `bit_t`.
- 3 `-Wsign-compare` — безопасны.
- 11 p64-сужений (`(phys_bytes)len → size_t` в `libfsdriver` и `libaudiodriver`) — длины, а не адреса.

##### Самые опасные места (топ-10)

1. `minix/lib/libfsdriver/call.c:241`: указатель от `mmap` записывается через `cp_grant_id_t` в union с `char *ptr`. На 64 page-in через `builtin_peek` разыменует мусорный указатель (PTR, 64).
2. `minix/lib/libvboxfs/vboxfs.h:40-100` и `dir.c:196-211`: раскладка структур протокола VirtualBox Shared Folders на 64 сдвигается на 4–8 байт (LAYOUT, 64).
3. `minix/lib/libhgfs/channel.c:95,136` и `backdoor.S`: адрес буфера для VMware backdoor передаётся в `u32_t`, ассемблер только i386 (PTR+ASM, сборка/64).
4. `minix/lib/liblwip/lib/lwipopts.h:58` и `minix/net/lwip/mempool.c:449`: `MEM_ALIGNMENT 4` и assert `sizeof(void*) == 4` — panic при старте сетевого стека на 64 (CONST, 64).
5. `minix/lib/libbdev/bdev.c:205-214` ↔ `minix/lib/libblockdriver/driver.c:225-236`: `iovec_s_t` отправляется, а читается как `iovec_t`. На LP64 в `iov_addr` попадает 4 байта мусора, работает только на LE (LAYOUT, 64).
6. `minix/lib/libaudiodriver/audio_fw.c:799`: физический DMA-адрес в `u32_t`. Защищён только флагом `AC_LOWER16M` (HW, >4Г).
7. `minix/fs/mfs/super.c:324-325`: `LONG_MAX` → `i32_t s_max_size` = −1 на 64 при некорректном суперблоке (CONST, 64).
8. `minix/fs/ext2/proto.h:37`, `minix/lib/libfsdriver/fsdriver.h:28,30`: несовпадение `int`/`ssize_t` — ошибка сборки (ERR).
9. `minix/lib/libsffs/read.c:86`, `minix/lib/libvtreefs/file.c:204`: граница `ULONG_MAX` на LP64 мертва (CONST, 64).
10. `minix/fs/mfs/utility.c:23`, `minix/fs/ext2/utility.c:49`: `conv4` на `long` с предположением о 32 битах (BITS, латентно).

##### Не уверен (нужен второй взгляд)

- **Раскладка, которую ждёт VirtualBox.** Предполагаю, что i386-раскладка MINIX (`crinfo` = 108,
  `objinfo` = 92) совпадает с хостовой (`AssertCompileSize(SHFLCREATEPARMS, …)` в
  `VBox/shflsvc.h`). В новых версиях `RTFSOBJATTR` union дополнен до 40 байт (`au64Padding[5]`),
  тогда `objinfo` = 96 и на i386 расходилась бы уже 32-битная версия. Сверить с заголовками
  используемой версии VirtualBox (`iprt/fs.h`, `VBox/shflsvc.h`), прежде чем фиксировать паддинг.
- **64-битный HGCM.** Нужен ли 64-битному гостю `VMMDevReq_HGCMCall64`
  (`HGCMFunctionParameter64`): это затрагивает `vbox_param_t` и драйвер `drivers/vmm_guest/vbox`
  (раздел драйверов).
- **`ex64lo/ex64hi` и `make64`.** В `minix/include/minix/u64.h:10-23` на LP64 они возвращают и
  принимают `unsigned long` (64 бита): `ex64lo` больше не обрезает до 32. В этом разделе все
  вызовы безопасны (присваивание в u32 или предварительная проверка `ex64hi(...) != 0` в
  `mfs/read.c:141`, `ext2/read.c:131`). Заголовок общий, проверить вызовы в других разделах.
- **`bio.c:158` (libminixfs).** `blocks_left` — `unsigned int` при `bytes` ≤ `SSIZE_MAX`. На 64
  теоретически переполняется (> 2^32 блоков за запрос), на практике `bytes` ограничен
  протоколом VFS. Проверить ширину поля `count` в сообщениях `bread/bwrite` после переработки
  сообщений.
- **Endianness bitmap'ов ext2.** Bitmap'ы ext2 читаются словами u32. Если в целях будет BE
  (mips64eb), формат ext2 и порядок `iovec_s_t`/`iovec_t` сломаются.

### 11.4 Драйверы ввода-вывода (кроме сетевых)

Охват: `minix/drivers/{storage,bus,iommu,clock,tty,video,power,system,hid,printer,sensors,eeprom,vmm_guest,audio,usb}`,
`minix/lib/{libvirtio,libddekit,libusb,libgpio,libi2cdriver,libclkconf,libacpi}`.
Сырьё `scan64.py` по разделу: m64 — 255 стр., p64 — 22, разные раскладки — 187. Драйверы только
для earm (mmc, i2c, gpio, fb/tda19988, tps*, sensors, eeprom, usbd/musb, tty arch/earm) прочитаны вручную.

Общие замечания, которые относятся ко многим строкам ниже:
- **Нет способа попросить DMA-память ниже 4 ГБ.** `alloc_contig()` знает только `AC_LOWER16M`/`AC_LOWER1M`
  (`minix/include/minix/syslib.h:99-102`). Все драйверы, у которых в регистр/дескриптор пишется только 32-битный адрес
  (at_wini, AHCI без S64A, VMMDev, amddev, OMAP DSS), выделяют буферы с флагом `0`/`AC_ALIGN4K`, то есть память может
  оказаться выше 4 ГБ. Нужен `AC_LOWER4G` (в VM — `PAF_LOWER4GB`) или отказ/bounce-буфер в драйвере. Шаг: А1/А3.
- **`vm_map_phys(endpoint_t, void *physaddr, size_t)`** (`minix/include/minix/vm.h:16`) передаёт физический адрес
  через указатель. На PAE/LPAE (32-битный указатель, 64-битный `phys_bytes`) адрес обрезается. Это затрагивает всех
  вызывающих на разделе: ahci, memory, pci, tty/console, ti1225, audio, acpi, fb, mmc, i2c, rtc, gpio, clkconf,
  ddekit. Отдельно по каждому не перечисляю, в таблицах отмечен только /dev/mem. Шаг: А1 (переделать интерфейс на `phys_bytes`).
- **`sys_outl(p,v)`** — макрос с явным `(u32_t)` (`minix/include/minix/syslib.h:219`), поэтому clang молчит, когда туда
  уходит `phys_bytes` (at_wini, vbox).
- **Псевдогрант «указатель в `cp_grant_id_t`»**: когда драйвер вызывает собственный путь ввода-вывода (`SELF`/`KERNEL`),
  виртуальный адрес кладётся в поле гранта `int32_t`. На LP64 указатель обрезается — это затрагивает tty, pty, vnd, floppy.
- **`MACHINE_ARCH == "i386"`** в Makefile: на x86_64 почти весь раздел выпадает из сборки (см. раздел ARCH в конце).

---

#### at_wini (minix/drivers/storage/at_wini)

| Файл:строка | Что | Кат. | Почему проблема | Шаг | Проявл. | Сл. |
|---|---|---|---|---|---|---|
| minix/drivers/storage/at_wini/at_wini.c:97-103 | `struct prdte { phys_bytes prdte_base; u16_t count; u8_t res; u8_t flags; }` — аппаратная PRD-таблица Bus Master IDE | HW/LAYOUT | Железо ждёт запись по 8 байт: `u32 base, u16 count, u16 (EOT в бите 15)`. Если `phys_bytes` 64-битный (LP64 **и** PAE с 64-битным `phys_bytes`), поле base занимает 8 байт и запись растёт до 16. Контроллер читает старшие 32 бита адреса как count/flags (count=0 означает 64 КБ, EOT не виден) и уходит за конец таблицы: DMA пишет в случайную память. Нужно `u32_t prdte_base` и проверка адреса < 4 ГБ | драйв. | оба | S |
| minix/drivers/storage/at_wini/at_wini.c:1360-1397 | `sys_umap(...,&user_phys)` → `prdt[j].prdte_base = user_phys` без проверки диапазона | HW/PHYS | В BM-IDE PRD только 32-битный адрес. Буфер пользователя выше 4 ГБ молча обрежется (когда поле станет u32) или испортит PRD (сейчас). Нужен отказ (`return 0` → PIO) или bounce через `dma_buf` | драйв. | >4Г | S |
| minix/drivers/storage/at_wini/at_wini.c:270-271 | `alloc_contig(ATA_DMA_BUF_SIZE, 0, &dma_buf_phys)`, `alloc_contig(PRDT_BYTES, 0, &prdt_phys)` | HW/PHYS | PRDT и DMA-буфер должны лежать ниже 4 ГБ, но ограничения нет (`AC_LOWER4G` отсутствует) | А1/драйв. | >4Г | S |
| minix/drivers/storage/at_wini/at_wini.c:1432 | `sys_outl(base_dma + DMA_PRDTP, prdt_phys)` | HW | Регистр PRDTP 32-битный; явное `(u32_t)` в макросе прячет обрезку, если PRDT выше 4 ГБ | драйв. | >4Г | S |
| minix/drivers/storage/at_wini/at_wini.c:1009,1041,1064,865-866 | `unsigned long block` → `do_transfer(..., unsigned int sector, ...)`, `sector_high = 0` | VIR | Номер сектора 32-битный (предел 2 ТБ, LBA48 используется только наполовину). На LP64 `unsigned long` → `unsigned` молча усекается. Ограничение существующее, 64-битный режим его не меняет | — | 64 | S |
| minix/drivers/storage/at_wini/at_wini.c:967-968,1418,1431 | `%d` для `wn - wini` (ptrdiff_t), `%lx` для `prdte_base`/`prdt_phys` (`phys_bytes`) | FMT | Неверная ширина в printf (на p64 `%lx` при 64-битном `phys_bytes`) | драйв. | оба | S |

Итог: на 64 битах DMA at_wini сразу ломается из-за раскладки `struct prdte` (16 байт вместо 8), и то же на PAE при 64-битном
`phys_bytes`. Кроме того, нет ни одной проверки «адрес < 4 ГБ» для PRD, PRDT и DMA-буфера.

#### ahci (minix/drivers/storage/ahci)

| Файл:строка | Что | Кат. | Почему проблема | Шаг | Проявл. | Сл. |
|---|---|---|---|---|---|---|
| minix/drivers/storage/ahci/ahci.c:834-838 | `ct_set_prdt`: `*p++ = prdt->vp_addr; *p++ = 0;` (DBA, DBAU=0) | HW/PHYS | `prd_t` = `struct vumap_phys` — это **внутренняя** структура: в аппаратную PRD она перекладывается вручную по 4 слова u32, поэтому раскладка не ломается. Но в DBAU всегда 0, а младшие 32 бита берутся неявным сужением: буфер выше 4 ГБ уйдёт по неверному адресу | драйв. | >4Г | S |
| minix/drivers/storage/ahci/ahci.c:887-894 | `memset(cl,...)`; `cl[2] = ps->ct_phys[cmd];` (CTBA; CTBAU=cl[3]=0) | HW/PHYS | Командная таблица адресуется только младшими 32 битами | драйв. | >4Г | S |
| minix/drivers/storage/ahci/ahci.c:1930-1934 | `port_write(FBU, 0); port_write(FB, fis_phys); port_write(CLBU, 0); port_write(CLB, cl_phys);` | HW/PHYS | Старшие половины FB/CLB всегда 0 | драйв. | >4Г | S |
| minix/drivers/storage/ahci/ahci.h:150-155 (нет S64A) | бит `CAP.S64A` (bit 31) не определён и не проверяется | HW | Чтобы поддержать память выше 4 ГБ, надо: если S64A=1, писать старшие половины (DBAU/CTBAU/CLBU/FBU); если S64A=0, выделять ниже 4 ГБ и отвергать пользовательские буферы выше 4 ГБ (`setup_prdt`, ahci.c:1100-1124) | драйв. | >4Г | M |
| minix/drivers/storage/ahci/ahci.c:1000,1906 | `alloc_contig(pad_size, 0, &pad_phys)`, `alloc_contig(mem_size, AC_ALIGN4K, &mem_phys)` | PHYS | FIS, CL, CT и TMP могут оказаться выше 4 ГБ, а записываются только младшие 32 бита | А1/драйв. | >4Г | S |
| minix/drivers/storage/ahci/ahci.c:1110-1124 | после `sys_vumap` проверяется только `vp_addr & 1` | HW | Нет проверки `vp_addr >> 32` при S64A=0 | драйв. | >4Г | S |
| minix/drivers/storage/ahci/ahci.c:2068,2087 | `u32_t base, size` из `pci_get_bar`; `vm_map_phys(SELF,(void*)base,size)` | PHYS | ABAR (BAR5) по спецификации 32-битный — допустимо; следствие общего интерфейса `pci_get_bar` (u32, см. pci) | — | — | S |
| minix/drivers/storage/ahci/ahci.c:394,1155,1428 | `%llx`/`%llu` для `u64_t` | FMT | На LP64 `u64_t` = `unsigned long`: только предупреждение, ширина совпадает | драйв. | 64 | S |

Итог: раскладка PRD сделана правильно (вручную по u32), но поддержки 64-битной адресации нет совсем: S64A не
проверяется, все `*U`-регистры и поля равны 0. На 64-битной машине с памятью > 4 ГБ AHCI-DMA повредит память.

#### virtio_blk + libvirtio (minix/drivers/storage/virtio_blk, minix/lib/libvirtio)

| Файл:строка | Что | Кат. | Почему проблема | Шаг | Проявл. | Сл. |
|---|---|---|---|---|---|---|
| minix/lib/libvirtio/virtio.c:508 | `vd->addr = vp->vp_addr & ~1UL;` | PHYS/CONST | `vring_desc.addr` — полный u64 (virtio_ring.h:61-70, раскладка 16 байт на обеих ABI — ОК). Но на ILP32 `~1UL` = `0xFFFFFFFE`: при 64-битном `phys_bytes` (PAE) старшие 32 бита адреса **обнуляются**. Нужно `~(phys_bytes)1` | А1 | >4Г | S |
| minix/lib/libvirtio/virtio.c:58,387,301 | `u32_t page; q->page = q->paddr / PAGE_SIZE; virtio_write32(QADDR, q->page)` | HW | Legacy-регистр QUEUE_PFN 32-битный: предел 16 ТБ (практически достаточно), но сужение неявное. Важнее, что PFN по спецификации legacy virtio считается в единицах 4096, а не `PAGE_SIZE`: при другом размере страницы (aarch64 с 16K/64K) адрес будет неверным. То же касается `vring_init(..., PAGE_SIZE)` (virtio.c:390) — нужна константа `VIRTIO_PCI_VRING_ALIGN=4096` | драйв./amd64 | 64 | S |
| minix/lib/libvirtio/virtio.c:322,436 | `alloc_contig(ring_size, AC_ALIGN4K, &paddr)` | PHYS | Для legacy virtio ограничение ниже 4 ГБ не нужно (PFN до 2^44) — ОК | — | — | — |
| minix/lib/libvirtio/virtio_ring.h:145 | `((unsigned long)&vr->avail->ring[num] + ...)` → `unsigned` в `vring_size` | VIR | Безопасно (размер кольца мал) | — | — | — |
| minix/drivers/storage/virtio_blk/virtio_blk.c:182,188 | `%08lx` для `vp_addr` (`phys_bytes`) | FMT | На p64 неверная ширина | драйв. | >4Г | S |
| minix/drivers/storage/virtio_blk/virtio_blk.h:47,104 | `virtio_blk_config` (packed), `virtio_blk_outhdr` (u32,u32,u64 = 16) | LAYOUT | sizeof совпадает на обеих ABI, отличается только align — ОК | — | — | — |

Итог: virtio подготовлен лучше остальных (u64 в дескрипторе). Опасность одна — `~1UL` на PAE.
Для aarch64 ещё нужно отвязать PFN и выравнивание кольца от `PAGE_SIZE`. Транспорт только legacy PCI через I/O-порты:
на ARM (MMIO/modern virtio) его нужно писать заново (ARCH).

#### floppy (minix/drivers/storage/floppy)

| Файл:строка | Что | Кат. | Почему проблема | Шаг | Проявл. | Сл. |
|---|---|---|---|---|---|---|
| minix/drivers/storage/floppy/floppy.c:463,554,620,646 | `cp_grant_id_t ugrants[]` = `iov_addr`; для `SELF`: `memcpy(floppy_buf, (void *)(*ug + *up), ...)` | PTR | Для вызова из самого драйвера (SELF) в `iov_addr` лежит виртуальный адрес, а он сохраняется в int32 `cp_grant_id_t`, поэтому на LP64 указатель обрезается | драйв. | 64 | S |
| minix/drivers/storage/floppy/floppy.c:336-337,706,717-719 | `alloc_contig(..., AC_LOWER16M\|AC_ALIGN4K, ...)`, проверка `& DMA_ADDR_MASK`, 24-битный ISA DMA | HW | Корректно (ISA DMA < 16 МБ, проверка есть) | — | — | — |
| minix/drivers/storage/floppy/floppy.c:771,847,1125,1176 | `m_notify.timestamp` (u64) → `clock_t` | TIME | Тики усекаются до 32 бит; таймеры MINIX переживают переполнение — безопасно | — | — | — |

Итог: ISA DMA сделан правильно. Остаётся путь SELF с указателем в гранте; сам драйвер на amd64 не нужен.

#### memory (minix/drivers/storage/memory)

| Файл:строка | Что | Кат. | Почему проблема | Шаг | Проявл. | Сл. |
|---|---|---|---|---|---|---|
| minix/drivers/storage/memory/memory.c:417 | `static int m_block_transfer(...)` при `bdr_transfer` типа `ssize_t (*)()` | ERR | На LP64 `ssize_t` ≠ `int`, поэтому «conflicting types» — ошибка сборки | amd64 | сборка | S |
| minix/drivers/storage/memory/memory.c:558-572 | `u32_t a = m_vaddrs[minor]; ... munmap((void *) a, size)` | PTR | Виртуальный адрес RAM-диска хранится в u32, на LP64 munmap получит обрезанный адрес | amd64 | 64 | S |
| minix/drivers/storage/memory/memory.c:157 | `m_geom[MEM_DEV].dv_size = 0xffffffffULL` | CONST/PHYS | /dev/mem покрывает только первые 4 ГБ физической памяти | А1 | >4Г | S |
| minix/drivers/storage/memory/memory.c:254 | `vm_map_phys(SELF, (void *) pagestart, PAGE_SIZE)` | PHYS | На PAE адрес страницы > 4 ГБ обрезается указателем (общий дефект интерфейса `vm_map_phys`) | А1 | >4Г | M |
| minix/drivers/storage/memory/memory.c:520 | `u32_t ramdev_size` (протокол MIOCRAMSIZE) | VIR | RAM-диск ограничен 4 ГБ; ioctl-протокол с userland (`ramdisk(8)`) — оставить фиксированную ширину | — | — | — |
| minix/drivers/storage/memory/memory.c:367 | `#if defined(__i386__)` вокруг `sys_enable_iop` | ARCH | На amd64 тоже нужен доступ к портам ввода-вывода (`/dev/mem` → IOPL) | amd64 | 64 | S |

Итог: на LP64 memory не собирается (`m_block_transfer`), выгрузка RAM-диска обрезает указатель, а /dev/mem видит только 4 ГБ.

#### fbd, filter, vnd, ramdisk (minix/drivers/storage/{fbd,filter,vnd,ramdisk})

| Файл:строка | Что | Кат. | Почему проблема | Шаг | Проявл. | Сл. |
|---|---|---|---|---|---|---|
| minix/drivers/storage/fbd/fbd.c:37 | `.bdr_transfer = fbd_transfer` (возвращает `int`) | ERR | Несовместимый тип указателя на функцию — в clang ≥16 ошибка по умолчанию | amd64 | сборка | S |
| minix/drivers/storage/fbd/fbd.c:435 | `rule_post_hook(osize, &r)` — `ssize_t *` в `int *` | PTR | Хук пишет 4 байта в 8-байтный `r`; при отрицательном результате старшая половина остаётся мусором | amd64 | 64 | S |
| minix/drivers/storage/fbd/action.c:18,30 | `val = lrand48() ^ (lrand48() << 1)` в `u32_t` | — | Безопасно (берутся младшие 32 бита) | — | — | — |
| minix/drivers/storage/vnd/vnd.c:42 | `.bdr_transfer = vnd_transfer` (`int`) | ERR | Как у fbd | amd64 | сборка | S |
| minix/drivers/storage/vnd/vnd.c:147 | `ptr = (char *) iov->iov_grant + iov_off;` (endpt == SELF) | PTR | Указатель в int32-поле гранта `iovec_s_t.iov_grant` | драйв. | 64 | S |
| minix/drivers/storage/filter/crc.c:66-86 | `unsigned long s; i = (s >> 24) ^ byte; s = (s << 8) ^ crctab[i];` | BITS | Алгоритм рассчитан на 32-битный `long`. На LP64 `s` не усекается, поэтому `s>>24` больше 255 и `crctab[i]` читается **за границей массива**, а CRC получается другим | драйв. | 64 | S |
| minix/drivers/storage/filter/sum.c:42-80 | `unsigned long *q = (unsigned long *)sum; *q = sector / crc ^ sector` при `SUM_SIZE = 4` (main.c:270,276) | LAYOUT/BITS | Контрольные суммы хранятся **на диске**. На LP64 пишется 8 байт в слот из 4: затирается сумма соседнего сектора, а последняя выходит за буфер. Формат несовместим с 32-битным | драйв. | 64 | S |
| minix/drivers/storage/filter/md5.h:11 | `typedef unsigned long uint32;` (`struct MD5Context` 88→112) | BITS | MD5 Колина Пламба формально допускает «≥32 бит», но корректность при 64-битном `uint32` не проверена (см. «Не уверен»). Лучше `uint32_t` или NetBSD `<sys/md5.h>` | драйв. | 64 | S |
| minix/drivers/storage/filter/inc.h:81 | `typedef unsigned long sector_t` → `calc_sum(unsigned sector, …)` (sum.c:143,167,239,268,297,394) | VIR | Сектор усекается до 32 бит — существующий предел 2 ТБ | — | — | — |
| minix/drivers/storage/ramdisk/proto:5,21,36; proto.common.etc:9 | `#ifdef __i386__` / `__arm__` при выборе содержимого образа | ARCH | Для новой архитектуры ветки нет | amd64 | сборка | S |

Итог: fbd/vnd (и mmcblk, см. ниже) не соберутся из-за `int` вместо `ssize_t` в `bdr_transfer`. filter на LP64 даёт
выход за границы массива (CRC) и портит свой дисковый формат контрольных сумм.

#### mmc (minix/drivers/storage/mmc, earm)

| Файл:строка | Что | Кат. | Почему проблема | Шаг | Проявл. | Сл. |
|---|---|---|---|---|---|---|
| minix/drivers/storage/mmc/mmcblk.c:47,280 | `static int block_transfer(...)` для `bdr_transfer` | ERR | Тип возврата `int` вместо `ssize_t` | aarch64 | сборка | S |
| minix/drivers/storage/mmc/mmchost_mmchs.c:141-142 | `mmchs->io_base = (uint32_t) vm_map_phys(...)` | PTR | MMIO-адрес обрезается до 32 бит, хотя `io_base` объявлен `vir_bytes` (omap_mmc.h:4) | aarch64 | 64 | S |
| minix/drivers/storage/mmc/emmc.c:600; 93,113 | `v_base = (uint32_t)vm_map_phys(...)`; `spin_until_set(uint32_t address, …)` | PTR | Аналогично; все регистры адресуются через `read32/write32(uint32_t)` из `minix/mmio.h` | aarch64 | 64 | S |
| minix/drivers/storage/mmc/mmchost_mmchs.c:40 | `#define USE_DMA` (DMA фактически не используется, только PIO) | HW | Адресов DMA нет — ОК | — | — | — |
| minix/drivers/storage/mmc/mmcblk.c:110 | `#ifdef __arm__` вокруг `host_initialize_host_structure_mmchs` | ARCH | На aarch64 (`__aarch64__`) хост-контроллер молча не инициализируется | aarch64 | 64 | S |

#### Общий заголовок MMIO для earm (minix/include/minix/mmio.h — используется §11.4)

| Файл:строка | Что | Кат. | Почему проблема | Шаг | Проявл. | Сл. |
|---|---|---|---|---|---|---|
| minix/include/minix/mmio.h (write32/read32/set32/write16/read16/set16) | `static inline void write32(uint32_t address, uint32_t value) { REG(address) = value; }` | PTR/HW | Адрес MMIO передаётся как `uint32_t`, поэтому на 64 битах все обращения к регистрам идут по обрезанному адресу. Пользователи: omap_i2c.c, omap_rtc.c (свои макросы, ОК), emmc.c, mmchost_mmchs.c, gpio.c, libclkconf, libgpio, ядро earm (omap_padconf.c, omap_timer.c). Нужен `vir_bytes`/`volatile void *` | aarch64 | 64 | M |

#### pci (minix/drivers/bus/pci)

| Файл:строка | Что | Кат. | Почему проблема | Шаг | Проявл. | Сл. |
|---|---|---|---|---|---|---|
| minix/drivers/bus/pci/pci.c:79-85 | `struct bar { ... u32_t pb_base; u32_t pb_size; }` | PHYS/HW | BAR хранится в 32 битах | драйв./А1 | >4Г | M |
| minix/drivers/bus/pci/pci.c:1064-1092 | 64-битный BAR с ненулевой старшей половиной **игнорируется** | HW | На 64-битных машинах прошивка обычно размещает 64-битные prefetchable BAR (NVMe, GPU, часть NIC) выше 4 ГБ, такие устройства станут недоступны | драйв. | >4Г | M |
| minix/drivers/bus/pci/pci.c:2444; minix/include/minix/syslib.h:250; main.c:479-494 | `_pci_get_bar(int, int, u32_t *base, u32_t *size, int *)`, ответ в `m_pci_lsys_busc_get_bar.base/size` | MSG/PHYS | Интерфейс и сообщение рассчитаны на 32-битный BAR: нужны `phys_bytes`/u64 base и size | MSG | >4Г | L |
| minix/drivers/bus/pci/pci.c:1323-1332 | `u32_t memgap_low = kinfo.mem_high_phys; memgap_high = 0xfe000000` | PHYS/CONST | Если памяти больше 4 ГБ, `mem_high_phys` усекается и окно для назначения BAR (`complete_bars`) считается неверно: BAR может попасть поверх RAM | А1/драйв. | >4Г | S |
| sys/dev/pci/pciio.h:114-122 (MINIX-добавка) | `struct pciio_map { ...; u_int phys_offset; ... }` (PCI_IOC_MAP, main.c:588-609) | PHYS/LAYOUT | Физический адрес отображения в userland только 32-битный | драйв. | >4Г | S |
| minix/drivers/bus/pci/pci.c:117-160 и далее | доступ к конфигурационному пространству только через порты 0xCF8/0xCFC (`PCII_*`, `sys_outl`) | ARCH | Нет ECAM (MCFG): на arm/aarch64/riscv PCIe так не работает, расширенное пространство (>256 байт) недоступно и на x86 | драйв. | 64 | L |
| minix/drivers/bus/pci/main.c:185,221,466,609; pci.c:2098 | `strlen` → int, `m2_l1` → u32, `env_parse` long → int, `(void *)map.phys_offset` | — | Безопасные сужения | — | — | — |

Итог: PCI-сервер полностью 32-битный по адресам BAR (хранение, интерфейс `pci_get_bar`, сообщение, ioctl).
64-битные BAR выше 4 ГБ отбрасываются — это основной блокер для реального железа на amd64.

#### ti1225 (minix/drivers/bus/ti1225)

| Файл:строка | Что | Кат. | Почему проблема | Шаг | Проявл. | Сл. |
|---|---|---|---|---|---|---|
| minix/drivers/bus/ti1225/ti1225.c:158-164 | `v32 = pci_attr_r32(devind, TI_CB_BASEADDR); csr_ptr = vm_map_phys(SELF,(void*)v32,...)` | PHYS | Base CardBus 32-битный по стандарту — ОК; `struct csr` (ti1225.h:20) — оверлей из u32, раскладка верная | — | — | — |

#### amddev — AMD DEV (minix/drivers/iommu/amddev)

| Файл:строка | Что | Кат. | Почему проблема | Шаг | Проявл. | Сл. |
|---|---|---|---|---|---|---|
| minix/drivers/iommu/amddev/amddev.c:288-289 | таблица DEV (битовая карта на 1M страниц = 4 ГБ), `alloc_contig(size, AC_ALIGN4K, &busaddr)` | HW/PHYS | Таблица должна лежать ниже 4 ГБ (см. следующую строку), но ограничения нет | А1/драйв. | >4Г | S |
| minix/drivers/iommu/amddev/amddev.c:307-308 | `write_reg(DEVF_BASE_HI, index, 0); write_reg(DEVF_BASE_LO, index, busaddr \| 3)` | HW | Старшая половина базы всегда 0, младшая берётся неявным сужением | драйв. | >4Г | S |
| minix/drivers/iommu/amddev/amddev.c:442-446 | `u32_t bit = (busaddr+o)/PAGE_SIZE; table[bit/8] &= ...` | HW/BITS | Таблица покрывает только 4 ГБ. Для `busaddr` ≥ 4 ГБ индекс выходит за 128-КБ буфер: **запись за границу кучи**, и защита для этой страницы не снимается | драйв. | >4Г | S |
| minix/drivers/iommu/amddev/amddev.c:296,305,391,402,413 | `%x` для `size_t`, `%lx` для `phys_bytes` | FMT | Неверная ширина | драйв. | оба | S |
| minix/drivers/iommu/amddev/amddev.c:383-384 | `start = m->m2_l1; size = m->m2_l2` (long) | MSG | Виртуальный адрес в `long`-поле — на LP64 ширина хватает, но поле должно стать фиксированным при переработке сообщений | MSG | — | S |

Итог: amddev — это AMD DEV (Device Exclusion Vector), а не IOMMU с таблицами страниц. Схема принципиально ограничена
4 ГБ, а для адресов выше 4 ГБ возникает запись за границу кучи. Драйвер устарел (DEV есть только у K8/K10); для amd64 его
лучше удалить, чем чинить.

#### readclock (minix/drivers/clock/readclock)

| Файл:строка | Что | Кат. | Почему проблема | Шаг | Проявл. | Сл. |
|---|---|---|---|---|---|---|
| minix/drivers/clock/readclock/Makefile:5 | `.include "arch/${MACHINE_ARCH}/Makefile.inc"` (есть только i386, earm) | ARCH/ERR | Для x86_64/aarch64 каталога нет — сборка упадёт | amd64 | сборка | S |
| minix/drivers/clock/readclock/readclock.h:7 | `struct rtc` (указатели на функции) 20→40 | — | Внутренняя структура, ОК | — | — | — |

#### tty, pty (minix/drivers/tty/{tty,pty})

| Файл:строка | Что | Кат. | Почему проблема | Шаг | Проявл. | Сл. |
|---|---|---|---|---|---|---|
| minix/drivers/tty/tty/tty.c:247,259,433 | `do_write(..., KERNEL, (cp_grant_id_t) buf, ...)` | PTR | Указатель на буфер передаётся через int32 `cp_grant_id_t` (псевдогрант для KERNEL) | драйв. | 64 | S |
| minix/drivers/tty/tty/arch/i386/console.c:188; arch/i386/rs232.c:273; arch/earm/rs232.c:241; minix/drivers/tty/pty/pty.c:559 | `memcpy(..., (char *) tp->tty_outgrant + tp->tty_outcum, ...)` при `tty_outcaller == KERNEL` | PTR | Обратная сторона того же: указатель восстанавливается из обрезанного int32 | драйв. | 64 | S |
| minix/drivers/tty/tty/Makefile:4 | `.include "arch/${MACHINE_ARCH}/Makefile.inc"` (i386, earm) | ARCH/ERR | Для новой архитектуры каталога нет | amd64 | сборка | S |
| minix/drivers/tty/tty/arch/i386/rs232.c:167,196,370,381,388 | `#if defined(__i386__)` | ARCH | На x86_64 ветки выпадут | amd64 | 64 | S |
| minix/drivers/tty/tty/arch/i386/console.c:963-985,1047,1192,1216-1217 | `phys_bytes vid_size` и т.п. → `unsigned`/`int` | — | Размеры видеопамяти (≤ 32 КБ); адрес 0xB8000 ниже 1 МБ — безопасно | — | — | — |
| minix/drivers/tty/tty/arch/earm/rs232.c:96,524 | `phys_bytes phys_base` хранит **виртуальный** адрес из `vm_map_phys` | VIR | Неверный тип по смыслу; на LP64 ширина совпадает, на p64 просто шире — ОК | — | — | — |
| tty.c:183,194; pty/tty.c:150 | `u64` timestamp → `clock_t` | TIME | Безопасно (переполнение тиков учитывается) | — | — | — |

Итог: единственная реальная проблема — передача указателя на буфер ядра через поле гранта (int32).

#### fb, tda19988 (minix/drivers/video, earm)

| Файл:строка | Что | Кат. | Почему проблема | Шаг | Проявл. | Сл. |
|---|---|---|---|---|---|---|
| minix/drivers/video/fb/fb.c:373,386-389 | `*((u32_t *)((u32_t) dev.dv_base + i)) = ...` | PTR | Виртуальный адрес кадрового буфера приводится к u32 | aarch64 | 64 | S |
| minix/drivers/video/fb/arch/earm/fb_arch.c:398,246-247 | `alloc_contig(fb_size, 0, &fb_phys)`; `writew(GFX_BA0, fb_phys + off)` | HW/PHYS | Регистр DSS GFX_BA0 32-битный, а буфер выделяется без ограничения (LPAE > 4 ГБ) | драйв. | >4Г | S |
| minix/include/minix/fb.h:12-13 | `struct fb_fix_screeninfo { ... phys_bytes mmio_start; size_t mmio_len; }` | LAYOUT | ioctl-структура зависит от `phys_bytes`/`size_t`; userland пересобирается вместе — ОК, но при 64-битном `phys_bytes` на 32 битах меняются её размер и номер ioctl | — | — | — |
| minix/drivers/video/tda19988/tda19988.c | — | — | Проблем не найдено (I2C через libi2cdriver, `ssize_t` верный) | — | — | — |

#### acpi — обвязка MINIX (minix/drivers/power/acpi, minix/lib/libacpi)

| Файл:строка | Что | Кат. | Почему проблема | Шаг | Проявл. | Сл. |
|---|---|---|---|---|---|---|
| minix/drivers/power/acpi/include/platform/acminix.h:136 | `#define ACPI_MACHINE_WIDTH 32` безусловно | ARCH/PHYS | На LP64 ACPICA остаётся 32-битной: `ACPI_SIZE`/`ACPI_PHYSICAL_ADDRESS` = UINT32, указатели приводятся к 32 битам. Отсюда все предупреждения ACPICA в сырье (tbinstal.c, tbdata.c, utaddress.c, rslist.c, rsmisc.c, hwvalid.c, evgpeinit.c — 13 мест). Нужно `#ifdef _LP64 → 64`. Заодно 64-битные адреса из XSDT/FADT (X_*) на 32 битах усекаются; для PAE нужен `ACPI_32BIT_PHYSICAL_ADDRESS` off | amd64 | оба | S |
| minix/drivers/power/acpi/include/platform/acminix.h:142-174 | `minix_div_64_by_32` / `minix_shr_64` — inline-asm x86 (`div`, `rcrl`) для `ACPI_DIV_64_BY_32` | ASM/ARCH | На не-x86 не соберётся; при ширине 64 не нужно (`ACPI_USE_NATIVE_DIVIDE`). `minix_shr_64` ещё и написан с ошибкой (`"=m"(h)` — указатель, а не значение) | amd64 | сборка | S |
| minix/drivers/power/acpi/acpi.c:16-17 | `mr.mr_base = 0; mr.mr_limit = 0xffffffff;` (SYS_PRIV_ADD_MEM) | CONST/PHYS | Права на память только до 4 ГБ: таблицы ACPI выше 4 ГБ (64-битный XSDT) недоступны | А1/драйв. | >4Г | S |
| minix/drivers/power/acpi/osminixxf.c:370 | `AcpiOsMapMemory: vm_map_phys(SELF, (void *) where, length)` | PHYS | Физический адрес через указатель (PAE) | А1 | >4Г | S |
| minix/drivers/power/acpi/osminixxf.c:929-932; minix/include/minix/type.h:127 | `AcpiOsGetRootPointer` → `machine.acpi_rsdp` (`phys_bytes`) → `ACPI_PHYSICAL_ADDRESS` (u32) | PHYS | RSDP ищется ядром в BIOS-области (< 1 МБ) — ОК для BIOS; при загрузке через UEFI/U-Boot адрес RSDP может быть выше 4 ГБ | Б2/В4 | >4Г | S |
| minix/drivers/power/acpi/osminixxf.c:851-885 | `AcpiOsReadMemory/WriteMemory` — заглушки (FIXME) | HW | Не 64-битная проблема, но регистры ACPI в SystemMemory (обычные на ARM/HW-reduced ACPI) не работают | драйв. | — | M |
| minix/include/minix/acpi.h:7-44; minix/lib/libacpi/acpi.c:45-82; drivers/power/acpi/acpi.c:148,pci.c:74-123 | `((struct acpi_get_irq_req *)&m)->...` — свои структуры, наложенные на `message` | MSG | Полагаются на заголовок `message` вида `{endpoint_t; int m_type}` и на 56 байт: `request` лежит поверх `m_type`. При переработке сообщений перевести на типизированные `m_*` | MSG | — | S |
| minix/drivers/power/acpi/include/platform/aclinux.h:182, accygwin.h:67 | `__x86_64__` в чужих платформенных заголовках | — | Не используются (берётся acminix.h) | — | — | — |

Итог: ACPI на 64 битах соберётся, но останется 32-битной (`ACPI_MACHINE_WIDTH 32`) — это самая простая и самая
важная правка. Привилегия на память и `vm_map_phys` ограничены 4 ГБ.

#### tps65217, tps65950, sensors, eeprom, libi2cdriver, i2c (earm)

| Файл:строка | Что | Кат. | Почему проблема | Шаг | Проявл. | Сл. |
|---|---|---|---|---|---|---|
| minix/drivers/bus/i2c/arch/earm/omap_i2c.c:88,302,475-552 | `vir_bytes mapped_addr` → `read16/write16/set16(uint32_t addr)` из `mmio.h` | PTR | Адрес MMIO обрезается при вызове (см. mmio.h) | aarch64 | 64 | S |
| minix/drivers/eeprom/cat24c256/cat24c256.c:141-151 | `iov_size`/`dv_size - position` → `unsigned` | — | Безопасно (размер EEPROM 32 КБ) | — | — | — |
| minix/include/minix/i2c.h:16-23 | `minix_i2c_ioctl_exec_t` (буферы встроены, `size_t` длины) | LAYOUT | Передаётся по гранту между процессами одной ABI — ОК | — | — | — |
| tps65217.c:256,260; tps65950.c:160; sensors/*: `(int) value` из `env_parse` | — | — | Безопасно | — | — | — |

#### gpio, libgpio, libclkconf (earm)

| Файл:строка | Что | Кат. | Почему проблема | Шаг | Проявл. | Сл. |
|---|---|---|---|---|---|---|
| minix/lib/libgpio/gpio_omap.c:59-61,437-441 | `uint32_t base_address = (uint32_t) vm_map_phys(...)`; `== (uint32_t) MAP_FAILED` | PTR | MMIO-указатель в u32; далее `read32/set32(base_address + regs->X)` (271-319, 362-365, 446) | aarch64 | 64 | S |
| minix/lib/libgpio/gpio_omap.c:179-190 | `gpio_omap_regs_t` из `vir_bytes` (40→80) | — | Это таблица смещений, а не оверлей регистров — ОК | — | — | — |
| minix/lib/libclkconf/clkconf.c:32,66-68,78,88 | `static u32_t base = (uint32_t) vm_map_phys(...)`; `vm_unmap_phys(SELF,(void*)base,...)` | PTR | То же | aarch64 | 64 | S |
| minix/drivers/system/gpio/gpio.c:52-57 | `struct gpio_cbdata` (указатели) | — | Внутренняя — ОК | — | — | — |

#### log, random, printer, pckbd (minix/drivers/{system,printer,hid})

| Файл:строка | Что | Кат. | Почему проблема | Шаг | Проявл. | Сл. |
|---|---|---|---|---|---|---|
| minix/drivers/system/random/aes/rijndael_api.c:143,191,243-245,308 | `aligned(p)`: `((unsigned) p & 3) == 0` | PTR | Проверяются только младшие биты, усечение безопасно (но лучше `uintptr_t`) | — | — | — |
| log.c:183,238,263; random.c:99,198; printer.c:179-180 | `size_t`/`long` → `int` | — | Безопасно (размеры буферов) | — | — | — |
| hid/pckbd | — | — | Проблем не найдено | — | — | — |

#### vbox — VirtualBox VMMDev (minix/drivers/vmm_guest/vbox)

| Файл:строка | Что | Кат. | Почему проблема | Шаг | Проявл. | Сл. |
|---|---|---|---|---|---|---|
| minix/drivers/vmm_guest/vbox/vmmdev.h:94-115 | `struct VMMDevHGCMParam { u32_t type; union { u32; u64; struct { u32 size; union { u32 phys; void *vir; } } ptr; ... } }` | LAYOUT/HW | Протокол хоста. На i386 это 12 байт (= `HGCMFunctionParameter32`, pack(4)). На x86_64 из-за выравнивания u64 по 8 и `void *` получается 24 байта, а хост ждёт 12 (Call32) или 16 (`HGCMFunctionParameter64`, pack(4)). Параметры сдвигаются, и хост читает мусор. Нужно `__packed`/`aligned(4)` и явные u32/u64 вместо `void *` | драйв. | 64 | S |
| minix/drivers/vmm_guest/vbox/vmmdev.h:12; hgcm.c:629 | `VMMDEV_REQ_HGCMCALL 62` (= `VMMDevReq_HGCMCall32`) | HW | Для 32-битной раскладки параметров тип 62 правильный и с 64-битного гостя, если структуру упаковать до 12 байт. Если перейти на 64-битную раскладку параметров — тип 63 (`HGCMCall64`), 16 байт на параметр | драйв. | 64 | S |
| minix/drivers/vmm_guest/vbox/vbox.c:50; vbox.c:127; hgcm.c:117 | `sys_outl(port, addr)` — физический адрес запроса в 32-битный порт VMMDev; `alloc_contig(VMMDEV_BUF_SIZE, 0, ...)` | HW/PHYS | Порт принимает только 32-битный физический адрес (`(u32_t)` прячет обрезку), а буферы запросов выделяются без ограничения ниже 4 ГБ | А1/драйв. | >4Г | S |
| minix/drivers/vmm_guest/vbox/hgcm.c:503-529 | PageList: `pagelist->addr[] = (u64_t) vp_addr` | HW | 64-битные адреса страниц — ОК | — | — | — |
| minix/drivers/vmm_guest/vbox/vmmdev.h:44,83 | `VMMDevReqHostTime` 32/32, `VMMDevHGCMPageList` 16/16 | LAYOUT | Размер совпадает — ОК (лучше всё же pack(4), как у VirtualBox) | — | — | — |
| minix/include/minix/vboxtype.h:22-34 | `vbox_param_t { type; union { u32; u64; struct { cp_grant_id_t; size_t off; size_t size; unsigned dir; } } }` | LAYOUT | Протокол vboxfs↔vbox по гранту (одна ABI) — ОК; размер зависит от `size_t` | — | — | — |
| minix/drivers/vmm_guest/vbox/hgcm.c:517,588,612-613; vbox.c:45 | `size_t` → u32 | — | Безопасно (размеры < 4 КБ / параметры HGCM) | — | — | — |

Итог: HGCM на x86_64 сломан раскладкой `VMMDevHGCMParam` (24 байта вместо 12/16). Все запросы VMMDev должны
лежать ниже 4 ГБ (32-битный порт).

#### audio (minix/drivers/audio, minix/lib/libaudiodriver)

| Файл:строка | Что | Кат. | Почему проблема | Шаг | Проявл. | Сл. |
|---|---|---|---|---|---|---|
| minix/lib/libaudiodriver/audio_fw.c:755-805 | `init_buffers()` целиком под `#if defined(__i386__)`, иначе `return EIO` | ARCH | На amd64 ни один аудиодрайвер не заработает | amd64 | 64 | S |
| minix/lib/libaudiodriver/audio_fw.c:764,799; drivers/audio/*/…: `drv_set_dma(u32_t dma, ...)` | `alloc_contig(..., AC_ALIGN64K\|AC_LOWER16M, &ph)` → u32 | HW | Корректно (ниже 16 МБ) | — | — | — |
| minix/drivers/audio/cs4281/cs4281.c:329-333; cs4281/io.h:13,26,39,51,63,75 | `dev.base[i] = (u32_t)reg` (MMIO при `DMA_BASE_IOMAP`, cs4281.h:6), затем `*(volatile u8_t *)(port)` | PTR | Указатель на MMIO в u32 | драйв. | 64 | S |
| minix/drivers/audio/{cmi8738,als4000,trident}/io.h (`#ifdef DMA_BASE_IOMAP`) | тот же шаблон, но макрос там не определён | PTR | Сейчас не собирается этим путём; при включении будет та же проблема | — | — | — |

#### usb: usbd (hcd, musb), usb_hub, usb_storage, libusb, libddekit

| Файл:строка | Что | Кат. | Почему проблема | Шаг | Проявл. | Сл. |
|---|---|---|---|---|---|---|
| minix/drivers/usb/usbd/include/usbd/hcd_common.h:30,35,45-48 | `typedef unsigned long hcd_reg4;` `#define _HCD_REG4 volatile hcd_reg4 *`; `HCD_RD4/HCD_WR4` | HW | Чтение и запись «32-битных» регистров MUSB/AM335x (musb_am335x.c:194-195,442,525-530,554-555,686-727 и др., 19 мест) на LP64 станут **64-битными** обращениями к MMIO: захватывается соседний регистр, а на ARM возможен bus fault. Нужно `uint32_t` | aarch64 | 64 | S |
| minix/drivers/usb/usbd/hcd/hcd_common.c:400; hcd_schedule.c:214-215 | `USB_MSG("0x%08X", (int)ptr)` | FMT/PTR | Отладочный вывод указателя через int | драйв. | 64 | S |
| minix/drivers/usb/usbd/hcd/musb/* | MUSB работает через FIFO (PIO), DMA-адресов нет | HW | ОК | — | — | — |
| minix/include/minix/usb.h (struct usb_urb); minix/lib/libusb/usb.c:34,48; minix/lib/libddekit/src/usb_server.c:312-323 | URB копируется между процессами начиная с `&urb->dev_id`, размер `urb_size - sizeof(void*)` | LAYOUT | Предполагается, что перед `dev_id` лежит ровно один указатель `next` без выравнивающего отступа. На LP64 `offsetof(dev_id)` = 8 = `sizeof(void*)` — работает; в URB передаётся и `void *priv` (как непрозрачное эхо). Хрупко: лучше `offsetof` | — | — | S |
| minix/include/minix/com.h:829-840 | `USB_GRANT_ID/USB_URB_ID/USB_RESULT` = `m4_l1/m4_l2` (long) | MSG | Грант и id в `long`-полях (libusb usb.c:66,112,217,220,254 сужает long → int/unsigned) — безопасно, но перевести на фиксированные поля | MSG | — | S |
| minix/drivers/usb/usb_storage/bulk.h:11-44 | CBW/CSW `__packed` | LAYOUT | ОК | — | — | — |
| minix/drivers/usb/usb_storage/usb_storage.c:953-954,1035-1036,915,1111,1462,1509 | `unsigned long` → `unsigned` (LBA, длина в `scsi_transfer`), `iov_addr` → грант | — | LBA 32 бита (READ10) — существующий предел 2 ТБ; остальное безопасно | — | — | — |
| minix/drivers/usb/usb_hub/usb_hub.c:921,935 | long → int (env) | — | Безопасно | — | — | — |
| minix/lib/libddekit/src/mem.c:81-83 | `ddekit_addr_t phys; alloc_contig(size, AC_ALIGN4K, &phys)` | PHYS | `ddekit_addr_t` = `unsigned long` (include/ddekit/types.h:20), а параметр — `phys_bytes *`. На PAE с 64-битным `phys_bytes` запишется 8 байт в 4-байтную переменную на стеке; pgtab (pgtab.c:36-37,185-264) хранит физические адреса в `unsigned long` | А1 | >4Г | S |
| minix/lib/libddekit/src/thread.c:74,87,94,449,478,504,531,542; resource.c:55,80-135 | `(int)th` и `%x` для указателей/адресов в отладочном выводе | FMT | Только отладка | драйв. | 64 | S |
| minix/lib/libddekit/src/resource.c:99,131 | `unsigned long ddekit_inl/outl` | — | Значение 32-битное, ширина `long` лишняя — безопасно | — | — | — |
| minix/drivers/usb/Makefile:3-5 | `usbd` только для earm | ARCH | — | — | — | — |

Итог: главное — `hcd_reg4 = unsigned long`: на aarch64 все 32-битные регистры MUSB читаются и пишутся 64-битными
обращениями.

#### ARCH: сборочные привязки раздела (кратко)

| Файл:строка | Что | Кат. | Почему проблема | Шаг | Проявл. | Сл. |
|---|---|---|---|---|---|---|
| minix/drivers/storage/Makefile; bus/Makefile; power/Makefile:3; audio/Makefile:3; hid/Makefile:3; iommu/Makefile:3; printer/Makefile:3; vmm_guest/Makefile:3; tty/Makefile:5; minix/lib/Makefile:32-43 | `.if ${MACHINE_ARCH} == "i386"` / `"earm"` | ARCH | На x86_64 не собираются at_wini, ahci, floppy, virtio_blk, fbd, filter, pci, ti1225, acpi, audio, pckbd, amddev, printer, vbox, libacpi, libvirtio и др.; на aarch64 — mmc, i2c, fb, gpio, tps, usbd, libgpio, libclkconf, libi2cdriver | amd64 | сборка | S |
| minix/drivers/tty/tty/Makefile:4; clock/readclock/Makefile:5 | `arch/${MACHINE_ARCH}/Makefile.inc` | ARCH/ERR | Нет каталога для новой архитектуры | amd64 | сборка | S |

---

#### Сводка раздела

Безопасные сужения и ложные срабатывания сырья (в таблицы не внесены или внесены одной группой): примерно **150 из 255**
строк сырья m64. Сюда входят:
- `size_t`/`ssize_t`/`long` → `int` в счётчиках и размерах буферов tty/pty/log/random/printer/pci/usb — около 70;
- `iov_addr` (`vir_bytes`) → `cp_grant_id_t` по конвенции «грант в iovec» (fbd, filter, floppy, usb_storage, memory, eeprom) — около 10;
- `u64` timestamp → `clock_t` (tty, pty, floppy, ahci) — 7;
- размеры видеопамяти `phys_bytes` → `int` в console.c — 8 (и 9 на p64);
- проверки выравнивания `(unsigned)ptr & 3` в rijndael — 9;
- `%llx` для `u64_t` (ahci, virtio_blk) — 5;
- 13 мест ACPICA сведены к одной причине (`ACPI_MACHINE_WIDTH 32`);
- LBA/сектор `unsigned long` → `unsigned` (at_wini, usb_storage, filter) — существующий предел 2 ТБ.

Из 187 строк сырья раскладок внешнюю раскладку имеют только `struct prdte`, `VMMDevHGCMParam` и `MD5Context`/суммы filter
(дисковый формат). `vring_desc`, `virtio_blk_*`, `VMMDevReqHostTime`, `VMMDevHGCMPageList` и `ti1225 csr` совпадают по
размеру. Всё остальное — внутренние структуры с указателями.

| Категория | Мест (строк таблиц) |
|---|---|
| HW | 22 |
| PHYS | 20 |
| PTR | 17 |
| LAYOUT | 9 |
| ERR (сборка) | 6 |
| ARCH | 10 |
| FMT | 8 |
| BITS | 4 |
| CONST | 4 |
| MSG | 5 |
| VIR | 4 |
| TIME | 1 |
| ASM | 1 |

(У части строк две категории, поэтому сумма больше числа строк. Всего в таблицах раздела 114 строк.)

##### Топ-10 опасных мест

1. **at_wini.c:97-103** — `struct prdte` с `phys_bytes`: на LP64 и на PAE (64-битный `phys_bytes`) аппаратная PRD-таблица
   растёт с 8 до 16 байт. BM-IDE DMA пишет в случайную память. Плюс нет проверки < 4 ГБ (1360-1397, 1432).
2. **ahci.c:834-838, 887-894, 1930-1934** — AHCI не поддерживает 64-битную адресацию: DBAU/CTBAU/CLBU/FBU = 0, S64A
   не проверяется, `alloc_contig` без ограничения < 4 ГБ. При памяти > 4 ГБ DMA уходит по чужому адресу.
3. **pci.c:79-85, 1064-1092, 2444 + `pci_get_bar(u32_t*)`** — BAR хранятся в 32 битах, 64-битные BAR выше 4 ГБ
   выбрасываются, `memgap_low` усекается (1331). Интерфейс и сообщение 32-битные (L).
4. **usbd/include/usbd/hcd_common.h:30,35** — `hcd_reg4 = unsigned long`: на aarch64 64-битные обращения к 32-битным
   MMIO-регистрам MUSB.
5. **vmmdev.h:94-115** — `VMMDevHGCMParam` на x86_64 занимает 24 байта вместо 12/16: HGCM (vboxfs) на 64-битном
   госте сломан. Плюс 32-битный порт запроса (vbox.c:50).
6. **amddev.c:442-446, 307-308** — запись за границу 128-КБ таблицы DEV для адресов ≥ 4 ГБ; BASE_HI = 0.
7. **minix/include/minix/mmio.h** — `read32/write32(uint32_t address)`: все earm-драйверы (mmc, i2c, gpio,
   clkconf) на aarch64 обращаются к MMIO по обрезанному адресу. Вместе с `(uint32_t) vm_map_phys(...)` в
   gpio_omap.c:437, clkconf.c:66, emmc.c:600, mmchost_mmchs.c:142.
8. **filter/crc.c:66-86 + sum.c:42-80** — CRC на 64-битном `long` выходит за границу `crctab`; 8-байтная запись в
   4-байтный слот портит дисковый формат контрольных сумм.
9. **libvirtio/virtio.c:508** — `vp_addr & ~1UL` на PAE обнуляет старшие 32 бита адреса дескриптора.
10. **acminix.h:136** — `ACPI_MACHINE_WIDTH 32` безусловно (и x86-asm 142-174); acpi.c:17 — права на память до 4 ГБ.

Сюда же — ошибки сборки на LP64: `memory.c:417`, `fbd.c:37`, `vnd.c:42`, `mmcblk.c:47` (`int` вместо `ssize_t` в
`bdr_transfer`); `tty/Makefile:4`, `readclock/Makefile:5` (нет `arch/x86_64`). Сквозная проблема: указатели в
псевдогрантах `cp_grant_id_t` (tty.c:247,259,433; console.c:188; rs232.c:273; pty.c:559; vnd.c:147; floppy.c:620,646).

Отдельная общая потребность для шага А1/А3: флаг `alloc_contig(..., AC_LOWER4G)`. Сейчас его нет, а нужен at_wini,
AHCI без S64A, VMMDev, amddev и OMAP DSS (LPAE).

##### Не уверен

- **filter/md5.h:11** (`uint32` = `unsigned long`): MD5 Колина Пламба заявляет, что работает при «≥32 бит», но в
  `MD5Transform`/`byteReverse` может не быть маскирования. Нужен тест хеша на LP64 или замена на `sys/md5.h`.
- **vbox HGCM на 64-битном госте**: принимает ли хост VirtualBox `VMMDevReq_HGCMCall32` (62) с 12-байтными
  параметрами от 64-битного гостя без ограничений. Насколько известно, принимает (обе версии обрабатываются при
  `VBOX_WITH_64_BITS_GUESTS`), но это стоит проверить на живом хосте до выбора между «упаковать до 12» и «перейти на Call64».
- **libvirtio**: при переходе на modern virtio (1.0, обязателен для virtio-mmio на ARM) адреса очередей пишутся двумя
  32-битными регистрами (lo/hi). Это отдельная задача, а не правка 64-битности.
- **at_wini.c:1380-1381** — проверка пересечения границы 64 КБ `user_phys/0x10000` при 64-битном `phys_bytes` корректна;
  но если буфер > 4 ГБ, после обрезки адреса до u32 (при будущем исправлении `prdte_base`) пересечение надо проверять
  уже по обрезанному значению. Проще отказывать в DMA выше 4 ГБ.
- **usb_urb**: копирование «с `&dev_id`» работает на LP64 случайно (`offsetof` = 8). Если в начало структуры
  добавят поле, всё сломается одинаково на всех ABI — это не 64-битная, а общая хрупкость.

### 11.5 Сетевые драйверы

Общее для всех PCI-драйверов раздела (одна запись здесь, в таблицах ниже не повторяется):

- **Нет флага «ниже 4 ГБ» для DMA-памяти.** `alloc_contig()` (`minix/lib/libsys/alloc_util.c:9`,
  флаги `minix/include/minix/syslib.h:99-102`) умеет только `AC_ALIGN4K/AC_ALIGN64K/AC_LOWER16M/AC_LOWER1M`;
  `AC_LOWER4G`/`MAP_LOWER4G` нет. Все драйверы ниже выделяют кольца и буферы без ограничения
  по адресу. Пока физическая память < 4 ГБ — работает; при памяти > 4 ГБ (amd64, PAE/LPAE)
  32-битные мастера (rtl8139, 3c90x, fxp, dec21140A, vt6105, lan8710a, atl2 при `HI=0`) и
  64-битные, у которых старшая половина не пишется (e1000, rtl8169, ip1000), получат
  обрезанный адрес и будут писать DMA в чужую память. Нужен флаг в libsys + зона в VM
  (шаг А1/А3) и проставление его в драйверах (драйв.).
- **64-битные BAR выше 4 ГБ игнорируются**: `pci_get_bar(..., u32_t *base, ...)`
  (`minix/include/minix/syslib.h:250`), а `minix/drivers/bus/pci/pci.c:1078-1091` пропускает BAR
  с ненулевой старшей половиной. Это шина PCI (§11.4), но затрагивает все MMIO-драйверы
  (3c90x, atl2, e1000, ip1000/vt6105 при `DMA_BASE_IOMAP`). Предупреждения
  `-Wint-to-void-pointer-cast` на `vm_map_phys(SELF, (void *)bar, ...)` (3c90x.c:678,
  atl2.c:509, e1000.c:129,210) — расширение u32→указатель, само по себе безопасно.
- «Контекст» драйвера (указатели) в 32-битных полях аппаратных дескрипторов — **не найден**
  ни в одном драйвере: везде параллельные массивы по индексу, virtio хранит `void *` в libvirtio.

#### 3c90x (`minix/drivers/net/3c90x`)

Дескриптор `xlbc_pd_t` (3c90x.h:171-176) — все поля `uint32_t`, раскладка не меняется.
Безымянная структура 3c90x.c:20 (68→120 байт) — программное состояние драйвера, не аппаратная.
Чип — 32-битный мастер (нет старших половин адреса).

| Файл:строка | Что | Кат. | Почему проблема | Шаг | Проявл. | Сл. |
|---|---|---|---|---|---|---|
| minix/drivers/net/3c90x/3c90x.c:331-350 | `alloc_contig(..., AC_ALIGN4K / 0, &..._phys)` для DPD/UPD/TX/RX буферов | HW | 32-битный мастер, буферы не ограничены < 4 ГБ | А1+драйв. | >4Г | M |
| minix/drivers/net/3c90x/3c90x.c:390,393,901,917 | `upd_base[i].next/addr`, `dpd_base[].addr/next` = `phys_bytes` → `uint32_t` поле дескриптора | HW | молча обрезается старшая часть физ. адреса | драйв. | >4Г | S |
| minix/drivers/net/3c90x/3c90x.c:397,922,926,1027 | `XLBC_WRITE_32(XLBC_UP/DN_LIST_PTR_REG, phys)` | HW | регистр 32 бит, адрес кольца обрезается | драйв. | >4Г | S |

Итог: на 64 битах при памяти < 4 ГБ работает; > 4 ГБ — нужен `AC_LOWER4G` для всех четырёх выделений.

#### atl2 (`minix/drivers/net/atl2`)

Аппаратные структуры `rxd_t` (uint32 + data) и TxS (`uint32_t`) — фиксированной ширины.
Безымянная `state` (84→128) — программная. Чип имеет один общий регистр старших 32 бит
для всех трёх колец.

| Файл:строка | Что | Кат. | Почему проблема | Шаг | Проявл. | Сл. |
|---|---|---|---|---|---|---|
| minix/drivers/net/atl2/atl2.c:45-47 | `vir_bytes txd_phys/txs_phys/rxd_phys` — физ. адреса в `vir_bytes` | PHYS | неверный тип; при 64-битном `phys_bytes` на ILP32 не совпадает | А1 | оба | S |
| minix/drivers/net/atl2/atl2.c:241-252 | `alloc_contig(..., &state.txd_phys)` и т.п.: `vir_bytes *` передаётся как `phys_bytes *` | PHYS | на PAE (`phys_bytes`=8, `vir_bytes`=4) запись 8 байт в 4-байтное поле затирает соседнее (txs_phys/rxd_phys/txd_tail); `-Wincompatible-pointer-types` (ошибка в GCC ≥ 14) | А1 | >4Г/сборка | S |
| minix/drivers/net/atl2/atl2.c:241-252 | все три кольца без ограничения < 4 ГБ | HW | общий `ATL2_DESC_ADDR_HI_REG`: кольца должны лежать в одном 4-ГБ окне, а при `HI=0` — ниже 4 ГБ | А1+драйв. | >4Г | M |
| minix/drivers/net/atl2/atl2.c:389-392 | `DESC_ADDR_HI = 0; *_ADDR_LO = state.*_phys` | HW | старшая половина не пишется, младшая — обрезка | драйв. | >4Г | S |

Итог: сборка под PAE даёт порчу соседних полей (тип `vir_bytes` для физ. адресов);
> 4 ГБ — нужен либо `AC_LOWER4G`, либо запись общего HI и проверка одного окна.

#### dec21140A (`minix/drivers/net/dec21140A`)

Аппаратный дескриптор — `de_descr_t { u32_t des[4]; }` (dec21140A.h:29-31), раскладка не
меняется. `struct de_local_descr` (12→24 байт) — **программная** «тень» драйвера
(указатели на дескриптор и буферы), железо её не читает — не проблема. `struct dpeth`
— программная. Чип (21140A) — 32-битный мастер.

| Файл:строка | Что | Кат. | Почему проблема | Шаг | Проявл. | Сл. |
|---|---|---|---|---|---|---|
| minix/drivers/net/dec21140A/dec21140A.c:312-313 | `sys_umap(..., (phys_bytes *)&descr->des[DES_BUF1])` — явное приведение `u32_t *` → `phys_bytes *` | PTR/HW | при 8-байтном `phys_bytes` (LP64 и PAE) пишет 8 байт: старшая половина уходит в `des[3]` (DES_BUF2), который тут же (стр. 315) зануляется — адрес молча обрезан; на big-endian (mips64 BE) в `des[2]` попадёт старшая половина. clang молчит из-за явного приведения | драйв. | оба | S |
| minix/drivers/net/dec21140A/dec21140A.h:47-50; dec21140A.c:39 | дескрипторы и буферы — массивы внутри `static dpeth_t de_state` (BSS), не `alloc_contig` | HW | нет гарантии физ. непрерывности буфера (1516 байт может пересечь страницу) и расположения < 4 ГБ | драйв. | >4Г | M |
| minix/drivers/net/dec21140A/dec21140A.c:405-410 | `u32_t val = sendrecv_descr_phys_addr[...]; io_outl(CSR3/CSR4, val)` | HW | адрес списка дескрипторов обрезается до 32 бит | драйв. | >4Г | S |
| minix/drivers/net/dec21140A/dec21140A.c:333,350 | `%08X` (DEBUG) и `%lx` для `phys_bytes` | FMT | неверный формат при 64-битном `phys_bytes` на ILP32 | А1 | >4Г | S |

Итог: самая опасная — запись `phys_bytes` через приведённый указатель в 32-битное поле
дескриптора (12/13); плюс DMA-буферы в BSS без контроля адреса.

#### dp8390 (`minix/drivers/net/dp8390`) — ISA/PCI NE2000, WD80x3, 3c503

Нет bus-master DMA: только программный ввод-вывод портов и разделяемая память карты
(`de_linmem`, ограничена `0xFFFFF` в env_parse, dp8390.c:906). `struct dpeth`, `dp_conf` — программные.

| Файл:строка | Что | Кат. | Почему проблема | Шаг | Проявл. | Сл. |
|---|---|---|---|---|---|---|
| minix/drivers/net/dp8390/dp8390.c:925 | `de_locmem = (char *)-dep->de_ramsize` (`unsigned int`) — «ловушка» | PTR | на LP64 отрицание u32 даёт 0x00000000FFFFxxxx, а не адрес у вершины АП — ловушка перестаёт быть «заведомо невалидной» | драйв. | 64 | S |
| minix/drivers/net/dp8390/3c503.c:108,114; wdeth.c:217,230 | `%lX` для `phys_bytes de_linmem` | FMT | при 64-битном `phys_bytes` на ILP32 | А1 | >4Г | S |

Итог: аппаратных проблем нет (ISA, PIO/shared-mem < 1 МБ); x86-only.

#### dpeth (`minix/drivers/net/dpeth`) — ISA 3c501/3c503/3c509/NE/WD

DMA нет (PIO и разделяемая память). `m_hdr_t`, `buff_t` (8→16, 16→24) — программные
заголовки буферов в malloc-памяти, не аппаратные.

| Файл:строка | Что | Кат. | Почему проблема | Шаг | Проявл. | Сл. |
|---|---|---|---|---|---|---|
| minix/drivers/net/dpeth/3c509.c:212 | `while ((padding++ % sizeof(long)) != 0) outb(..., 0)` | HW | 3c509 требует выравнивания пакета в TX FIFO на dword; на LP64 добивает до 8 байт — лишние 4 нулевых байта попадут в FIFO как начало следующей преамбулы | драйв. | 64 | S |
| minix/drivers/net/dpeth/dp.c:87 | `env_parse(..., &val, 0L, LONG_MAX)` для `dpc_mem`; `dp.c:88` `de_linmem = val` | CONST | граница `LONG_MAX` для физ. адреса ISA-памяти (должна быть ≤ 0xFFFFF); на PAE `long`→`phys_bytes` расширение — безопасно | драйв. | — | S |
| minix/drivers/net/dpeth/3c503.c:95; wd.c:201 | `%lX`/`%05lX` для `phys_bytes de_linmem` | FMT | при 64-битном `phys_bytes` на ILP32 | А1 | >4Г | S |

Итог: единственная реальная 64-битная ошибка — `sizeof(long)` как ширина dword FIFO в 3c509.

#### e1000 (`minix/drivers/net/e1000`)

Дескрипторы `e1000_rx_desc_t`/`e1000_tx_desc_t` (e1000_hw.h:31-50): `u32_t buffer; u32_t buffer_h;` —
раскладка фиксирована. Чип поддерживает 64-битную адресацию, драйвер её не использует.
`struct e1000` (64→112) — программная.

| Файл:строка | Что | Кат. | Почему проблема | Шаг | Проявл. | Сл. |
|---|---|---|---|---|---|---|
| minix/drivers/net/e1000/e1000.c:315,333 | `rx/tx_desc[i].buffer = phys` (`phys_bytes`→`u32_t`), `buffer_h` не пишется (остаётся 0 после memset) | HW | старшая половина адреса буфера теряется; исправляется записью `buffer_h = phys >> 32` | драйв. | >4Г | S |
| minix/drivers/net/e1000/e1000.c:336-337,346-347 | `RDBAL = rx_desc_p; RDBAH = 0`, `TDBAL = tx_desc_p; TDBAH = 0` | HW | базы колец — только младшие 32 бита | драйв. | >4Г | S |
| minix/drivers/net/e1000/e1000.c:105-109,129 | `u32_t flash_addr = pci_attr_r32(PCI_BAR_2)` без маски типа и без старшей половины | HW | 64-битный BAR флеша выше 4 ГБ не поддержан, младшие биты типа не сброшены | драйв. | >4Г | S |

Итог: самый простой в доведении драйвер — железо 64-битное, нужно дописать `buffer_h`/`*BAH`.

#### fxp (`minix/drivers/net/fxp`) — Intel 8255x

Все аппаратные структуры (`rfd`, `tx`, `ias`, `cc`, `sc`) — фиксированной ширины (в fxp.h
нет `long`/указателей). `struct fxp` (224→248) — программная (содержит `fxp_stat`, куда пишет
чип, — сам `struct sc` фиксирован). 8255x — 32-битный мастер.

| Файл:строка | Что | Кат. | Почему проблема | Шаг | Проявл. | Сл. |
|---|---|---|---|---|---|---|
| minix/drivers/net/fxp/fxp.c:207,520 | `alloc_contig(sizeof(*fxp_state), 0, NULL)`, `alloc_contig(..., AC_ALIGN4K, &ph)` | HW | 32-битный мастер, без ограничения < 4 ГБ (fxp_stat тоже DMA-цель через sys_umap, стр. 475) | А1+драйв. | >4Г | M |
| minix/drivers/net/fxp/fxp.c:553,557,583,587 | `rfd_linkaddr`/`tx_linkaddr = bus_addr` (`phys_bytes`→`u32`) | HW | обрезка физ. адреса в дескрипторе | драйв. | >4Г | S |
| minix/drivers/net/fxp/fxp.c:950,990 | `fxp_outl(port, SCB_POINTER, bus_addr)` (`do_outl(port_t, u32_t)`) | HW | обрезка адреса CU/RU | драйв. | >4Г | S |
| minix/drivers/net/fxp/fxp.c:525,529 | `buf = (phys_bytes)alloc_buf; tmpbufp = (union tmpbuf *)buf` — виртуальный указатель через `phys_bytes` | PTR | смешение вирт./физ. типов; работает, пока `phys_bytes` ≥ ширины указателя | драйв. | — | S |
| minix/drivers/net/fxp/fxp.c:523 | `panic("... size: %d", alloc_bufsize)` (`size_t`) | FMT | `%d` для `size_t` | драйв. | 64 | S |
| minix/drivers/net/fxp/fxp.c:1767-1768 | `tell_iommu`: `m.m2_l1 = buf (vir_bytes); m.m2_l2 = size` | MSG | вирт. адрес и размер в `long`-полях сообщения m2 (сервис amddev, i386-only, фактически мёртв) | MSG | 64 | S |

Итог: > 4 ГБ — нужен `AC_LOWER4G`; на 64 битах при памяти < 4 ГБ работоспособен.

#### ip1000 (`minix/drivers/net/ip1000`)

`NDR_desc { u64_t next, status, frag_info; }` (ip1000.h:140-144) — 24 байта на обеих архитектурах
(разница только в выравнивании 4→8, аппаратно корректно). `frag_info` — адрес в битах 0..47 и
длина в 48..63: 64-битный адрес поддержан. `NDR_driver` и вложенные rx/tx — программные.

| Файл:строка | Что | Кат. | Почему проблема | Шаг | Проявл. | Сл. |
|---|---|---|---|---|---|---|
| minix/drivers/net/ip1000/ip1000.c:396-399 | `RX/TX_DESC_BASEL = addr; RX/TX_DESC_BASEU = 0` | HW | старшая половина базы колец не пишется | драйв. | >4Г | S |
| minix/drivers/net/ip1000/ip1000.c:839 | `alloc_contig(tot_buf_size, 0, &buf_dma)` | HW | при `BASEU=0` кольца должны быть < 4 ГБ, ограничения нет | А1+драйв. | >4Г | S |
| minix/drivers/net/ip1000/ip1000.c:687-691; io.h:9-80 | под `DMA_BASE_IOMAP`: `pdev->base[i] = (u32_t)reg` (MMIO-указатель в `u32_t base[6]`) и `*(volatile u32_t *)(port)` | PTR | на LP64 обрезка указателя на MMIO. Сейчас `DMA_BASE_IOMAP` нигде не определён — ветка мёртвая, используется `sys_in*/sys_out*` | драйв. | 64 | S |

Итог: железо и дескрипторы 64-битные; нужно писать `BASEU`. MMIO-ветка непереносима, но выключена.

#### lan8710a (`minix/drivers/net/lan8710a`) — earm, TI AM335x CPSW (ручной разбор)

`lan8710a_desc_t` (lan8710a.h:55-61) — 4×`u32_t`, раскладка фиксирована. `lan8710a_t` (88→168)
— программная (`regs_*` в `vir_bytes` — корректно). Дескрипторы лежат в CPPI RAM по
фиксированному физ. адресу (`BEGINNING_RX/TX_DESC_MEM`, стр. 264-272). CPDMA — 32-битный
мастер; Cortex-A8 без LPAE, но CPSW есть и в SoC с LPAE (AM57x, Cortex-A15).

| Файл:строка | Что | Кат. | Почему проблема | Шаг | Проявл. | Сл. |
|---|---|---|---|---|---|---|
| minix/drivers/net/lan8710a/lan8710a.c:422-424,443-445 | `alloc_contig(..., AC_ALIGN4K, &buf_phys_addr)` | HW | 32-битный CPDMA, ограничения < 4 ГБ нет | А1+драйв. | >4Г | M |
| minix/drivers/net/lan8710a/lan8710a.c:432,436-440,451 | `buffer_pointer/next_pointer = (u32_t)(phys...)` | HW | явное приведение глушит clang; на LPAE обрезка | драйв. | >4Г | S |
| minix/drivers/net/lan8710a/lan8710a.c:362-363,403-404 | `lan8710a_reg_write(CPDMA_STRAM_*_HDP, (u32_t)phys)` | HW | то же для регистров HDP | драйв. | >4Г | S |
| minix/drivers/net/lan8710a/lan8710a.c:207-286; lan8710a_reg.h | жёсткие физ. адреса блоков AM335x (`CM_PER_BASE_ADR` и т.д.), `vm_map_phys((void *)CONST, ...)` | ARCH | привязка к одной SoC; при порте — из FDT | В4/драйв. | — | M |
| minix/drivers/net/lan8710a/lan8710a.h:22; lan8710a_reg.h:31,87,133 | `(1 << 31)` | CONST | сдвиг знаковой 1 в бит 31 (UB, как testing.md 4.16); не 64-специфично | — | — | S |

Итог: на 64-битном ARM этот драйвер не нужен как есть (SoC-специфичен); для LPAE-платформ с
CPSW — нужен `AC_LOWER4G`.

#### lance (`minix/drivers/net/lance`) — AMD LANCE/PCnet (16-битный стиль, 24-битные адреса)

`struct lance_init_block` (lance.c:109-116) — **аппаратная**: блок инициализации, который чип
читает по CSR1/CSR2 (16-битный стиль: mode 2 + MAC 6 + LADRF 8 + RDRA 4 + TDRA 4 = 24 байта).
`lance_rx_head`/`lance_tx_head` — аппаратные, фиксированной ширины (`Address`=`uint32_t`).
`struct lance_interface` (48856→48864) — аппаратная целиком (init block + кольца + буферы);
меняется только из-за init block. `lance_chip_type` — программная. Память: `AC_LOWER16M`
(стр. 759) — корректно для 24-битного DMA.

| Файл:строка | Что | Кат. | Почему проблема | Шаг | Проявл. | Сл. |
|---|---|---|---|---|---|---|
| minix/drivers/net/lance/lance.c:113 | `unsigned long filter[2]` в `lance_init_block` | LAYOUT/HW | на LP64 фильтр 16 байт + выравнивание 8 → блок 32 байта вместо 24; `rx_ring`/`tx_ring` смещаются с 16/20 на 24/28 — чип прочитает в RDRA/TDRA половины фильтра (нули) и запустит DMA по адресу 0 | драйв. | 64 | S |
| minix/drivers/net/lance/lance.c:763-764 | `Address l = (vir_bytes)lance_buf; lp = (struct lance_interface *)l;` | PTR | указатель проходит через `uint32_t` — на LP64 обрезается, `lp` невалиден → падение при первой записи | драйв. | 64 | S |
| minix/drivers/net/lance/lance.c:353,477,529,558,603,804 | `u.addr[3]` как старший байт `u.base` | ARCH | предполагает little-endian; на BE (mips64 BE) флаги OWN/статус попадут в младший байт адреса | драйв. | — | S |
| minix/drivers/net/lance/lance.c:761 | `panic("alloc_contig failed: %d", LANCE_BUF_SIZE)` (`sizeof`) | FMT | `%d` для `size_t` | драйв. | 64 | S |

Сужения `phys_bytes`→`Address` (стр. 592, 771, 773, 775, 802, 810, 816) — безопасны: адреса
явно маскируются `& 0xffffff` и память выделена с `AC_LOWER16M`.

Итог: на 64 битах драйвер не работает дважды — обрезка указателя `lp` и неверная раскладка
блока инициализации (`unsigned long` → `uint32_t`).

#### rtl8139 (`minix/drivers/net/rtl8139`)

Дескрипторов в памяти нет (регистры TSAD0-3, RBSTART). `struct re` и вложенная — программные.
Чип — 32-битный мастер.

| Файл:строка | Что | Кат. | Почему проблема | Шаг | Проявл. | Сл. |
|---|---|---|---|---|---|---|
| minix/drivers/net/rtl8139/rtl8139.c:334 | `alloc_contig(BUF_ALIGNMENT + tot_bufsize, 0, &buf)` | HW | 32-битный мастер, без ограничения < 4 ГБ | А1+драйв. | >4Г | M |
| minix/drivers/net/rtl8139/rtl8139.c:469-477; rtl8139.h:433 | `bus_buf = vm_1phys2bus(phys)` (макрос-заглушка `(p)`), `rl_outl(RBSTART/TSAD0+i*4, bus_buf)` | HW | обрезка адреса в 32-битный регистр; трансляция «физ.→шина» отсутствует (на ARM с dma-ranges понадобится) | драйв. | >4Г | S |
| minix/drivers/net/rtl8139/rtl8139.c:1489-1510 | `tell_iommu`: `m2_l1 = vir_bytes buf`, `m2_l2 = size` | MSG | как в fxp: адрес в `long`-поле m2 (amddev, i386-only) | MSG | 64 | S |

Итог: работает при памяти < 4 ГБ; для > 4 ГБ — `AC_LOWER4G`.

#### rtl8169 (`minix/drivers/net/rtl8169`)

`re_desc` (rtl8169.c:29-34): `status, vlan, addr_low, addr_high` — все `u32_t`; `re_dtcc` — фиксированный.
Чип 64-битный, драйвер старшие половины не пишет. `struct re` и вложенные — программные.

| Файл:строка | Что | Кат. | Почему проблема | Шаг | Проявл. | Сл. |
|---|---|---|---|---|---|---|
| minix/drivers/net/rtl8169/rtl8169.c:507,519 | `desc->addr_low = ret_buf` (`phys_bytes`→`u32_t`), `addr_high` = 0 (memset) | HW | старшая половина адреса буфера теряется | драйв. | >4Г | S |
| minix/drivers/net/rtl8169/rtl8169.c:794-795,800-801 | `RDSAR_LO = p_rx_desc; RDSAR_HI = 0`, `TNPDS_LO/HI` аналогично | HW | база колец — только младшие 32 бита | драйв. | >4Г | S |
| minix/drivers/net/rtl8169/rtl8169.c:295-296 | `DTCCR_HI = 0; DTCCR_LO = dtcc_buf \| CMD` | HW | адрес буфера счётчиков — только младшие 32 бита | драйв. | >4Г | S |
| minix/drivers/net/rtl8169/rtl8169.c:474 | `alloc_contig(tot_bufsize, AC_ALIGN64K, &buf)` | HW | пока HI=0 — нужно < 4 ГБ | А1+драйв. | >4Г | S |

Итог: железо 64-битное — достаточно писать `addr_high`/`*_HI`.

#### virtio_net (`minix/drivers/net/virtio_net`)

Адреса передаются в libvirtio через `struct vumap_phys` (`phys_bytes vp_addr`) — без сужения.
`struct packet` (28→56) — программная (`vhdr`/`vdata` указатели, контекст хранится как `void *`
в libvirtio). `virtio_net_hdr` — фиксированной ширины (u8/u16). Флаг записи в бит 0
`vp_addr |= 1` — не зависит от ширины.

Замечаний в самом драйвере нет. Ограничения по адресу (legacy virtio-pci: `QUEUE_PFN` 32 бита
→ 44-битный физ. адрес колец) — раздел libvirtio, не D.

Итог: переносим без правок.

#### vt6105 (`minix/drivers/net/vt6105`) — VIA Rhine

`NDR_desc { u32_t status, length, addr, next; }` (vt6105.h:87-92) — фиксированная. `NDR_driver` и вложенные
— программные. Чип — 32-битный мастер (одного регистра базы на кольцо). Вопреки ip1000,
64-битных адресов нет.

| Файл:строка | Что | Кат. | Почему проблема | Шаг | Проявл. | Сл. |
|---|---|---|---|---|---|---|
| minix/drivers/net/vt6105/vt6105.c:647 | `alloc_contig(tot_buf_size, 0, &buf_dma)` | HW | 32-битный мастер, ограничения < 4 ГБ нет | А1+драйв. | >4Г | M |
| minix/drivers/net/vt6105/vt6105.c:185,188,190,197,200,202 | `desc->addr = buf_dma; desc->next = desc_dma_start + ...` (`phys_bytes`→`u32_t`) | HW | обрезка адресов в дескрипторах | драйв. | >4Г | S |
| minix/drivers/net/vt6105/vt6105.c:209-210 | `ndr_out32(REG_RX/TX_DESC_BASE, phys)` | HW | обрезка базы колец | драйв. | >4Г | S |
| minix/drivers/net/vt6105/vt6105.c:495-499; io.h:9-80 | под `DMA_BASE_IOMAP`: `pdev->base[i] = (u32_t)reg`, `*(volatile u32_t *)(port)` | PTR | как в ip1000: обрезка MMIO-указателя; ветка выключена | драйв. | 64 | S |

Итог: < 4 ГБ работает; > 4 ГБ — только `AC_LOWER4G`.

#### Сводка раздела

Безопасные сужения из сырья (в таблицы не внесены): **около 35 мест** —
длины пакетов/буферов `size_t`→`u32_t`/`int`/`unsigned` в дескрипторах и счётчиках (3c90x:902,
atl2:757,772, dec21140A:243,317,483, dp8390:245,728, dpeth 3c501:146,151,167 и 8390:131,142,160,228,230,
fxp:510,514,864, ip1000:582, lan8710a:732, rtl8139:671,711, rtl8169:961,1008,1011, vt6105:186,198,244,390),
`long`→`int` для IRQ/порта из env_parse (dp8390:902,910, dpeth dp.c:86,88; dp8390.c:904 на PAE),
счётчики статистики u64→u32 (rtl8169:305,309), `phys_bytes`→`vir_bytes` для длины в sys_umap
(dec21140A:312, fxp:569 на PAE), маскируемые 24-битные адреса lance (7 мест),
расширение BAR u32→указатель (4 места).

| Категория | Мест |
|---|---|
| HW | 28 |
| PTR (вкл. 1 PTR/HW) | 6 |
| PHYS | 2 |
| LAYOUT/HW | 1 |
| FMT | 5 |
| MSG | 2 |
| CONST | 2 |
| ARCH | 2 |
| **Всего строк в таблицах** | **48** |

##### Самые опасные места (топ-10)

1. **lance.c:763-764** — указатель на DMA-область проходит через `uint32_t Address` → на LP64 `lp` обрезан, драйвер падает сразу (64).
2. **lance.c:113** — `unsigned long filter[2]` в аппаратном init block: 24→32 байта, чип читает неверные адреса колец (64).
3. **Нет `AC_LOWER4G` в libsys/VM** — все 32-битные мастера (rtl8139, 3c90x, fxp, dec21140A, vt6105, atl2, lan8710a) при памяти > 4 ГБ пишут DMA по обрезанному адресу (>4Г).
4. **dec21140A.c:312-313** — `sys_umap` пишет 8-байтный `phys_bytes` через приведённый указатель в `u32_t des[2]` дескриптора; старшая половина затирает `des[3]`; на BE — неверный адрес (оба).
5. **atl2.c:45-47, 241-252** — физ. адреса в `vir_bytes`, `alloc_contig` пишет `phys_bytes` 8 байт в 4-байтные поля на PAE (порча соседних полей; ошибка GCC 14) (>4Г/сборка).
6. **dpeth/3c509.c:212** — выравнивание TX FIFO по `sizeof(long)`: на LP64 лишние 4 байта в FIFO (64).
7. **e1000.c:315,333,336-347** — `buffer_h` и `RDBAH/TDBAH` = 0 при 64-битном железе (>4Г).
8. **rtl8169.c:295-296,507,519,794-801** — `addr_high` и `*_HI` = 0 при 64-битном железе (>4Г).
9. **ip1000.c:396-399** — `BASEU` = 0 (>4Г); **dec21140A** — DMA-буферы в BSS без контроля непрерывности/адреса.
10. **ip1000/vt6105 io.h + init (DMA_BASE_IOMAP)** — MMIO-указатель в `u32_t base[]` (сейчас мёртвая ветка, но при включении — обрезка на 64).

##### Не уверен

- dec21140A.c:343 — `sys_umap(SELF, VM_D, (vir_bytes)&(loc_descr->descr), ...)` берёт физ. адрес
  **поля-указателя** в программной структуре, а не самого дескриптора; проверка выравнивания
  бессмысленна. Не 64-битная ошибка, но стоит исправить при пересмотре.
- dec21140A: буферы 1516 байт в BSS — непрерывны ли физически? `sys_umap` с длиной, вероятно,
  вернёт ошибку при разрыве (panic при инициализации), а не тихую порчу — проверить по ядру.
- Поддерживает ли VM на amd64 `MAP_LOWER16M` (нужно lance) — раздел VM; здесь принято, что да.
- Трансляция «физический адрес → адрес шины» (`vm_1phys2bus` в rtl8139 — заглушка) нигде не
  делается; на ARM/RISC-V с `dma-ranges` в DTS понадобится общий механизм (В4/драйв.).
- atl2: допустимо ли чипу (L2) получать ненулевой `DESC_ADDR_HI` — Linux atl2 пишет HI;
  тогда достаточно выделять все три кольца в одном 4-ГБ окне.

## Приложение А. Типы сообщений, не помещающиеся в 56 байт на LP64

Из `minix/include/minix/ipc.h`; размер на i386 у всех — 56 байт. Получено тестом abi64 (`messages_56_bytes`) и разбором объявлений. `ptr` — указатель, `long` — поля `m*_l*` и `long`, остальное — по имени типа.

| Тип | `ipc.h`, строка | Размер на LP64 | Причина: тип — поля (пример) |
|---|---|---|---|
| `mess_1` | 37 | 80 | ptr: m1p4 |
| `mess_2` | 45 | 72 | long: m2l2; ptr: m2p1 |
| `mess_3` | 56 | 64 | ptr: m3p1 |
| `mess_4` | 63 | 80 | long: m4l5 |
| `mess_7` | 70 | 72 | ptr: m7p2 |
| `mess_9` | 77 | 80 | long: m9l5 |
| `mess_10` | 85 | 72 | long: m10l3 |
| `mess_fs_vfs_breadwrite` | 118 | 64 | size_t: nbytes |
| `mess_fs_vfs_getdents` | 153 | 64 | size_t: nbytes |
| `mess_fs_vfs_rdlink` | 191 | 64 | size_t: nbytes |
| `mess_fs_vfs_readwrite` | 214 | 64 | size_t: nbytes |
| `mess_krn_lsys_schedule` | 261 | 72 | long: acnt_deqs, acnt_ipc_sync, acnt_ipc_async, acnt_preempt |
| `mess_krn_lsys_sys_fork` | 282 | 64 | vir_bytes: msgaddr |
| `mess_krn_lsys_sys_trace` | 317 | 64 | long: data |
| `mess_krn_lsys_sys_umap` | 324 | 64 | phys_bytes: dst_addr |
| `mess_lbdev_lblockdriver_msg` | 338 | 64 | long: request |
| `mess_lc_ipc_semctl` | 364 | 64 | vir_bytes: opt |
| `mess_lc_ipc_semget` | 374 | 64 | key_t(long): key |
| `mess_lc_ipc_semop` | 383 | 64 | ptr: ops |
| `mess_lc_ipc_shmat` | 391 | 72 | ptr: addr, retaddr |
| `mess_lc_ipc_shmctl` | 400 | 64 | ptr: buf |
| `mess_lc_ipc_shmdt` | 409 | 64 | ptr: addr |
| `mess_lc_ipc_shmget` | 415 | 64 | key_t(long): key; size_t: size |
| `mess_lc_mib_sysctl` | 424 | 80 | vir_bytes: oldp, newp, namep; size_t: oldlen, newlen |
| `mess_lc_pm_exec` | 435 | 80 | vir_bytes: name, frame, ps_str; size_t: namelen, framelen |
| `mess_lc_pm_groups` | 460 | 64 | vir_bytes: ptr |
| `mess_lc_pm_itimer` | 468 | 72 | vir_bytes: value, ovalue |
| `mess_lc_pm_mcontext` | 477 | 64 | vir_bytes: ctx |
| `mess_lc_pm_ptrace` | 493 | 64 | vir_bytes: addr; long: data |
| `mess_lc_pm_rusage` | 510 | 64 | vir_bytes: addr |
| `mess_lc_pm_sig` | 532 | 72 | vir_bytes: act, oact, ret |
| `mess_lc_pm_sigset` | 543 | 64 | vir_bytes: ctx |
| `mess_lc_pm_sprof` | 552 | 72 | vir_bytes: ctl_ptr, mem_ptr; size_t: mem_size |
| `mess_lc_pm_sysuname` | 564 | 64 | size_t: len; vir_bytes: value |
| `mess_lc_pm_time` | 574 | 64 | long: nsec |
| `mess_lc_pm_wait4` | 585 | 64 | vir_bytes: addr |
| `mess_lc_readclock_rtcdev` | 594 | 64 | vir_bytes: tm |
| `mess_lc_svrctl` | 603 | 64 | long: request; vir_bytes: arg |
| `mess_lc_vfs_chown` | 611 | 64 | vir_bytes: name; size_t: len |
| `mess_lc_vfs_creat` | 630 | 64 | vir_bytes: name; size_t: len |
| `mess_lc_vfs_fcntl` | 655 | 64 | vir_bytes: arg_ptr |
| `mess_lc_vfs_fstat` | 665 | 64 | vir_bytes: buf |
| `mess_lc_vfs_gcov` | 680 | 72 | size_t: labellen, buflen; vir_bytes: label, buf |
| `mess_lc_vfs_getvfsstat` | 690 | 72 | size_t: len; vir_bytes: buf |
| `mess_lc_vfs_ioctl` | 699 | 72 | long: req; vir_bytes: arg |
| `mess_lc_vfs_link` | 708 | 72 | vir_bytes: name1, name2; size_t: len1, len2 |
| `mess_lc_vfs_mknod` | 736 | 64 | vir_bytes: name; size_t: len |
| `mess_lc_vfs_mount` | 747 | 96 | size_t: devlen, pathlen, typelen, labellen; vir_bytes: dev, path, type, label |
| `mess_lc_vfs_path` | 762 | 64 | vir_bytes: name; size_t: len |
| `mess_lc_vfs_readlink` | 785 | 72 | vir_bytes: name, buf; size_t: namelen, bufsize |
| `mess_lc_vfs_readwrite` | 795 | 72 | vir_bytes: buf; size_t: len, cum_io |
| `mess_lc_vfs_select` | 805 | 80 | ptr: readfds, writefds, errorfds; vir_bytes: timeout |
| `mess_lc_vfs_sendrecv` | 816 | 80 | vir_bytes: buf, addr; size_t: len |
| `mess_lc_vfs_sockaddr` | 836 | 64 | vir_bytes: addr |
| `mess_lc_vfs_sockmsg` | 854 | 64 | vir_bytes: msgbuf |
| `mess_lc_vfs_sockopt` | 863 | 64 | vir_bytes: buf |
| `mess_lc_vfs_stat` | 874 | 72 | size_t: len; vir_bytes: name, buf |
| `mess_lc_vfs_statvfs1` | 883 | 72 | size_t: len; vir_bytes: name, buf |
| `mess_lc_vfs_truncate` | 894 | 72 | vir_bytes: name; size_t: len |
| `mess_lc_vfs_umount` | 912 | 72 | vir_bytes: name, label; size_t: namelen, labellen |
| `mess_lc_vm_brk` | 922 | 64 | ptr: addr |
| `mess_lc_vm_getphys` | 928 | 72 | ptr: addr, ret_addr |
| `mess_lc_vm_shm_unmap` | 936 | 64 | ptr: addr |
| `mess_lexec_pm_exec_new` | 967 | 64 | vir_bytes: ptr |
| `mess_lsys_fi_ctl` | 1049 | 64 | size_t: size |
| `mess_lsys_getsysinfo` | 1065 | 72 | vir_bytes: where; size_t: size |
| `mess_lsys_krn_readbios` | 1074 | 72 | size_t: size; phys_bytes: addr; vir_bytes: buf |
| `mess_lsys_kern_safecopy` | 1083 | 72 | size_t: offset, bytes; ptr: address |
| `mess_lsys_krn_sys_copy` | 1129 | 80 | vir_bytes: src_addr, dst_addr; phys_bytes: nr_bytes |
| `mess_lsys_krn_sys_diagctl` | 1150 | 64 | vir_bytes: buf |
| `mess_lsys_krn_sys_exec` | 1160 | 80 | vir_bytes: ip, stack, name, ps_str |
| `mess_lsys_krn_sys_getinfo` | 1180 | 72 | vir_bytes: val_ptr, val_ptr2 |
| `mess_lsys_krn_sys_getmcontext` | 1192 | 64 | vir_bytes: ctx_ptr |
| `mess_lsys_krn_sys_memset` | 1217 | 72 | phys_bytes: base, count; long: pattern |
| `mess_lsys_krn_sys_privctl` | 1227 | 72 | vir_bytes: arg_ptr; phys_bytes: phys_start, phys_len |
| `mess_lsys_krn_sys_sdevio` | 1238 | 80 | long: port; phys_bytes: vec_addr; vir_bytes: vec_size, offset |
| `mess_lsys_krn_sys_setgrant` | 1260 | 64 | vir_bytes: addr |
| `mess_lsys_krn_sys_setmcontext` | 1268 | 64 | vir_bytes: ctx_ptr |
| `mess_lsys_krn_sys_settime` | 1276 | 64 | long: nsec |
| `mess_lsys_krn_sys_sprof` | 1286 | 72 | vir_bytes: ctl_ptr, mem_ptr; size_t: mem_size |
| `mess_lsys_krn_sys_statectl` | 1299 | 64 | ptr: address |
| `mess_lsys_krn_sys_trace` | 1322 | 64 | vir_bytes: address; long: data |
| `mess_lsys_krn_sys_umap` | 1332 | 64 | vir_bytes: src_addr |
| `mess_lsys_krn_sys_vdevio` | 1344 | 64 | vir_bytes: vec_addr |
| `mess_lsys_krn_sys_vumap` | 1353 | 80 | vir_bytes: vaddr, paddr; size_t: offset |
| `mess_lsys_kern_vsafecopy` | 1366 | 64 | ptr: vec_addr |
| `mess_lsys_mib_reply` | 1384 | 64 | ssize_t: status |
| `mess_lsys_pm_getepinfo` | 1399 | 64 | vir_bytes: groups |
| `mess_lsys_vfs_mapdriver` | 1465 | 72 | size_t: labellen; vir_bytes: label |
| `mess_lsys_vfs_socketpath` | 1476 | 64 | size_t: count |
| `mess_lsys_vm_getref` | 1486 | 64 | ptr: addr |
| `mess_lsys_vm_info` | 1494 | 72 | ptr: ptr; vir_bytes: next |
| `mess_lsys_vm_map_phys` | 1504 | 72 | phys_bytes: phaddr; size_t: len; ptr: reply |
| `mess_lsys_vm_rusage` | 1513 | 64 | vir_bytes: addr |
| `mess_lsys_vm_unmap_phys` | 1522 | 64 | ptr: vaddr |
| `mess_lsys_vm_vmremap` | 1537 | 72 | ptr: dest_addr, src_addr, ret_addr; size_t: size |
| `mess_mib_lc_sysctl` | 1548 | 64 | size_t: oldlen |
| `mess_mib_lsys_call` | 1554 | 72 | size_t: oldp_len, newp_len |
| `mess_mib_lsys_info` | 1571 | 72 | size_t: name_size, desc_size |
| `mess_mmap` | 1582 | 72 | ptr: addr, retaddr; size_t: len |
| `mess_pci_lsys_busc_get_bar` | 1722 | 64 | size_t: size |
| `mess_pm_lc_ptrace` | 1752 | 64 | long: data |
| `mess_pm_lc_time` | 1766 | 64 | long: nsec |
| `mess_rs_init` | 1855 | 64 | vir_bytes: buff_addr; size_t: buff_len |
| `mess_rs_pm_exec_restart` | 1869 | 64 | vir_bytes: pc, ps_str |
| `mess_rs_req` | 1887 | 72 | ptr: addr, name |
| `mess_sigcalls` | 1916 | 64 | ptr: sigctx |
| `mess_vfs_fs_breadwrite` | 1933 | 64 | size_t: nbytes |
| `mess_vfs_fs_create` | 1963 | 64 | size_t: path_len |
| `mess_vfs_fs_getdents` | 1992 | 64 | size_t: mem_size |
| `mess_vfs_fs_link` | 2010 | 64 | size_t: path_len |
| `mess_vfs_fs_lookup` | 2021 | 72 | size_t: path_len, path_size, ucred_size |
| `mess_vfs_fs_mkdir` | 2038 | 64 | size_t: path_len |
| `mess_vfs_fs_mknod` | 2051 | 64 | size_t: path_len |
| `mess_vfs_fs_new_driver` | 2072 | 64 | size_t: path_len |
| `mess_vfs_fs_rdlink` | 2101 | 64 | size_t: mem_size |
| `mess_vfs_fs_readsuper` | 2111 | 64 | size_t: path_len |
| `mess_vfs_fs_readwrite` | 2122 | 64 | size_t: nbytes |
| `mess_vfs_fs_rename` | 2133 | 64 | size_t: len_old, len_new |
| `mess_vfs_fs_slink` | 2146 | 64 | size_t: path_len, mem_size |
| `mess_vfs_fs_unlink` | 2176 | 64 | size_t: path_len |
| `mess_vfs_lchardriver_readwrite` | 2238 | 72 | size_t: count; long: request |
| `mess_vfs_lsockdriver_ioctl` | 2284 | 64 | long: request |
| `mess_vfs_lsockdriver_sendrecv` | 2304 | 64 | size_t: data_len |
| `mess_vfs_lsys_gcov` | 2340 | 64 | size_t: size |
| `mess_vfs_utimens` | 2356 | 72 | long: ansec, mnsec; size_t: len; ptr: name |
| `mess_vmmcp` | 2383 | 64 | ptr: block, flags_ptr |
| `mess_vmmcp_reply` | 2396 | 64 | ptr: addr |

## Связанные документы

- `docs/modernization.md` — п. 7, план выполнения (шаги А1–А7, Б1–Б3, В1–В4).
- `docs/memory.md` — принцип 3, М1, §10 (вопрос 7: тип физического адреса).
- `docs/porting.md` — порядок портирования; `docs/arch-i386.md`, `docs/arch-earm.md`.
- `docs/testing.md` §1д — тесты на хосте (модуль `abi64`).
