# Порт i386 — реализация архитектурно-зависимой части

> Долгоживущий справочник по текущему (унаследованному от MINIX 3, 2018) порту
> **i386**: структура каталогов, файлы, конкретные функции ядра и VM-сервера,
> сводная таблица возможностей. Составлен по исходникам в сентябре 2026.
> Обновляйте при изменении самого порта (этап модернизации —
> `docs/modernization.md` — затронет его сильно). Парный документ —
> `docs/arch-earm.md`; общая картина — `docs/architecture.md` §7.

## 1. Общая характеристика

- `MACHINE=i386`, `MACHINE_ARCH=i386`, тулчейн `i586-elf32-minix-`
  (`GNU_ARCH.i386=i586`, `GCC_CONFIG_ARCH.i386=i586` в `share/mk/bsd.own.mk`).
- 32-битный защищённый режим, плоская сегментная модель, страничная адресация
  **без PAE**: двухуровневые таблицы, страницы 4 КБ, большие страницы 4 МБ (PSE).
- Ядро слинковано по физическому адресу `0x00400000` и отображено высоко по
  виртуальному `0xF0400000` (`kernel.lds`), пользователю остаётся ~3,75 ГБ.
- Загрузка — **multiboot** (заголовок в `head.S`), загрузчик MINIX
  (`bootxx_minixfs3` / boot monitor) или GRUB (EFI-вариант).
- Самый полный порт: APIC/IOAPIC, ACPI, SMP (опционально), FPU/SSE, NMI-watchdog,
  аппаратные точки останова, три способа входа в ядро.

### 1.1 Параметры сборки

Для `MACHINE_ARCH == i386` по умолчанию `MKWATCHDOG=MKACPI=MKAPIC=MKDEBUGREG=MKPCI=yes`,
отсюда `USE_WATCHDOG/USE_ACPI/USE_APIC/USE_DEBUGREG=yes`
(`share/mk/bsd.own.mk`; `MKSMALL=yes` отключает их). Прочие:

| Переменная | По умолчанию | Действие |
|---|---|---|
| `CONFIG_SMP` (+ `CONFIG_MAX_CPUS`) | не задана | включает `smp.c`, `arch_smp.c`, `trampoline.S`, `-DCONFIG_SMP` |
| `MKPAE` | `no` | `-DPAE=1` в ядре и VM — **но реализации PAE нет** (см. §5) |
| `USE_APIC` | yes | `apic.c`, `apic_asm.S`, `-DUSE_APIC` |
| `USE_ACPI` | yes | `acpi.c`, `-DUSE_ACPI` |
| `USE_DEBUGREG` | yes | `breakpoints.c`, `debugreg.S` |
| `USE_WATCHDOG` | yes | `arch_watchdog.c` (+ общий `watchdog.c`) |
| `CONFIG_OXPCIE` | не задана | поддержка PCIe-UART Oxford OXPCIe952 (`oxpcie.c`) |

## 2. Структура каталогов порта

