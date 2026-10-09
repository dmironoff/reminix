# Handoff — снимок состояния

> Этот файл перезаписывается каждую сессию. Не храните здесь архитектурные решения —
> они идут в `docs/architecture.md` / `docs/porting.md` / `docs/modernization.md` /
> `docs/memory.md` / `docs/threads.md` / `docs/testing.md` / `docs/build-*.md` /
> `docs/docker-build.md` / `docs/types-audit.md` / `docs/messages.md`.
> **Текущий план работ — `docs/work-plan.md`** (решения владельца 2026-10-07, порядок,
> правила проверки); выполненное отмечается там.

## Дата и контекст

2026-10-09. Шаг А1.0 (`work-plan.md` §5) внесён, проверен, закоммичен и
запушен (`3218bdcf9`; `scan64.py` a16 и документы — следующим коммитом).
Подготовлен инструмент для А1.1.
Предыдущая сессия (2026-10-08/09): §4.1, §4.2 (пакет `batch1`), §8 — всё
закоммичено и запушено, последний коммит `a2abfc479`.

## Сделано в сессии

- **А1.0** (решения владельца по пп. 1–6 предложения, 2026-10-09):
  - `minix/include/minix/memtypes.h` (новый): `phys_addr_t` = `phys_bytes`,
    `vir_addr_t` = `vir_bytes`, `dma_addr_t` = `phys_addr_t`, `pfn_t` =
    `phys_clicks`; `PRIxPHYS`/`PRIxVIR` = `"lx"`, `PRIxDMA` = `PRIxPHYS`
    (строками, без `<inttypes.h>`); `static inline phys_to_dma(pa)` —
    тождество, без аргумента устройства (добавить, когда появится потребитель —
    `dma-ranges`/IOMMU); `#ifdef CONFIG_PHYS_ADDR_64` → `#error` до А1.8.
  - Включается из `minix/type.h` сразу после старых typedef (ядро, VM, libsys,
    серверы, драйверы получают его без новых `#include`); сам включает
    `<minix/type.h>`, поэтому работает и при прямом включении.
  - `INCS` в `minix/include/minix/Makefile`, `distrib/sets/lists/minix-comp/mi`.
  - Проверка (`obj/test-logs/check-A1.0.summary`, сборка с нуля, тулчейн
    сохранён): `hdimage SMP=no` 603 с, `SMP=yes` 597 с, `sdimage BOARD=beaglebone`
    388 с; `disasm.sh compare --funcs batch1 A1.0`: 240 из 242 совпадают в обоих
    вариантах, `memory` (времена файлов RAM-диска) и `usb_hub`
    (`__DATE__`/`__TIME__`) — «функции совпадают». Снимок `obj/disasm/A1.0` —
    эталон для А1.1.
- **`obj/test-logs/check-chain.sh`** (не в git): `BUILD_ONLY=yes` — остановиться
  после `disasm compare`; `BASE=<метка>` — эталон (по умолчанию `fc5cf831c`),
  сравнение с `--funcs`.
- **`scan64.py`**: режим `a16` (определения А1.6 на i386: `vir_bytes`,
  `phys_bytes` = `unsigned int`, `PRIxVIR`/`PRIxPHYS` = `"x"`), в p64
  `PRIxPHYS` = `"llx"` (`types-audit.md` §2.1). Прогон `--modes m32,p64,a16
  --no-layouts` → `obj/abi64-scan-A1.0`: a16 — 60 новых `-Wformat` в 22 файлах
  (`new-a16.txt`), p64 без изменений (154, 21 `-Wformat`).
- `work-plan.md` §5 — А1.0 отмечен; `types-audit.md` §2.1 — режим a16 и итог.
- Срыв первой попытки цепочки (по вине агента): `CLEAN=yes` запущен, когда шла
  консоль владельца `run-i386 SMP=yes CPUS=4` на `obj/i386-smp`; `clean`
  удалил её образ. Владелец завершил консоль и остановил осиротевшую сборку;
  цепочка перезапущена по его команде. Журнал консоли цел
  (`i386-smp-cpu4-console-20261008-161532.*`).

