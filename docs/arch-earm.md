# Порт earm (ARMv7-A, TI OMAP) — реализация архитектурно-зависимой части

> Долгоживущий справочник по текущему (унаследованному от MINIX 3, 2018) порту
> **earm**: структура каталогов, файлы, конкретные функции ядра и VM-сервера,
> BSP, сводная таблица возможностей. Составлен по исходникам в сентябре 2026.
> Обновляйте при изменении самого порта. Парный документ — `docs/arch-i386.md`;
> процесс сборки SD-образа — `docs/build-arm32.md`; общая картина —
> `docs/architecture.md` §7.

> **Эталонная ARM-плата проекта (решение 2026-09-25) — Orange Pi PC Plus
> (Allwinner H3)**, QEMU `-M orangepi-pc` для отладки. Этот справочник описывает
> текущий порт под TI. Переход на H3 — направление Б плана модернизации
> (`docs/modernization.md`); PSCI и запуск вторичных CPU — §7.

## 1. Общая характеристика

- `MACHINE=evbarm`, `MACHINE_ARCH=earm` (алиас сборки `evbearm-el`), тулчейн
  `arm-elf32-minix-`, `GCC_CONFIG_ARCH.earm=armv7-a`; по умолчанию сборка GCC
  (`MKGCCCMDS=yes MKLLVM=no` в `arm_sdimage.sh`). EABI, little-endian,
  soft-float ABI.
- ARMv7-A (Cortex-A8), MMU в формате **short-descriptor**: L1 — 4096 записей по
  1 МБ (секции или ссылки на L2), L2 — 256 записей по 4 КБ.
- Ядро: физически `0x80200000`, виртуально `0xF0400000` (`kernel.lds`).
- **Один вендорский BSP — TI** (`bsp/ti`), две платы:
  BeagleBoard-xM (DM37xx/OMAP3) и BeagleBone/BeagleBone Black (AM335x). Плата
  выбирается во время выполнения по параметру `board_name=` из командной строки
  U-Boot (`set_machine_id`), дальше код ветвится по `BOARD_IS_BBXM`/`BOARD_IS_BB`.
- Загрузка — U-Boot (`go 0x80200000`), модули по фиксированным адресам; ядро
  **эмулирует multiboot** внутри себя (`setup_mbi`).
- Для `earm` в `share/mk/bsd.own.mk` принудительно
  `MKWATCHDOG=MKPAE=MKACPI=MKAPIC=MKDEBUGREG=MKINSTALLBOOT=MKPCI=no`.
- **SMP явно запрещён**: `arch_clock.c` содержит
  `#error CONFIG_SMP is unsupported on ARM`.

## 2. Структура каталогов порта