```
minix/kernel/arch/i386/            — машинно-зависимая часть ядра
├── Makefile.inc                   — состав, unpaged-объекты, procoffsets.h
├── kernel.lds                     — linker script (phys 0x00400000 / vir 0xF0400000)
├── procoffsets.cf, sconst.h       — смещения полей struct proc для ассемблера
├── head.S, pre_init.c             — точка входа multiboot, ранняя инициализация (до paging)
├── pg_utils.c                     — ранние таблицы страниц, карта памяти, включение paging
├── mpx.S                          — входы в ядро: прерывания, исключения, IPC, kernel call; выход
├── klib.S                         — низкоуровневые примитивы (копирование, CR, MSR, FPU)
├── protect.c                      — GDT/IDT/TSS, сегменты, шлюзы прерываний
├── exception.c                    — обработка исключений, page fault, стек-трейсы
├── memory.c                       — копирование между АП, umap/vm_lookup, отображения для VM, usermapped
├── arch_system.c                  — FPU, init, контекст процесса, выход в user, отладка
├── arch_clock.c                   — таймеры (PIT/LAPIC), учёт циклов (TSC), загрузка CPU
├── arch_do_vmctl.c                — VMCTL: смена CR3, TLB
├── arch_reset.c                   — reset/halt/poweroff, отладочный последовательный вывод
├── i8259.c                        — контроллер прерываний 8259 PIC
├── apic.c, apic.h, apic_asm.S/.h  — LAPIC/IOAPIC, IPI, калибровка        [USE_APIC]
├── acpi.c, acpi.h                 — таблицы ACPI (RSDP/RSDT/MADT), poweroff  [USE_ACPI]
├── arch_smp.c, trampoline.S       — запуск AP, IPI планирования/останова  [CONFIG_SMP]
├── arch_spinlock.h                — спинлоки MCS на <stdatomic.h> (static inline) [CONFIG_SMP]
├── arch_watchdog.c                — NMI-watchdog на счётчиках производительности [USE_WATCHDOG]
├── breakpoints.c, debugreg.S/.h   — аппаратные точки останова DR0–DR7   [USE_DEBUGREG]
├── do_iopenable.c, do_readbios.c, do_sdevio.c — i386-специфичные kernel calls
├── io_{inb,inw,inl,outb,outw,outl,intr}.S — порт-ввод/вывод, cli/sti
├── direct_tty_utils.c             — прямой вывод в VGA-текстовый буфер
├── oxpcie.c, oxpcie.h, serial.h   — последовательные порты
├── usermapped_data_arch.c, usermapped_glo_ipc.S — IPC-заглушки в user-mapped странице
├── glo.h                          — глобальные переменные порта
└── include/
    ├── arch_proto.h               — прототипы порта, структуры TSS/дескрипторов
    ├── archconst.h                — константы (селекторы, векторы, K_STACK_SIZE ...)
    ├── arch_clock.h, arch_smp.h, arch_watchdog.h, hw_intr.h, direct_utils.h

minix/servers/vm/arch/i386/        — машинно-зависимая часть VM-сервера
├── Makefile.inc                   — только .PATH
└── pagetable.h                    — отображение ARCH_VM_* / PTF_* на биты I386_VM_*

minix/include/arch/i386/include/   — устанавливается как <machine/...>
├── vm.h        — биты PTE/PDE, CR0/CR4, коды ошибок page fault, CPUID-флаги
├── stackframe.h — struct stackframe_s (сохранённые регистры), segframe
├── archtypes.h, archconst.h, interrupt.h (векторы, IRQ), ipcconst.h (IPCVEC/KERVEC)
├── memory.h, bios.h, cmos.h, ports.h, fpu.h, elf.h, diskparm.h, partition.h
└── pci*.h      — PCI (в т.ч. вендорские мосты AMD/Intel/SiS/VIA)
```

Unpaged-объекты (`head.o pre_init.o direct_tty_utils.o pg_utils.o klib.o
utility.o arch_reset.o io_inb.o io_outb.o` + куски libc/libminc/libsys)
получают префикс символов `__k_unpaged_` через `objcopy` и линкуются в секции
`.unpaged_*` по физическому адресу — это код, работающий до включения paging.

## 3. Файлы и функции ядра

### 3.1 Загрузка и ранняя инициализация

| Файл | Функции / метки | Назначение |
|---|---|---|
| `head.S` | `MINIX`, заголовок multiboot, `multiboot_init` | точка входа: стек, `pre_init()`, затем `kmain()` |
| `pre_init.c` | `pre_init(magic, ebx)`, `get_parameters()`, `mb_set_param()`, `overlaps()` | разбор multiboot info (модули, карта памяти, cmdline) в `kinfo_t`; ранние таблицы страниц; включение paging |
| `pg_utils.c` | `print_memmap`, `cut_memmap`, `add_memmap`, `alloc_lowest`, `pg_alloc_page`, `alloc_pagetable`, `pg_identity`, `pg_mapkernel`, `vm_enable_paging`, `pg_load`, `pg_clear`, `pg_map`, `pg_rounddown`, `pg_info` | карта физической памяти, identity-отображение, отображение ядра 4-МБ страницами, включение `CR0.PG/WP`, `CR4.PSE`, затем `PGE` |
| `protect.c` | `prot_init`, `idt_init`, `idt_copy_vectors(_pic)`, `int_gate(_idt)`, `init_codeseg/init_dataseg/init_param_dataseg`, `init_segdesc`, `tss_init`, `prot_load_selectors`, `idt_reload`, `enable_iop`, `vir2phys`, `bootmod` | GDT, IDT, TSS на каждый CPU, селекторы; `bootmod()` — доступ к multiboot-модулям |
| `arch_system.c` | `arch_init`, `cpu_identify`, `ser_init` | инициализация порта: FPU, APIC/ACPI, выбор способа входа в ядро, последовательный порт |