## Подготовка А1.1

Список мест — `obj/abi64-scan-A1.0/new-a16.txt` (`-Wformat`), 60 мест:
ядро — `proc.c` (7), `arch/i386/memory.c` (5), `apic.c` (2); VM — `region.c`
(8), `pagefaults.c` (3), `mem_cache.c`, `mmap.c`, `pagetable.c` (по 2),
`mem_shared.c`; серверы — `is/dmp_vm.c` (2), `rs/exec.c`, `ipc/shm.c`;
procfs `pid.c` (3); драйверы — `virtio_blk` (5), `amddev` (4), `ahci` (3),
`at_wini` (2), `dp8390` `3c503.c`/`wdeth.c` (по 2), `dpeth` `3c503.c`/`wd.c`,
`dec21140A`. Все — `%l*` для `vir_bytes`/`phys_bytes` (или выражений с ними).
Вне скана (вручную, `grep '%[-#0-9]*l[xXud]'` по каталогу): earm (около 13
строк `%l*` в `kernel/arch/earm` и драйверах earm), код под `#if SANITYCHECKS`
и прочими отладочными `#if`. Критерий готовности А1.1: дизассемблер совпадает
с `A1.0`, в a16 и p64 новых `-Wformat` нет (кроме не-адресных, если найдутся —
разобрать). `%x` с явным `(unsigned)`/`(u32_t)`-приведением адреса скан не
видит — это класс «приведения адреса к `u32_t`» (§9а), не форматы; в А1.1
только отмечать.

На i386 `uintptr_t` — `unsigned int` (`PRIxPTR "x"`), на earm — `unsigned long`
(`"lx"`): в А1.6 `PRIxVIR` = `PRIxPTR`.

## Открыто

- **Остановка стенда при случайном стрессе** (`testing.md` §5.9): после
  нехватки памяти VM (тест 64) `rc` получил ENOENT на `/dev/null` и прогон
  исчез. Повтор: `TEST_RANDOM=896425` с теми же параметрами.
- Подтесты 6 и 21 теста 90 под `TEST_JOBS` падают из-за задержек самого теста.
- Через CPU по-прежнему идут вызовы пользователей к серверам (`pm ↔ user`,
  `vfs ↔ user`): балансировщик SCHED — после А1 и MSG.
- B4 (x2APIC) — отложен. Вопросы ARM 1–3 — после А1 и MSG.
- Справка `build.mk` про `HANG_IDLE` (4 CPU — 3000, «тест 79 молчит до ~40 мин»)
  устарела: после IPI тест 79 идёт ~90 с, самый долгий тест (91) — ~220 с.
  Умолчание 600 с, вероятно, уже хватает и на 4 CPU — не проверялось.
- Отладочный код, оставленный сознательно: `bkl_lock/bkl_unlock` (только при
  `BKL_DEBUG=yes`), `dbg_cpu_events`, `dbg_boot_event`, `tickdebug`,
  `dbg_ipc_*` (`ipcstat=1`), диагностика в `test41.c`.

## Доступ к среде

- Ubuntu, AMD Ryzen 5 5600G, KVM без AVIC; проект —
  `/media/dmironov/1fe3f4f3-4210-4da4-b212-3c442d01f36e/reminix`.
- Долгие прогоны — отвязанно от сессии:
  `(setsid nohup make -o image -C docker -f build.mk test-i386 … > obj/test-logs/run-….out 2>&1 < /dev/null &)`.
  Полная проверка подшага — `obj/test-logs/check-chain.sh <метка>`
  (`CLEAN=yes` — сборка без старых объектов, тулчейн сохраняется); остановить —
  `kill -TERM -<pgid>` цепочки и `docker kill` контейнера сборки.
- С 2026-10-08 `SMP=yes` по умолчанию на i386: однопроцессорное ядро — явно
  `SMP=no` (`hdimage`, `test-i386`, `quick`).