```
minix/kernel/arch/earm/            — машинно-зависимая часть ядра
├── Makefile.inc                   — состав, unpaged-объекты, подключение bsp/ti
├── kernel.lds                     — linker script (phys 0x80200000 / vir 0xF0400000)
├── procoffsets.cf, sconst.h       — смещения полей struct proc (r0–r12, sp, lr, pc, psr, TTBR)
├── head.S                         — точка входа (секция .startup), стек, pre_init, kmain
├── pre_init.c                     — разбор bootargs, эмуляция multiboot, ранние таблицы, MMU
├── pg_utils.c                     — ранние таблицы страниц, карта памяти, включение MMU
├── exc.S                          — таблица векторов исключений (VBAR)
├── mpx.S                          — входы: IRQ, SVC (IPC / kernel call), выход в user
├── klib.S                         — copy_msg_*, разрешение/запрет прерываний
├── phys_copy.S, phys_memset.S     — копирование/заполнение с обработкой сбоев (+ memcpy/memset)
├── protect.c                      — VBAR, TSS-аналог (стеки), загрузка boot-процессов
├── exception.c                    — исключения, data/prefetch abort → page fault
├── memory.c                       — копирование между АП, vm_lookup, отображения для VM и BSP
├── arch_system.c                  — init, CCNT, контекст процесса, выход в user, смена TTBR
├── arch_clock.c                   — системный тик и учёт циклов через BSP-таймер
├── arch_do_vmctl.c                — VMCTL: смена TTBR0, TLB
├── arch_reset.c                   — reset/halt/poweroff через BSP, ser_putc
├── hw_intr.c                      — hw_intr_mask/unmask → BSP
├── do_padconf.c                   — kernel call SYS_PADCONF (мультиплексирование выводов)
├── direct_tty_utils.c             — «прямой» вывод = последовательный порт
├── glo.h, timer.h
├── include/
│   ├── arch_proto.h, archconst.h, arch_clock.h, arch_watchdog.h (пустой)
│   ├── cpufunc.h                  — CP15: барьеры, кеши, TLB, SCTLR, TTBR0/1, TTBCR, DACR, DFSR/IFSR, DFAR/IFAR, VBAR, MIDR, CPSR
│   ├── ccnt.h                     — PMU cycle counter
│   ├── hw_intr.h, io.h, direct_utils.h
└── bsp/
    ├── include/                   — интерфейс BSP (контракт между ядром и платой)
    │   └── bsp_init.h, bsp_intr.h, bsp_padconf.h, bsp_reset.h, bsp_serial.h, bsp_timer.h
    └── ti/                        — реализация для TI OMAP3 / AM335x
        ├── Makefile.inc
        ├── omap_init.c            — bsp_init
        ├── omap_intr.c, omap_intr_registers.h   — INTC
        ├── omap_timer.c, omap_timer_registers.h — GPTIMER/DMTIMER + free-running counter
        ├── omap_serial.c/.h       — отладочный UART
        ├── omap_padconf.c         — pin mux
        ├── omap_reset.c           — reset/poweroff/watchdog
        └── omap_rtc.c/.h          — RTC

minix/servers/vm/arch/earm/        — машинно-зависимая часть VM-сервера
├── Makefile.inc                   — только .PATH
├── pagetable.h                    — ARCH_VM_* / PTF_* на биты ARM_VM_*; PFERR_* по DFSR
└── vm.lds                         — собственный linker script VM (text с 0x8000), подключается в vm/Makefile

minix/include/arch/earm/include/   — устанавливается как <machine/...>
├── vm.h         — биты L1/L2 (секции, PTE, TEX/C/B, AP, S, nG, domain), коды abort (DFSR)
├── stackframe.h — r0–r12, sp, lr, pc, psr
├── multiboot.h  — структуры multiboot (для эмуляции в pre_init)
└── archtypes.h, archconst.h, interrupt.h, ipcconst.h, memory.h, elf.h, diskparm.h, partition.h
```

## 3. Файлы и функции ядра

### 3.1 Загрузка и ранняя инициализация

| Файл | Функции | Назначение |
|---|---|---|
| `head.S` | `MINIX`, `multiboot_init` | вход из U-Boot (`argc/argv`), стек, `pre_init()`, `kmain()` |
| `pre_init.c` | `pre_init(argc, argv)`, `set_machine_id`, `find_value`, `setup_mbi`, `get_parameters`, `mb_set_param`, `overlaps` | очистка BSS; плата по `board_name=`; `bsp_ser_init`; **эмуляция multiboot**: модули по адресам `0x82000000 + i·8 МБ`, карта памяти — **жёстко 256 МБ с `0x80000000`**; `dcache_clean`, ранние таблицы, включение MMU |
| `pg_utils.c` | `print_memmap`, `cut_memmap`, `add_memmap`, `alloc_pagetable`, `pg_alloc_page`, `pg_identity`, `pg_mapkernel`, `vm_enable_paging`, `pg_load`, `pg_clear`, `pg_map`, `pg_rounddown`, `pg_info` | те же роли, что на i386, но для L1/L2 ARM |
| `protect.c` | `prot_init` (`write_vbar(exc_vector_table)`), `tss_init`, `vir2phys`, `bootmod`, `arch_post_init`, `arch_boot_proc`, `libexec_pg_alloc` | векторы исключений, стеки ядра, загрузка boot-образов процессов (ELF) |
| `arch_system.c` | `arch_init` (включение PMU CCNT, доступ из user), `cpu_identify` (MIDR; **частота жёстко 660 МГц**), `arch_ser_init` | инициализация порта, затем `bsp_init()` |

### 3.2 Входы в ядро, исключения, выход в user