### 3.2 Входы в ядро и выход в пространство пользователя

| Файл | Функции / метки | Назначение |
|---|---|---|
| `mpx.S` | `hwint00`…`hwint15` | входы аппаратных прерываний (PIC) |
| | `ipc_entry_softint_orig/_um`, `ipc_entry_sysenter`, `ipc_entry_common` | вход IPC: `int $IPCVEC` (два варианта), `SYSENTER` |
| | `kernel_call_entry_orig/_um/_common` | вход kernel call (`int $KERVEC`) |
| | `exception_entry(_from_user/_nested)`, `divide_error`, `page_fault`, `general_protection`, … `simd_exception` | обработчики исключений |
| | `restore_user_context_int/_sysenter/_syscall` | выход в user: `iret` / `SYSEXIT` / `SYSRET` |
| | `startup_ap_32`, `reload_cr3`, стеки `k_stacks_start..end` | вход AP (SMP), перезагрузка CR3, стеки ядра на CPU |
| `arch_system.c` | `arch_do_syscall`, `arch_finish_switch_to_user`, `restore_user_context`, `arch_proc_setcontext`, `arch_proc_reset`, `arch_set_secondary_ipc_return` | выбор способа выхода по `p_seg.p_kern_trap_style` (`KTS_INT_*`, `KTS_SYSENTER`, `KTS_SYSCALL`, `KTS_FULLCONTEXT`) |
| `usermapped_glo_ipc.S`, `usermapped_data_arch.c` | `usermapped_{send,receive,sendrec,sendnb,notify,senda,do_kernel_call}_{softint,sysenter,syscall}`; таблицы `minix_ipcvecs_softint/_sysenter/_syscall` | IPC-заглушки в странице, отображаемой в каждый процесс; `memory.c` при загрузке выбирает таблицу по `MKF_I386_INTEL_SYSENTER` / `MKF_I386_AMD_SYSCALL` и публикует через `minix_kerninfo` |

### 3.3 Исключения

| Файл | Функции | Назначение |
|---|---|---|
| `exception.c` | `exception_handler`, `pagefault`, `inkernel_disaster`, `proc_stacktrace(_execute)`, `enable_fpu_exception`, `disable_fpu_exception` | page fault пользователя → уведомление VM (`RTS_PAGEFAULT`); сбои внутри ядра при копировании (`phys_copy_fault`, `memset_fault`) → возврат ошибки; стек-трейс по цепочке `%ebp` |

### 3.4 Память и адресные пространства

| Файл | Функции | Назначение |
|---|---|---|
| `memory.c` | `memory_init`, `createpde`, `mem_clear_mapcache`, `lin_lin_copy` | копирование между адресными пространствами через **окна 4 МБ** — свободные PDE ядра (`freepdes[]`, `MAXFREEPDES=2`), выданные VM |
| | `virtual_copy_f`, `data_copy`, `data_copy_vmcheck`, `vm_memset`, `check_resumed_caller` | реализация `SYS_VIRCOPY`/`SAFECOPY`/`MEMSET`; при отсутствии страницы — приостановка вызывающего и запрос к VM |
| | `umap_virtual`, `vm_lookup`, `vm_lookup_range`, `vm_check_range` | **ядро само обходит аппаратные таблицы страниц процесса** (PDE/PTE, в т.ч. 4-МБ) |
| | `arch_phys_map`, `arch_phys_map_reply` | список физических областей, которые VM должен отобразить в АП ядра: VGA-буфер, usermapped-страницы (`VMMF_USER`, `VMMF_GLO`), LAPIC, IOAPIC, OXPCIe |
| | `arch_enable_paging`, `arch_proc_init`, `release_address_space`, `platform_tbl_ptr`, `platform_tbl_checksum_ok` | включение адресного пространства VM, начальный контекст процесса, поиск таблиц BIOS/ACPI |
| `arch_do_vmctl.c` | `arch_do_vmctl`, `setcr3` | `VMCTL_GET_PDBR`, `VMCTL_SETADDRSPACE` (запись CR3, снятие `RTS_VMINHIBIT`), `VMCTL_FLUSHTLB` (`reload_cr3`), `VMCTL_I386_INVLPG` (выборочная инвалидация) |
| `klib.S` | `__switch_address_space`, `refresh_tlb`, `reload_cr3` (в mpx.S) | переключение CR3 при смене процесса |

