# Handoff — снимок состояния

> Этот файл перезаписывается каждую сессию. Не храните здесь архитектурные решения —
> они идут в `docs/architecture.md` / `docs/porting.md` / `docs/modernization.md` /
> `docs/memory.md` / `docs/threads.md` / `docs/testing.md` / `docs/build-*.md` /
> `docs/docker-build.md` / `docs/types-audit.md` / `docs/messages.md`.

## Дата и контекст

2026-10-04. Начато направление А (шаг А1), но сессия ушла в SMP: владелец
переписал спинлоки (MCS), сделаны сопутствующие изменения тестовой среды и
загрузки SMP. Всё закоммичено и запушено. **А1 не начат:** предложение по типу
физического адреса (ниже) ждёт подтверждения владельца.

## Сделано в сессии

- **Время в итоге `test-i386`** (`docker/run-tests.sh`): строки консоли
  помечаются временем хоста (`<журнал>.times`), в итоге `run time` (весь QEMU) и
  `tests time` (от `REMINIX-TESTS-BEGIN` до `END`), длительность каждого теста —
  `<журнал>.durations` (`testing.md` §1).
- **`BKL_DEBUG`** (`build.mk`, `CONFIG_BKL_DEBUG`): отладочная обёртка BKL
  (`bkl_lock/bkl_unlock`, `noinline`, журнал для `bkl.txt`) только по запросу;
  при переключении `build`/`hdimage`/`quick` сами удаляют объекты ядра
  (отметка `obj/<ARCH>-smp/.bkl_debug`).
- **BSP — всегда логический CPU 0** на всех архитектурах (решение владельца,
  `modernization.md` п. 4 «Нумерация CPU»): `discover_cpus()` берёт APIC ID BSP
  из CPUID до разбора MADT, `smp_init()` сверяет с LAPIC ID и паникует при
  расхождении. `.balign K_STACK_SIZE` перед `k_initial_stack` (владелец):
  `cpuid` на стеке загрузки гарантированно читает 0 из `head.S`.
- **Спинлоки MCS** (владелец): `minix/kernel/spinlock.h` — прослойка макросов
  (без SMP — пустые операторы и константы), `arch/i386/arch_spinlock.h` — MCS на
  `<stdatomic.h>`, узлы на CPU внутри замка, всё по 64 байта (проверено снимком
  памяти); ассемблерный test-and-set из `klib.S` удалён. Проверки повторного
  захвата — TODO в `arch_spinlock_lock/unlock` (нужно соглашение `locked` 0/1/2
  и сброс узла в `unlock`).

## Итог прогонов

- MCS, SMP 4 CPU, полный прогон — **103/103**, `tests time` 3993,7 с; контроль
  (прежний test-and-set, остальное то же) — **103/103**, 3937,0 с. Разбор по
  тестам — `testing.md` §2 «Сравнение спинлоков»: 79 −36 %, 70 +67 %, **тест 4 —
  346 с вместо 0,6 с (один раз, открыто)**.
- MCS, обычное ядро и SMP 4 CPU, `TESTS=1,2,95,fdt` — PASS; после правки
  макросов и TODO оба ядра i386 собираются (код SMP не менялся).
- earm: полная сборка `sdimage BOARD=beaglebone` — успешно, `checkflist` чист.

## Доступ к среде

- Ubuntu, AMD Ryzen 5 5600G, KVM без AVIC; проект —
  `/media/dmironov/1fe3f4f3-4210-4da4-b212-3c442d01f36e/reminix`.
- Долгие прогоны — отвязанно от сессии:
  `(setsid nohup make -o image -C docker -f build.mk test-i386 … > obj/test-logs/run-….out 2>&1 < /dev/null &)`.
- `HANG_IDLE`: полный прогон на 4 CPU — `HANG_IDLE=3000 TEST_TIMEOUT=9000`, на 2 CPU — 1800,
  `TESTS=41` — не меньше 200.
- Время набора и каждого теста — `tests time` в итоге и `<журнал>.durations`;
  сравнивать прогоны только при одинаковом числе CPU и без сборки на хосте.
- Сборка параллельно с прогоном: `JOBS=6` и `renice -n 10` контейнеру сборки.
  Контейнер сборки — тот, чья команда содержит `x86_hdimage`/`arm_sdimage`; не
  перепутать с тестовым (`run-tests.sh`).