| Файл | Функции / метки | Назначение |
|---|---|---|
| `exc.S` | `exc_vector_table`, `invalid_exc` | таблица векторов: undefined, SVC, prefetch abort, data abort, IRQ |
| `mpx.S` | `svc_entry` → по `r3`: `KERVEC_INTR` → `kernel_call_entry`, `IPCVEC_INTR` → `ipc_entry`; иначе `invalid_svc` | **единственный способ входа в ядро — `svc`** |
| | `irq_entry` (`_from_user`/`_from_kernel`) | аппаратные прерывания → `bsp_irq_handle` |
| | `restore_user_context`, стеки `k_stacks_*` | выход в пользовательский режим |
| `exception.c` | `exception_handler(is_nested, saved_lr, vector)`, `data_abort`, `pagefault`, `inkernel_disaster`, `proc_stacktrace(_execute)`, `enable/disable_fpu_exception` | data/prefetch abort → page fault для VM (адрес из DFAR/IFAR, тип по DFSR/IFSR); сбои копирования в ядре |
| `arch_system.c` | `arch_do_syscall`, `arch_finish_switch_to_user`, `arch_proc_setcontext`, `arch_proc_reset`, `arch_set_secondary_ipc_return` (`r1`) | диспетчеризация и контекст |
| `klib.S` | `copy_msg_from_user`, `copy_msg_to_user`, `__user_copy_msg_pointer_failure`, `intr_enable/disable`, `interrupts_enable/disable` | примитивы |
| `phys_copy.S`, `phys_memset.S` | `phys_copy`, `memcpy`, `phys_memset`, `memset`, `bzero`, `__aeabi_memset/memclr`, метки `*_fault` | копирование с перехватом сбоев |

Usermapped-страница есть (секции `.usermapped*`), но таблиц `minix_ipcvecs` с
альтернативными способами входа нет — только `svc`.

### 3.3 Память и адресные пространства

| Файл | Функции | Назначение |
|---|---|---|
| `memory.c` | `memory_init`, `createpde`, `mem_clear_mapcache`, `lin_lin_copy` | копирование между АП через **окна по 1 МБ** (секции L1 в свободных записях `freepdes[]`, `MAXFREEPDES=2`) |
| | `virtual_copy_f`, `data_copy`, `data_copy_vmcheck`, `vm_memset`, `umap_virtual`, `vm_lookup`, `vm_lookup_range`, `vm_check_range`, `check_resumed_caller` | как на i386: ядро само обходит таблицы L1/L2 |
| | `arch_phys_map`, `arch_phys_map_reply`, `arch_enable_paging`, `arch_proc_init`, `release_address_space` | области для отображения VM-ом в АП ядра |
| | `kern_req_phys_map`, `kern_phys_map_ptr`, `kern_phys_map_mapped_ptr` | **механизм BSP**: драйвер ядра (INTC, таймер, UART, RTC) регистрирует MMIO-область; до включения VM используется identity, после — VM отображает, адрес обновляется через callback |
| `arch_do_vmctl.c` | `arch_do_vmctl`, `set_ttbr` | `VMCTL_GET_PDBR`, `VMCTL_SETADDRSPACE` (TTBR0), `VMCTL_FLUSHTLB` (`reload_ttbr0`); **выборочной инвалидации нет** |
| `arch_system.c` | `__switch_address_space` | смена TTBR0 при переключении процесса |
| `include/cpufunc.h` | `write_ttbr0` (всегда вызывает `refresh_tlb`: TLBIALL + инвалидация I-cache и BTB), `dcache_maint/clean/invalidate`, `dsb/dmb/isb` | **каждая смена адресного пространства — полный сброс TLB** |

### 3.4 BSP-интерфейс (`bsp/include`) и реализация TI (`bsp/ti`)