### 3.5 Низкоуровневые примитивы (`klib.S`, `io_*.S`)

`phys_copy`, `phys_memset`, `copy_msg_from_user`, `copy_msg_to_user` (с метками
сбоя), `phys_insb/insw/outsb/outsw`, `read_cpu_flags`, `read_cs/ds/ss`,
`read_ebp`, `x86_load_kerncs`, `ia32_msr_read/write`, `fninit`, `clts`,
`fnstsw`, `fxrstor`, `frstor`, `halt_cpu`, `x86_triplefault`,
`poweroff_vmware_clihlt`, `eoi_8259_master/slave`, `smp_get_htt/num_htt/cores`,
`mfence`, `arch_pause`,
`interrupts_enable/disable`, `switch_k_stack`; макросы `ARG_EAX_*` генерируют
обёртки чтения/записи CR0/CR2/CR3/CR4 и т.п. `io_*.S`: `inb/inw/inl`,
`outb/outw/outl`, `intr_disable/intr_enable`.

### 3.6 Прерывания

| Файл | Функции | Назначение |
|---|---|---|
| `i8259.c` | `intr_init`, `irq_8259_mask/unmask`, `irq_8259_eoi`, `i8259_disable` | классический PIC (используется без APIC и до его включения) |
| `apic.c` | `lapic_enable/disable`, `apic_idt_init`, `apic_calibrate_clocks`, `lapic_set_timer_one_shot/periodic`, `lapic_stop_timer`, `lapic_microsec_sleep`, `detect_ioapics`, `ioapic_enable_all/disable_all`, `ioapic_set_irq/unset_irq`, `ioapic_mask/unmask_irq`, `ioapic_eoi`, `arch_eoi`, `apic_send_ipi`, `apic_send_init_ipi`, `apic_send_startup_ipi`, `apic_single_cpu_init`, `apicid`, `dump_apic_irq_state` | LAPIC (таймер, IPI, EOI), IOAPIC (маршрутизация IRQ, edge/level EOI) |
| `apic_asm.S` | `lapic_timer_int_handler`, `apic_spurios_intr`, `apic_error_intr`, `apic_ipi_sched_intr`, `apic_ipi_halt_intr`, `lapic_intr_dummy_handles_*` | входы векторов APIC |
| `acpi.c` | `acpi_init`, `acpi_get_table_base/length`, `acpi_get_ioapic_next`, `acpi_get_lapic_next`, `acpi_poweroff` | RSDP/RSDT, MADT (список CPU и IOAPIC), выключение через FADT |

### 3.7 Время

| Файл | Функции | Назначение |
|---|---|---|
| `arch_clock.c` | `init_8253A_timer`, `stop_8253A_timer`, `init_local_timer`, `stop_local_timer`, `restart_local_timer`, `register_local_timer_handler`, `arch_timer_int_handler` | системный тик: PIT 8253 или LAPIC-таймер |
| | `estimate_cpu_freq`, `cycles_accounting_init`, `context_stop`, `context_stop_idle`, `ms_2_cpu_time`, `cpu_time_2_ms`, `cpu_load`, `busy_delay_ms`, `get_cpu_ticks` | учёт времени по **TSC**, частота CPU, загрузка |
| `arch_system.c` | `arch_init_profile_clock`, `arch_stop_profile_clock`, `arch_ack_profile_clock` | статистическое профилирование по прерыванию CMOS RTC |

### 3.8 FPU

`arch_system.c`: `fpu_init`, `save_local_fpu`, `save_fpu`, `restore_fpu`,
`fpu_sigcontext`. Ленивое сохранение (исключение «сопроцессор недоступен» →
`copr_not_available_handler`); `FXSAVE/FXRSTOR`, если есть FXSR+SSE (`osfxsr_feature`),
иначе `FNSAVE/FRSTOR`. XSAVE/AVX не поддерживаются.

### 3.9 SMP (`CONFIG_SMP`)