- Ждать окончания прогона — по строке `RESULT:` в `run-….out`; `pgrep -f` /
  `pkill -f` с шаблоном находят и сам ожидающий цикл.
- `scan64.py` нужны собранные `obj/i386` и `obj/i386-smp` и clang ≥ 18 на хосте
  (в Docker-образе clang нет).
- Коммит и push — только по команде владельца; без строк соавторства.
- `…/reminix(тупик)` — тупиковая ветка, только чтение.


## Текущий статус

- i386: обычное ядро 1 CPU и SMP 4 CPU — 103/103 (с `fdt`), спинлоки MCS.
- **Открыто:** тест 4 на 4 CPU с MCS (`testing.md` §2); §5.4 — медленный
  межпроцессорный IPC; §5.7 — паника `uds` (не воспроизводится).
- Отладочный код, оставленный сознательно: `bkl_lock/bkl_unlock` (только при
  `BKL_DEBUG=yes`), `dbg_cpu_events`, `dbg_boot_event`, `tickdebug`,
  диагностика в `test41.c`.
- **Ждёт решения владельца — тип физического адреса для А1** (`types-audit.md`
  §9.1, `memory.md` §10 в. 7). Предложено: `phys_bytes` → `uint64_t` на всех
  архитектурах (фиксированная ширина на границе ядро–VM–сообщения), допустимый
  диапазон — ограничение платформы (`ARCH_PHYS_MAX`, i386 без PAE — 4 ГБ),
  линейные адреса ядра под именем `phys_bytes` (`phys_copy`, `phys_memset`,
  `createpde`, `lin_lin_copy`, …) → `vir_bytes` (ассемблер берёт 32-битные
  аргументы), длины операций → `size_t`/`vir_bytes`, длины физ. диапазонов —
  64 бита; `phys_clicks` 32 бита до `pfn_t` в А3; MMIO и RAM — один тип;
  `bus_addr_t` — при пересмотре драйверов; имя `phys_bytes` оставить. Ломается
  `at_wini` `struct prdte` (поле → `u32_t`). До М4 остаются формат журнала АПТ,
  PTE и отдельный тип «адрес RAM».
- §9.2 (раскладка сообщений на 64 битах) — тоже ждёт решения.

## Отложенные исправления (решение владельца 2026-10-02: «на потом»)

Найдены в этапе 0.2, **не исправлены**; подробности — `docs/messages.md` §12 и
`docs/types-audit.md` §8 (последний абзац). Каждое — отдельным коммитом с
проверкой `host-test` + `test-i386`.

1. **`do_vsafecopy`** (`minix/kernel/system/do_safecopy.c:399-419`) — нет проверки
   `0 <= els <= SCPVEC_NR` перед копированием в статический `vec[SCPVEC_NR]`:
   переполнение буфера ядра по запросу процесса с правом `SYS_VSAFECOPY`.
   **Самое срочное.**
2. **`TTY_FKEY_CONTROL`** — libsys (`minix/lib/libsys/fkey_ctl.c:24-25`) читает
   ответ как `mess_tty_lsys_fkey_ctl` (смещения 0/4), TTY пишет в поля запроса
   (смещения 4/8): вызывающий получает код запроса вместо маски.
3. `minix/fs/ptyfs/ptyfs.c:59` — `snprintf(name, sizeof(name), …)` (размер
   указателя вместо `size`).
4. `minix/servers/vfs/mount.c:113` — копируется `sizeof(mount_label)` вместо
   `label_len`.
5. Протоколы «на совпадении смещений»: `BUSC_PCI_RESCAN` (`m1_i1`/`m2_i1`),
   `IOMMU_MAP` (`m2_*`/`m1_*`), `VM_RS_CTL_LEN` (`int`-поле, запись через
   `size_t *`), `RS_UPDATE` (`m_rs_req.addr` поверх `m_rs_update`).
6. Сообщения не обнуляются перед отправкой (`vfs/request.c`, `fwd_msg()`,
   `do_reboot()` `RTCDEV_PWR_OFF`, `sched_nice()`, PM `SIGS_SIGNAL_RECEIVED`) —
   мусор стека уходит другому процессу.