| Интерфейс | Функции TI | Детали |
|---|---|---|
| `bsp_init.h` | `bsp_init` (`omap_init.c`) | padconf, reset, RTC, отключение сторожевого таймера |
| `bsp_intr.h` | `intr_init`, `bsp_irq_handle`, `bsp_irq_mask`, `bsp_irq_unmask` (`omap_intr.c`) | контроллер **INTC** OMAP; база по плате (`OMAP3_DM37XX_INTR_BASE` / `OMAP3_AM335X_INTR_BASE`) |
| `bsp_timer.h` | `bsp_timer_init`, `bsp_timer_stop`, `bsp_register_timer_handler`, `bsp_timer_int_handler`, `omap3_frclock_init/stop`, `read_frc`, `frc_overflow_check`, `read_tsc_64`, `kern_phys_fr_user_mapped` (`omap_timer.c`) | тик: GPTIMER1 (DM37xx, 32 кГц) / DMTIMER1_1MS (AM335x); свободный счётчик: GPTIMER10 / DMTIMER7; free-running counter отображается в user (usermapped) |
| `bsp_serial.h` | `bsp_ser_init`, `bsp_ser_putc` (`omap_serial.c`) | отладочный UART (база по плате) |
| `bsp_padconf.h` | `bsp_padconf_init`, `bsp_padconf_set` (`omap_padconf.c`) | pin mux с фильтром по плате; используется kernel call'ом `SYS_PADCONF` (`do_padconf.c`) для драйверов (GPIO, I2C, ...) |
| `bsp_reset.h` | `bsp_reset_init`, `bsp_reset`, `bsp_poweroff`, `bsp_disable_watchdog` (`omap_reset.c`) | reset/poweroff через PRM, отключение аппаратного WDT |
| — | `omap3_rtc_init`, `omap3_rtc_run` (`omap_rtc.c`) | RTC |

### 3.5 Время, FPU, прочее

| Файл | Функции | Назначение / статус |
|---|---|---|
| `arch_clock.c` | `init_local_timer`, `stop_local_timer`, `restart_local_timer`, `register_local_timer_handler`, `arch_timer_int_handler`, `cycles_accounting_init`, `context_stop(_idle)`, `ms_2_cpu_time`, `cpu_time_2_ms`, `cpu_load`, `get_cpu_ticks` | тик через BSP-таймер, учёт по свободному счётчику |
| `arch_system.c` | `fpu_init`, `save_local_fpu`, `save_fpu`, `restore_fpu`, `fpu_sigcontext` | **пустые заглушки**: состояние VFP/NEON не сохраняется |
| `arch_system.c` | `get_randomness`, `do_ser_debug` (пусто) | |
| `arch_reset.c` | `halt_cpu`, `reset`, `poweroff`, `arch_shutdown`, `ser_putc` | через BSP |
| `hw_intr.c` | `hw_intr_mask`, `hw_intr_unmask` | обёртки над `bsp_irq_*` |
| `direct_tty_utils.c` | `direct_cls`, `direct_print(_char)`, `direct_read_char` (всегда 0) | вывод идёт в последовательный порт |
| `do_padconf.c` | `do_padconf` | `SYS_PADCONF` |

Общий код ядра с ветвлениями под ARM: `minix/kernel/system.c` регистрирует
`SYS_PADCONF` только `#if defined(__arm__)` (а `SYS_DEVIO/VDEVIO` — только для
i386); `main.c` — ранний `arch_ser_init()`; `proc.c` — проверка `p_seg.p_ttbr`.

## 4. VM-сервер (earm)

- `arch/earm/pagetable.h`: `PTF_WRITE/READ/PRESENT/SUPER/USER/NOCACHE(DEVICE)/
  CACHEWB/CACHEWT/SHARE`, `ARCH_VM_DIR_ENTRIES` (4096), `ARCH_BIG_PAGE_SIZE`
  (секция 1 МБ), `ARCH_VM_PT_ENTRIES` (256), `PFERR_PROT/NOPAGE/WRITE/READ` по
  полю FS регистра DFSR.
- `arch/earm/vm.lds` — отдельный linker script VM (подключается в
  `minix/servers/vm/Makefile` только для earm).
- Архитектурные ветвления в **общем** коде VM:
  - `pagetable.c` — ~60 мест `__arm__`/`__i386__`: атрибуты кеширования
    (`ARM_VM_PTE_CACHED`, `ARM_VM_PTE_DEVICE`, `ARM_VM_PTE_SUPER`), домены в PDE
    (`ARM_VM_PDE_DOMAIN`, с пометкой `//LSC FIXME`), выравнивание 16-КБ каталога
    (`static_sparepagedirs`), увеличенный запас страниц `SPAREPAGES` (150 против 20);
  - `mem_anon.c`, `mem_anon_contig.c`, `mem_cache.c`, `mem_directphys.c`,
    `mem_file.c`, `mem_shared.c` — `pt_flags` возвращает `ARM_VM_PTE_CACHED` под
    `#if defined(__arm__)`;
  - `utility.c` — то же при отображении страниц кучи.
- `pt_t` — тот же двухуровневый `u32_t`-формат, что на i386.

## 5. Сводная таблица возможностей