- Сравнение машинного кода: `docker/disasm.sh snapshot <метка>` после полных
  `hdimage` обоих вариантов, затем `docker/disasm.sh compare <до> <после>`.
- `HANG_IDLE`: полный прогон на 4 CPU — `HANG_IDLE=3000 TEST_TIMEOUT=9000`, на 1–2 CPU — 1800,
  `TESTS=41` — не меньше 200 (см. «Открыто» — вероятно, завышено).
- Время набора и каждого теста — `tests time` в итоге и `<журнал>.durations`;
  сравнивать прогоны только при одинаковом числе CPU и без сборки на хосте.
- Сборка параллельно с прогоном: `JOBS=6` и `renice -n 10` контейнеру сборки.
  Контейнер сборки — тот, чья команда содержит `x86_hdimage`/`arm_sdimage`; не
  перепутать с тестовым (`run-tests.sh`).
- Ждать окончания прогона — по строке `RESULT:` в `run-….out`; `pgrep -f` /
  `pkill -f` с шаблоном находят и сам ожидающий цикл (и убивают его).
- `quick` не ставит заголовки в `destdir` (в NetBSD это цель `includes`):
  после правки `minix/include` — `nbmake-i386 -C minix/include includes` в
  контейнере или полный `hdimage`; `/etc/system.conf` и `minix/commands` —
  тоже только `hdimage` (или `QUICK_DIRS` с `minix/commands`).
- После правки `minix/include` объекты, собранные со старым заголовком, make
  может не пересобрать (`install` сохраняет время файла): 2026-10-05 RS с
  устаревшим `request.o` затирал свой `rp` на стеке. Удалить объекты и
  пересобрать (`check-chain.sh` с `CLEAN=yes`).
- `scan64.py` нужны собранные `obj/i386` и `obj/i386-smp` и clang ≥ 18 на хосте
  (в Docker-образе clang нет); итог — `obj/abi64-scan/summary.txt`.
- Коммит и push — только по команде владельца; без строк соавторства.
- **Перед `CLEAN=yes` — `docker ps`:** нет ли `run-*`/`test-*` владельца на тех
  же деревьях `obj/` (2026-10-09 `clean` удалил образ работающей консоли).
- Проверка подшага без изменения кода:
  `CLEAN=yes BUILD_ONLY=yes BASE=<эталон> obj/test-logs/check-chain.sh <метка>`
  через `setsid nohup`; ~27 мин.
- Ожидающий цикл не должен искать `pgrep -f` по строке, входящей в его же
  команду (находит сам себя и не кончается) — ждать по файлу итога.
- `…/reminix(тупик)` — тупиковая ветка, только чтение.
- Снимок в конце набора: `run-tests.sh` сам его не делает (гость выключается),
  наблюдатель по `REMINIX-TESTS-END` в журнале шлёт `test-snapshot`.
- earm: перед сборкой ядра вне `build.sh` — `nbmake-evbearm-el -C minix/include includes`.
- earm, проверка сборки одного каталога без `sdimage`:
  `docker run --rm -u $(id -u):$(id -g) -v $PWD:/work/reminix -w /work/reminix reminix-build:latest obj/evbearm-el/tooldir/bin/nbmake-evbearm-el -C <каталог> dependall`.
- `quick` не ставит `etc/rc.d/*`, а `*.conf` служб попадают в образ только
  при свежем исходнике (`docker-build.md` §4): копировать в
  `obj/<arch>[-smp]/work/fs` вручную, затем `quick`.

## Отложенные исправления

Нумерация — исходная (этап 0.2), порядок и сроки — `work-plan.md` §4.2.