`arch_smp.c`: `smp_init`, `discover_cpus` (MADT), `copy_trampoline`,
`smp_start_aps`, `smp_ap_boot`, `ap_finish_booting`, `tss_init_all`,
`smp_reinit_vars`, `smp_halt_cpu`, `smp_shutdown_aps`, `arch_smp_halt_cpu`,
`arch_send_smp_schedule_ipi`; спинлоки `smp_cpu_lock`, `dispq_lock` определены, но
нигде не захватываются.
`trampoline.S`: `trampoline` (16-битный старт AP, `__ap_gdt/__ap_idt/__ap_pt`).
Общий код — `minix/kernel/smp.c`; ядро защищено Big Kernel Lock.

Номер CPU (`cpuid`, `arch/i386/include/arch_smp.h`) читается из верхнего слова
текущего стека ядра (его пишет `tss_init()`; на стеке загрузки `k_initial_stack` —
`push $0` в `head.S`, стек выровнен по странице). Загрузочный CPU — всегда
логический CPU 0 (`docs/modernization.md`, п. 4, «Нумерация CPU»):
`discover_cpus()` берёт его APIC ID из CPUID до разбора MADT, остальные CPU — 1, 2, …
в порядке MADT; `smp_init()` сверяет с регистром LAPIC ID.

**`lapic_addr` меняет смысл** (`apic.c:148`, тип `vir_bytes`): до включения
страничной адресации — **физический** адрес LAPIC (`LOCAL_APIC_DEF_ADDR` или из
MADT, `arch_smp.c:359`); его же отдаёт VM `arch_phys_map()` (`memory.c:831`) для
отображения. После ответа VM (`arch_enable_paging()`, `memory.c:965`) в него
пишется **виртуальный** адрес отображения из `lapic_addr_vaddr`, и с этого момента
`LAPIC_*` (`apic.h`) обращаются через него. Нулевое значение — «LAPIC не
используется». В А1.3 это разделяется на две переменные: `phys_addr_t` и
`vir_addr_t` (`work-plan.md` §5).

#### Пути IPI, EOI и таймер (2026-10-05, коммит `86103152f`)

Без виртуализации APIC в KVM каждое обращение к xAPIC — выход в гипервизор;
пробуждение процесса на другом CPU стоило ~11 таких обращений (~16–19 мкс
времени ядра). Сейчас:

- **Решение и отправка.** `enqueue()` (`proc.c`) для процесса чужого CPU:
  цель простаивает — `smp_kick_idle()`; цель занята вытесняемым процессом
  более низкого приоритета — `smp_schedule()`. Обе лишь ставят бит в
  cpu-local `ipi_pending` (решение — под BKL). `context_stop(KERNEL)`
  отпускает BKL и вызывает `smp_flush_ipis()`: IPI уходят после замка,
  несколько на один CPU сливаются. `smp_schedule_sync()` (STOP_PROC,
  VM_INHIBIT, SAVE_CTX) шлёт свой IPI сразу и ждёт ответа без BKL; слот
  `sched_ipi_data[cpu]` и `p->p_cpu` перепроверяются после каждого ожидания,
  если процесс оказался на своём CPU — задача выполняется на месте
  (`smp_sched_task()`).
- **`apic_send_ipi()`** — ICR из констант (fixed, physical, level assert),
  ICR2 = APIC ID << 24: две записи, без чтений. Ожидание Delivery Status —
  только на железе (под гипервизором, CPUID.1:ECX[31], пропускается).
  INIT/SIPI — свой код (`apic_send_init_ipi`, `apic_send_startup_ipi`).
- **Приём.** `apic_ipi_sched_intr` → `lapic_intr` → `context_stop` (BKL,
  `smp_sched_handler()` — задачи из слота) → `smp_ipi_sched_handler()`:
  вытесняет текущий процесс, только если он вытесняемый и в очередях CPU есть
  более приоритетный (`sched_ready_above()`). **EOI один** — в
  `LAPIC_INTR_HANDLER` (`apic_asm.S`); у IPI останова — в `stop_local_timer()`
  (обработчик не возвращается).
- **Тик.** LAPIC-таймер периодический (`lapic_set_timer_periodic(system_hz)`
  на BSP и AP), `restart_local_timer()` пуст; на тике
  `sched_starve_check()` вытесняет процесс, если на CPU ждёт более
  приоритетный (запасной путь для случая без IPI). `lapic_microsec_sleep()`
  после калибровки ждёт по TSC, чтобы запуск AP не сбил периодический режим.
- **FPU.** `save_fpu()` для процесса другого CPU шлёт синхронный IPI, только
  если его FPU-состояние может быть в регистрах того CPU.