Статус: ✅ реализовано · ⚠️ частично / с оговорками · ❌ нет.

| Возможность | Статус | Реализация / что сделать |
|---|---|---|
| Архитектура | ⚠️ | ARMv7-A 32 бита; aarch64 — отдельный порт |
| Поддержка SoC/плат | ⚠️ | только TI: BeagleBoard-xM (DM37xx), BeagleBone/Black (AM335x); нужен generic ARMv7 + механизм BSP по FDT. Эталонная плата — Orange Pi PC Plus (H3), шаги Б1–Б2 модернизации |
| Загрузка | ⚠️ | U-Boot (форк MINIX3) + **фиксированные адреса модулей** и эмуляция multiboot → **U-Boot + fitImage** (п. 6 модернизации) |
| Описание оборудования | ❌ | нет Device Tree; плата по `board_name=`, адреса периферии в коде → нужен разбор DTB (естественно получается вместе с fitImage) |
| Карта памяти | ❌ | жёстко 256 МБ с `0x80000000` → брать из DTB/U-Boot |
| MMU short-descriptor (4 КБ + секции 1 МБ) | ✅ | `pg_utils.c`, `memory.c`, VM `pagetable.c` |
| LPAE (long-descriptor) | ❌ | не требуется для Cortex-A8; для aarch64 формат другой |
| XN (запрет исполнения) | ❌ | бит не используется |
| Атрибуты кеширования | ⚠️ | TEX/C/B заданы; ветвления в общем коде VM — вынести в абстрактные таблицы страниц (п. 3) |
| Домены ARM | ⚠️ | `DACR = 0x55555555` (все домены — client), поле домена в PDE выставляется с пометкой FIXME; фактически не используются, в aarch64 отсутствуют |
| Глобальные / неглобальные страницы (nG) | ❌ | бит `ARM_VM_PTE_NOTGLOBAL` определён, но не используется |
| ASID (CONTEXTIDR) | ❌ | **нет**: каждая запись TTBR0 → полный сброс TLB, I-cache и BTB. Жизненно важно исправить (п. 5 модернизации) |
| Выборочная инвалидация TLB | ❌ | только TLBIALL |
| TTBR1 для ядра | ❌ | `write_ttbcr(0)` в `pg_utils.c` — используется только TTBR0, ядро отображается в каждое пространство процесса |
| Обслуживание кешей | ✅ | `dcache_maint` (set/way), барьеры |
| Вход в ядро | ✅ | только `svc` (IPC и kernel call различаются по `r3`) |
| Исключения, page fault | ✅ | `exception.c`, DFAR/DFSR/IFAR/IFSR |
| Контроллер прерываний | ⚠️ | только TI INTC; для других SoC и для aarch64 нужен **GIC** |
| Системный таймер | ⚠️ | GPTIMER/DMTIMER через BSP; ARM Generic Timer не поддерживается |
| Счётчик циклов | ✅ | PMU CCNT (доступен из user) + свободный счётчик таймера |
| Частота CPU | ❌ | жёстко 660 МГц в `cpu_identify` |
| FPU (VFP/NEON) | ❌ | **заглушки**: контекст VFP не сохраняется, при переключениях возможна порча; soft-float ABI. Реализовать ленивое сохранение VFP |
| SMP | ❌ | `#error CONFIG_SMP is unsupported on ARM` (п. 4 модернизации); запуск вторичных CPU — через PSCI, см. §7 |
| Спинлоки/атомарные операции | ❌ | в ядре не нужны без SMP; реализовать на `LDREX/STREX` + барьерах |
| TLS-регистр потока | ❌ | `TPIDRURO` не используется (п. 8, `docs/threads.md`) |
| Watchdog (NMI-аналог) | ❌ | аппаратный WDT отключается при загрузке |
| Статистическое профилирование | ❌ | нет `arch_init_profile_clock` |
| Аппаратные точки останова | ❌ | нет |
| Pin mux для драйверов | ✅ | `SYS_PADCONF` |
| Консоль ядра | ✅ | отладочный UART платы |
| Ранняя консоль / чтение | ⚠️ | вывод в UART; `direct_read_char` не реализован |
| Reset / poweroff | ✅ | через BSP (PRM); на платах с PSCI — `SYSTEM_RESET` / `SYSTEM_OFF`, см. §7 |
| RTC | ✅ | `omap_rtc.c` |
| Абстрактные таблицы страниц | ❌ | п. 3 модернизации |
| `kyield` / `kmutex` | ❌ | п. 1–2 модернизации |