| № | Что | Состояние |
|---|---|---|
| 1 | `do_vsafecopy`: нет проверки `0 <= els <= SCPVEC_NR` — **безопасность** | исправлено 2026-10-09 |
| 9 | `do_memset()` теряет `VMSUSPEND` | исправлено 2026-10-09 |
| 3 | `ptyfs.c:59` `sizeof(name)` | исправлено 2026-10-09 |
| 4 | `vfs/mount.c:113` `sizeof(mount_label)` | исправлено 2026-10-09 |
| 8 (часть) | `do_irqctl.c:166` `1 << notify_id` | исправлено 2026-10-09 |
| 10 | `pthread_mutex_trylock` вызывает сам себя | исправлено 2026-10-09 |
| 7 (часть) | `u64.h`, `int`↔`ssize_t`, `kinfo.vm_allocated_bytes` | исправлено 2026-10-09 |
| 7 (часть) | `libfsdriver/call.c:241`, `iovec_s_t`/`iovec_t` | в А1.4 |
| 8 (часть) | `SVMCTL_MRG_EP2/ADDR2`, обрезка в `sys_umap`/`sys_sprof` | в А1.3 |
| 8 (часть) | `req_lookup` `path_size`, `req_peek` ≥ 4 ГБ | в MSG.4 |
| 2 | `TTY_FKEY_CONTROL` (ответ читается не теми полями) | в MSG.8 |
| 5 | `BUSC_PCI_RESCAN`, `IOMMU_MAP`, `VM_RS_CTL_LEN`, `RS_UPDATE` | в MSG |
| 6 | сообщения не обнуляются | в MSG (правило 7 §14а) |

Найдено в сессии, не исправлялось (кандидаты в таблицу, решение владельца):
- `vfs/dmap.c` `do_mapdriver`: при `label_len == 0` читается `label[-1]`
  (отправитель — только RS).
- `fbd.c:435` `rule_post_hook(osize, &r)`: `r` — `ssize_t`, хук принимает
  `int *` (на LP64 пишет половину; `types-audit.md` §11.4).
- `kinfo.kernel_allocated_bytes(_dynamic)` — тоже `int` (`param.h:45-46`); в
  №7 переведён только `vm_allocated_bytes`, как в плане.

## Замечено в коде (для будущей работы)

64-битные и >4 ГБ-проблемы — `docs/types-audit.md`; протоколы сообщений —
`docs/messages.md`. Не связанные с типами:
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
- Кэш-строки SMP: `dbg_cpu_events` (шаг 20 байт) делит строки между CPU;
  счётчики `dbg_ipc_*` при `ipcstat=1` пишутся всеми CPU; `smp_cpu_lock`,
  `dispq_lock` не используются (по 576 байт).
- SMP: `enqueue()` пишет время входа в очередь текущему процессу
  (`proc_ptr->p_accounting.enter_queue`), а не `rp` (исходный MINIX).
- `idlepoll` на тесте 4: 0,3–0,7 с против 0,1–0,2 с без опроса — не разбиралось.
- `cpuid` (`arch_smp.h:11`) — l-value: `cpuid = …` скомпилируется.
- `smp_reinit_vars()` не обнуляет `bsp_cpu_id` (безвредно: BSP = 0); если
  `discover_cpus()` ничего не нашёл, CPU 0 входит в ядро на `k_boot_stack`.
- NMI: без `watchdog=1` любое NMI молча выбрасывается; в `struct nmi_frame`
  (`arch_watchdog.h:6`) регистры в обратном порядке относительно `pusha`;
  `lockup_check()` на SMP с общими `static`.
- Калибровка i8253 в запасном режиме: обработчик отпускает BKL, взятый
  прерыванием в ядре (сейчас разрешён только IRQ0).
- SMP: `CLOCK_MONOTONIC` в одном процессе вернул значение чуть меньше прежнего
  (test95 на 4 CPU, «-0.00 s») — не исследовано.
- `TIME_BLOCK`/`PRINT_STATS` (`minix/timers.h`) не используются и ссылаются на
  несуществующий `add64`.

## Следующие шаги

1. А1.1 — предложить владельцу порядок (по каталогам: ядро, VM, libsys,
   серверы, драйверы; коммит на каталог или один), внести, проверить
   `disasm.sh compare --funcs A1.0 A1.1` + `scan64.py --modes m32,p64,a16`.
2. Дальше — `work-plan.md` §5: А1.2а …