- **Простой.** `idlepoll=<мкс>` (на SMP по умолчанию 50, `idlepoll=0` —
  выключить; при одном CPU не опрашивает): перед `hlt` CPU с включёнными
  прерываниями опрашивает cpu-local `need_resched`; `smp_kick_idle()` пишет
  его и шлёт IPI, только если цель не в состоянии `IDLE_POLLING`
  (`idle_poll()`, `proc.c`; запись флага / чтение состояния и запись
  `IDLE_HALTED` / чтение флага разделены полными барьерами — пробуждение не
  теряется). Под KVM это главный выигрыш: тест 73 на 4 CPU — 79 → 20 с.
  Цена — сожжённое время CPU в простое; на железе лучше MONITOR/MWAIT (x86),
  WFE/SEV (ARM).
- **Кэш-строки.** `sched_ipi_data[]`, `bkl_stats[]` и `__cpu_local_vars`
  (шаг 0x340) — по строке на CPU.

### 3.10 Прочее

| Файл | Функции | Назначение |
|---|---|---|
| `arch_reset.c` | `reset` (KBC, порт `0xCF9`, порт `0x92`), `halt`, `poweroff` (ACPI, эмуляторы), `arch_shutdown`, `ser_putc` | перезагрузка/выключение, отладочный вывод в COM |
| `arch_watchdog.c` | `arch_watchdog_init/stop/lockup`, `i386_watchdog_start`, `intel_*`/`amd_*` | NMI-watchdog и NMI-профилирование на счётчиках производительности Intel/AMD |
| `breakpoints.c`, `debugreg.S` | `breakpoint_set` | аппаратные точки останова |
| `do_iopenable.c` | `do_iopenable` | `SYS_IOPENABLE`: разрешить процессу `in/out` (IOPL) |
| `do_readbios.c` | `do_readbios` | `SYS_READBIOS`: чтение области BIOS |
| `do_sdevio.c` | `do_sdevio` | `SYS_SDEVIO`: строковый порт-ввод/вывод |
| `direct_tty_utils.c` | `direct_cls`, `direct_print(_char)`, `direct_put_char`, `direct_read_char` | ранний/аварийный вывод в VGA-текст |
| `oxpcie.c` | `oxpcie_set_vaddr`, `oxpcie_putc`, `oxpcie_in` | консоль ядра на PCIe-UART |

Общий код ядра с ветвлениями под i386: `minix/kernel/system.c` регистрирует
`SYS_DEVIO`, `SYS_VDEVIO`, `SYS_READBIOS`, `SYS_IOPENABLE`, `SYS_SDEVIO` только
`#if defined(__i386__)`; `proc.c` проверяет `p_seg.p_cr3`; `main.c` — ветки
`USE_APIC`/`CONFIG_SMP`.

## 4. VM-сервер (i386)

- `minix/servers/vm/arch/i386/pagetable.h` — только макросы: `PTF_WRITE/READ/
  PRESENT/USER/GLOBAL/NOCACHE`, `ARCH_VM_DIR_ENTRIES` (1024), `ARCH_BIG_PAGE_SIZE`
  (4 МБ), `ARCH_VM_PDE/PTE`, `PFERR_NOPAGE/PROT/WRITE/READ`, `VM_PAGE_SIZE` (4 КБ).
- Реальная архитектурная логика — **в общем `minix/servers/vm/pagetable.c`**
  под `#if defined(__i386__)`: использование глобального бита (`global_bit`,
  `_CPUF_I386_PGE`) и больших страниц (`_CPUF_I386_PSE`) в `pt_init`, формирование
  PDE в `pt_ptalloc`/`pt_allocate_kernel_mapped_pagetables`, маскирование
  `ACC/DIRTY` при сравнении PTE в `pt_writemap`, отладочная печать флагов
  (`ptestr`). Структура `pt_t` (`pt.h`) — двухуровневая, на `u32_t`.
- VM строит таблицы страниц в своей памяти и передаёт ядру корень через
  `VMCTL_SETADDRSPACE` (CR3); выборочно инвалидирует TLB через
  `VMCTL_I386_INVLPG`.

## 5. Сводная таблица возможностей

Статус: ✅ реализовано · ⚠️ частично / с оговорками · ❌ нет.