## 6. Замечания для модернизации и портов arm32/aarch64

- Интерфейс `bsp/include` — удачная основа для разделения «общий ARM / плата»
  (как `sys/arch/arm` / `sys/arch/evbarm` в NetBSD). Но сейчас в ядре нет
  понятия «драйвер контроллера прерываний / таймера», выбираемого по Device
  Tree: всё связывается на этапе сборки. Для generic arm32 и aarch64 нужны GIC,
  ARM Generic Timer, PL011/ns16550 UART и выбор реализаций по DTB.
- Отсутствие ASID и полный сброс TLB, I-cache и BTB на каждом переключении
  адресного пространства — главная причина низкой производительности IPC на ARM.
  Это первоочередная цель п. 5 модернизации.
- Ненулевой риск порчи данных: FPU-заглушки при коде, использующем VFP (если
  тулчейн или приложения генерируют VFP-инструкции). Для hard-float/aarch64
  сохранение FP/SIMD-контекста обязательно.
- Жёстко заданные карта памяти, адреса модулей и частота CPU уходят вместе с
  переходом на fitImage + DTB.
- Запуск вторичных CPU, reset и poweroff на новых платах делать через PSCI, а
  не через регистры питания конкретного SoC (§7).

## 7. План: PSCI — запуск вторичных CPU и управление питанием

> Проектное решение для п. 4 модернизации (`docs/modernization.md`). В текущем
> коде earm PSCI нет. Подход проверен разбором тупиковой ветки: там AP на
> Allwinner H3 поднимались прямой записью в CPUCFG/PRCM (`h3_cpu.c`), и этот путь
> оказался ненадёжным (§7.8).

### 7.1 Что такое PSCI

PSCI (Power State Coordination Interface, спецификация ARM DEN 0022) — стандартный
интерфейс вызова прошивки для управления питанием ядер и системы. ОС не трогает
регистры питания и сброса SoC. Она вызывает функцию прошивки инструкцией `smc`
(или `hvc` под гипервизором), а прошивка выполняет последовательность, специфичную
для чипа.

Зачем это нужно:
- после U-Boot ядро ОС работает в **Non-secure**, а регистры питания ядер обычно
  доступны только из Secure world;
- у каждого SoC своя схема питания (Allwinner CPUCFG + PRCM, Rockchip PMU, i.MX
  SRC). PSCI заменяет их одним вызовом `CPU_ON`, одинаковым для H3, RK3288,
  A64/H5, RK3399 и `qemu -M virt`;
- AP, поднятый прошивкой, стартует в том же состоянии безопасности, что и BSP.

### 7.2 Кто реализует PSCI

| Платформа | Реализация | Вызов |
|---|---|---|
| Allwinner H3 / Orange Pi PC Plus (ARMv7) | U-Boot (`CONFIG_ARMV7_PSCI`, резидентный код в secure SRAM) | `smc` |
| aarch64-платы (A64, H5, RK3399, i.MX8) | TF-A (BL31) на EL3 | `smc` |
| QEMU `-M virt` | встроен в QEMU | `hvc` или `smc` в зависимости от конфигурации |
| Под гипервизором | гипервизор | `hvc` |
| Raspberry Pi 3 | обычно **нет**; `enable-method = "spin-table"` | запись адреса в память + `sev` |

Способ вызова и метод запуска ядер **не зашиваются в код**, а берутся из FDT.

### 7.3 Обнаружение через FDT

U-Boot добавляет в DTB, передаваемый ядру:

```dts
psci {
    compatible = "arm,psci-1.0", "arm,psci-0.2";
    method = "smc";                     /* или "hvc" */
};
cpus {
    #address-cells = <1>;
    cpu@0 { device_type = "cpu"; reg = <0>; enable-method = "psci"; };
    cpu@1 { device_type = "cpu"; reg = <1>; enable-method = "psci"; };
};
```

- `cpu@N/reg` — аффинитет MPIDR, аргумент `CPU_ON`. Число CPU тоже берётся
  отсюда, а не из констант BSP.
- `enable-method`: `psci` или `spin-table` (тогда рядом есть `cpu-release-addr`).
- Резидентный код прошивки в DRAM помечается `/memreserve/` или
  `/reserved-memory`. Ранняя карта памяти ядра обязана исключить эти области
  (`docs/memory.md` §3.3, тип `BM_FIRMWARE`).