7. Дешёвые 64-битные правки, не зависящие от решений: `u64.h` (`ex64lo` →
   `uint32_t`), `libfsdriver/call.c:241` (`data.ptr = buf`), `iovec_s_t`/`iovec_t`
   libbdev↔libblockdriver, `int`↔`ssize_t` в прототипах (ext2, libfsdriver,
   memory/fbd/vnd/mmcblk, ipc, getdents), `kinfo.vm_allocated_bytes` (`int`).
8. Мелочи из `messages.md` §12: `do_irqctl.c:166` (`1 << notify_id`),
   `SVMCTL_MRG_EP2/ADDR2` не заполняются ядром, `req_lookup` `path_size`,
   `req_peek` паника при позиции ≥ 4 ГБ, обрезка размеров в `sys_umap`/`sys_sprof`.

## Замечено в коде (для будущей работы)

64-битные и >4 ГБ-проблемы — `docs/types-audit.md`; протоколы сообщений —
`docs/messages.md`. Не связанные с типами:
- `minix/lib/libmthread/pthread_compat.c`: `pthread_mutex_trylock` рекурсивно
  вызывает сам себя.
- SMP i386: IOAPIC направляет все IRQ на BSP; IPI планировщика EOI-ится дважды
  (`ipi_ack()` и макрос `lapic_intr`); `stop_local_timer()` делает лишний
  `apic_eoi()` (теперь — только при останове CPU).
- `pt_writemap` (VM) ставит и снимает `VMINHIBIT` на каждый вызов (`FIXME`).
- `switch_address_space_idle()`: AP в простое сидит в таблице страниц VM.
- `arch_enable_paging()` вызывается при загрузке трижды.
- MIB спрашивает данные для всех `CONFIG_MAX_CPUS`; `mib.h` берёт
  `CONFIG_MAX_CPUS` из заголовка i386 (на earm — 1).
- `quick` всегда перелинковывает драйвер `memory` (безвредно).
- dec21140A: `sys_umap` берёт физ. адрес поля-указателя, а не дескриптора
  (`dec21140A.c:343`); DMA-буферы в BSS.
- VFS: стек рабочего потока 28 КБ — замерить запас (на LP64 кадры растут).
- usb: копирование `usb_urb` «с `&dev_id`» хрупко на любом ABI.
- Кэш-строки SMP (снимок 2026-10-04): счётчики «по CPU» `bkl_ticks`,
  `bkl_tries`, `bkl_succ`, `kernel_ticks`, `dbg_cpu_events` — массивы в одной
  строке, их пишет каждый CPU на каждом входе в ядро (ложное разделение);
  `__cpu_local_vars` не выровнен по 64 (адрес % 64 = 32, границы CPU делят
  строку); `sched_ipi_data[]` — все CPU в одной строке; `smp_cpu_lock`,
  `dispq_lock` не используются (по 576 байт).
- `cpuid` (`arch_smp.h:11`) — l-value: `cpuid = …` скомпилируется.
- `smp_reinit_vars()` не обнуляет `bsp_cpu_id` (теперь безвредно: BSP = 0 по
  построению); если `discover_cpus()` ничего не нашёл, CPU 0 до конца работы
  входит в ядро на `k_boot_stack` (`sp0` из `prot_init()`).
- NMI: без `watchdog=1` любое NMI молча выбрасывается (`nmi_watchdog_handler`);
  в `struct nmi_frame` (`arch_watchdog.h:6`) общие регистры в обратном порядке
  относительно `pusha` (отчёт «KERNEL LOCK UP» путает eax↔edi и т. д.);
  `lockup_check()` на SMP с общими `static`.
- Калибровка i8253 в запасном режиме: обработчик отпускает BKL, взятый
  прерыванием в ядре; любое другое прерывание в это время дало бы повторный
  захват BKL (сейчас разрешён только IRQ0).
- SMP: `CLOCK_MONOTONIC` в одном процессе вернул значение чуть меньше прежнего
  (test95 на 4 CPU напечатал «-0.00 s», меньше тика) — не исследовано.


## Следующие шаги

1. Ждать указаний владельца; решение по типу физ. адреса → план А1.
2. Тест 4 на 4 CPU с MCS: серия `TESTS=4`, при повторе — снимок.
3. Спинлоки: проверки повторного захвата (TODO), счётчики «по CPU» в
   `__cpu_local_vars` и его выравнивание по 64.
4. Направление Б — Б1 (решения по `modernization.md` вопросы 1–2, проверка 3).
5. Отложенные исправления — когда владелец скажет; первым — `do_vsafecopy`.