| Возможность | Статус | Реализация / что сделать |
|---|---|---|
| Разрядность | ⚠️ | только 32 бита; 64 бита — это отдельный порт amd64 |
| Загрузка | ✅ | multiboot (MINIX boot, GRUB/EFI); **заменяется на U-Boot + fitImage** (`docs/modernization.md` п. 6) |
| Карта памяти от загрузчика | ✅ | multiboot mmap → `kinfo_t` |
| MMU: 4 КБ страницы, 2 уровня | ✅ | `pg_utils.c`, `memory.c`, VM `pagetable.c` |
| Большие страницы (4 МБ, PSE) | ⚠️ | только для отображения ядра/физических окон; VM для пользователей не использует |
| Глобальные страницы (PGE) | ✅ | для отображений ядра |
| PAE (>4 ГБ физической памяти) | ❌ | `MKPAE` лишь проверяет CPUID; структуры на `u32_t`. Смысла доделывать нет — решается портом amd64 |
| NX (запрет исполнения) | ❌ | требует PAE/amd64 |
| Выборочная инвалидация TLB | ✅ | `INVLPG` (`VMCTL_I386_INVLPG`) |
| PCID (идентификаторы контекстов TLB) | ❌ | на i386 недоступен; реализовать в amd64 (п. 5 модернизации) |
| Входы в ядро | ✅ | `int`, `SYSENTER`, `SYSCALL` — выбор при загрузке |
| Исключения, page fault | ✅ | `exception.c` |
| Контроллер прерываний | ✅ | 8259 PIC; LAPIC + IOAPIC (`USE_APIC`) |
| ACPI | ⚠️ | в ядре — только таблицы (RSDP/RSDT/MADT) и poweroff; AML-интерпретатор (ACPICA) — в пользовательском драйвере `minix/drivers/power/acpi` |
| Системный таймер | ✅ | PIT 8253 или LAPIC-таймер (калибровка) |
| Счётчик циклов | ✅ | TSC |
| Tickless-режим | ❌ | LAPIC-таймер периодический, 1/HZ (`init_local_timer`, с 2026-10-05; раньше one-shot с перевзводом на каждом выходе в пользователя), тик идёт и в простое |
| FPU/SSE | ✅ | ленивое сохранение, FXSAVE/FNSAVE |
| AVX/XSAVE | ❌ | добавить (актуально для amd64) |
| SMP | ⚠️ | есть, но только при `CONFIG_SMP`, Big Kernel Lock, миграции нет — **переделка по п. 4 модернизации** |
| Спинлоки | ✅ | MCS (2026-10-04): `arch_spinlock.h` на `<stdatomic.h>`, узел на CPU в самом замке (576 байт при `CONFIG_MAX_CPUS=8`, всё по 64 байта); прежний test-and-set на ассемблере из `klib.S` удалён. Проверки повторного захвата — TODO в `arch_spinlock_lock/unlock` |
| TLS-регистр потока | ❌ | `%gs`-сегмент на поток — п. 8 модернизации (`docs/threads.md`) |
| NMI-watchdog | ✅ | Intel/AMD perf-счётчики |
| Профилирование | ✅ | CMOS RTC / NMI |
| Аппаратные точки останова | ✅ | DR0–DR7 |
| Порт-ввод/вывод для драйверов | ✅ | `SYS_DEVIO`, `SYS_VDEVIO`, `SYS_SDEVIO`, `SYS_IOPENABLE` |
| Консоль ядра | ✅ | VGA-текст, COM (8250), OXPCIe |
| Reset / poweroff | ✅ | KBC, `0xCF9`, `0x92`, ACPI |
| Абстрактные таблицы страниц | ❌ | п. 3 модернизации |
| `kyield` / `kmutex` | ❌ | п. 1–2 модернизации |

## 6. Замечания для модернизации и порта amd64

- Архитектурные ветвления живут не только в `arch/i386/`: `minix/kernel/system.c`,
  `proc.c`, `main.c` и особенно VM `pagetable.c` — их вынос в архитектурный слой
  входит в п. 3 модернизации.
- Ядро само обходит аппаратные таблицы страниц (`vm_lookup`, `umap_virtual`,
  `createpde`) — при переходе на абстрактные таблицы страниц это место
  переопределяется.
- Окна копирования на 4-МБ PDE (`freepdes`) — приём, специфичный для 32-битного
  x86; на amd64 естественнее прямое отображение всей физической памяти в ядре.
- Набор SMP/APIC/ACPI — основной переиспользуемый актив для amd64.