- Старый вариант `compatible = "arm,psci"` (0.1) задаёт номера функций свойствами
  `cpu_on`, `cpu_off` и т.д. Поддерживать его имеет смысл только при
  необходимости.

### 7.4 Соглашение о вызове (SMCCC) и функции

Номер функции передаётся в `r0` / `x0`, аргументы — в `r1..r3` / `x1..x3`. Затем
выполняется `smc #0` или `hvc #0`, результат возвращается в `r0` / `x0`. На arm32
прошивка сохраняет r4–r14. В ассемблере нужны `.arch_extension sec` и
`.arch_extension virt`.

```c
static inline int32_t psci_call_smc(uint32_t fn, uint32_t a1, uint32_t a2, uint32_t a3)
{
	register uint32_t r0 asm("r0") = fn, r1 asm("r1") = a1,
	                  r2 asm("r2") = a2, r3 asm("r3") = a3;
	asm volatile(".arch_extension sec\n\tsmc #0"
	             : "+r"(r0) : "r"(r1), "r"(r2), "r"(r3) : "memory");
	return (int32_t)r0;
}
```

| Функция | ID (SMC32 / SMC64) | Назначение в ReMinix |
|---|---|---|
| `PSCI_VERSION` | 0x84000000 | проверить наличие и версию (0.2 / 1.0 / 1.1) |
| `PSCI_FEATURES` | 0x8400000A | проверить поддержку отдельной функции (с 1.0) |
| `CPU_ON` | 0x84000003 / 0xC4000003 | запуск AP |
| `CPU_OFF` | 0x84000002 | выключить текущее ядро (горячее отключение, останов системы) |
| `AFFINITY_INFO` | 0x84000004 / 0xC4000004 | состояние ядра: таймаут запуска AP, проверка после `CPU_OFF` |
| `CPU_SUSPEND` | 0x84000001 / 0xC4000001 | глубокий сон ядра в idle (энергопотребление) |
| `SYSTEM_RESET` | 0x84000009 | перезагрузка |
| `SYSTEM_OFF` | 0x84000008 | выключение |
| `SYSTEM_RESET2`, `SYSTEM_SUSPEND` | 0x84000012, 0x8400000E | опционально, позже |

Коды возврата: 0 `SUCCESS`, −1 `NOT_SUPPORTED`, −2 `INVALID_PARAMETERS`,
−3 `DENIED`, −4 `ALREADY_ON`, −5 `ON_PENDING`, −6 `INTERNAL_FAILURE`,
−9 `INVALID_ADDRESS`.

### 7.5 Семантика `CPU_ON` и требования к точке входа AP

`CPU_ON(mpidr, entry_pa, context_id)` **асинхронный**: `SUCCESS` означает, что
запуск начат. BSP ждёт флага готовности от AP с таймаутом. Если флага нет,
проверяет `AFFINITY_INFO` и продолжает работу без этого ядра.

Состояние AP на входе:
- выполнение с `entry_pa` — **физического** адреса;
- **MMU и D-cache выключены**;
- `r0` / `x0` = `context_id`. ReMinix передаёт туда номер CPU;
- Non-secure, тот же режим, в котором U-Boot передал управление BSP. На H3 с
  `bootm_boot_mode=nonsec` это **HYP**, поэтому переключение HYP→SVC нужно и на AP.

Требования к коду ReMinix:
1. **До `CPU_ON`** BSP сбрасывает из кэша до PoC всё, что AP читает при
   выключенном MMU: код trampoline, таблицу стеков, значение TTBR и флаги.
2. **Trampoline** использует только физические адреса или позиционно-независимый
   код, никаких `ldr =виртуальный_символ`.
3. **Общая процедура входа** в `head.S` для BSP и AP, по порядку:
   1. HYP→SVC первыми инструкциями;
   2. стек из массива стеков по номеру CPU из `r0`;
   3. `ACTLR.SMP`, если прошивка его не выставила;
   4. TTBCR / TTBR0 / TTBR1 / DACR с таблицами ядра, ASID 0;
   5. включение MMU и кэшей;
   6. переход на виртуальный `ap_main(cpu)`.
4. **На AP**, до входа в планировщик:
   - VBAR;
   - CPU-интерфейс GIC (`GICC_CTLR` / `GICC_PMR` у каждого ядра свои);
   - локальный generic timer;
   - VFP;
   - пометка «готов»;
   - вход в планировщик под BKL (или новой схемой блокировок п. 4).

### 7.6 Встраивание в ядро

```
minix/kernel/arch/earm/psci.c        (почти тот же файл пойдёт на aarch64)
    psci_init(fdt)          найти /psci (libfdt в ядре, п. 10 модернизации),
                            выбрать smc/hvc, PSCI_VERSION, PSCI_FEATURES
    psci_cpu_on(mpidr, entry_pa, ctx)
    psci_cpu_off(), psci_affinity_info(mpidr)
    psci_system_reset(), psci_system_off(), psci_cpu_suspend(state)

arch_smp_boot_cpu(cpu):
    enable-method узла cpu@N:
      "psci"        -> psci_cpu_on
      "spin-table"  -> записать entry в cpu-release-addr, dsb, sev
      нет метода    -> bsp_boot_cpu()  (запасной путь конкретного SoC)

arch_reset / arch_poweroff:
    PSCI есть -> SYSTEM_RESET / SYSTEM_OFF, иначе bsp_reset / bsp_poweroff
```

Интерфейс `bsp_cpu.h` (`bsp_boot_cpu`, `bsp_stop_cpu`) остаётся **необязательным**:
он нужен только платам без PSCI и без spin-table. Для TI AM335x и OMAP3
(одноядерные) SMP не нужен, reset и poweroff остаются через PRM.

### 7.7 Особенности Allwinner H3

- **Проверить, что PSCI есть.** В U-Boot для sun8i `CONFIG_ARMV7_PSCI` включён по
  умолчанию. Проверка: в консоли U-Boot `fdt addr <адрес>; fdt print /psci` после
  загрузки DTB.
- **Режим на время отладки.** Можно поставить `bootm_boot_mode=sec`: ядро войдёт
  в SVC в Secure. Но тогда U-Boot, вероятно, не установит PSCI, и AP придётся
  поднимать вручную. Основной путь — nonsec с переключением HYP→SVC.
- **Регистры GIC.** В Non-secure регистры групп прерываний (`GICD_IGROUPR`) не
  доступны. U-Boot сам переводит прерывания в Group 1, драйвер GIC ReMinix их не
  трогает.
- **Адреса GIC-400 на H3:** дистрибьютор `0x01C81000`, CPU-интерфейс `0x01C82000`.
  Брать из FDT, а не из констант.

### 7.8 Почему не прямое управление CPUCFG/PRCM (урок тупиковой ветки)

В тупиковой ветке `h3_cpu.c:bsp_boot_cpu` писал регистры Allwinner напрямую и
допустил сразу несколько ошибок:
- адрес входа писался в `0x1A4 + cpu*4`, а у H3 один регистр `PRIVATE0` по
  `0x1A4`;
- зажим питания снимался не полностью;
- полярность `GEN_CTRL` была инвертирована;
- не было задержек;
- AP запускался в Secure при BSP в Non-secure.

Кроме того, ядро в Non-secure конкурирует с прошивкой, которая управляет теми же
регистрами. PSCI убирает весь этот класс ошибок.

### 7.9 Аналоги на других целевых архитектурах

Общий интерфейс `arch_smp_boot_cpu` / `arch_reset` / `arch_poweroff` /
`arch_cpu_idle` ложится на все порты:

| Архитектура | Запуск AP | Reset / off | Источник топологии |
|---|---|---|---|
| arm32 / aarch64 | PSCI `CPU_ON` (или spin-table) | PSCI `SYSTEM_RESET` / `SYSTEM_OFF` | FDT `/cpus`, `/psci` |
| riscv64 | SBI HSM `sbi_hart_start(hartid, start_pa, opaque)` — прямой аналог `CPU_ON` (трансляция выключена, `opaque` в `a1`); есть `hart_stop`, `hart_suspend`, `hart_get_status` | SBI SRST | FDT `/cpus` |
| amd64 | INIT–SIPI–SIPI через LAPIC, trampoline ниже 1 МБ | ACPI / контроллер | ACPI MADT |
| mips64 | стандарта нет: CPS/CPC (MIPS R6), платформенные механизмы Octeon / Loongson | зависит от платформы | FDT |
