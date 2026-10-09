# Handoff — снимок состояния

> Этот файл перезаписывается каждую сессию. Не храните здесь архитектурные решения —
> они идут в `docs/architecture.md` / `docs/porting.md` / `docs/modernization.md` /
> `docs/memory.md` / `docs/threads.md` / `docs/testing.md` / `docs/build-*.md` /
> `docs/docker-build.md` / `docs/types-audit.md` / `docs/messages.md`.
> **Текущий план работ — `docs/work-plan.md`** (решения владельца 2026-10-07, порядок,
> правила проверки); выполненное отмечается там.

## Дата и контекст

2026-10-09 (вторая сессия). Шаг А1.1 (`work-plan.md` §5) внесён, проверен,
закоммичен и запушен: А1.1 — `8c457d929`, `scan64.py` — `6e8238c81`,
документы — следующим коммитом. Предыдущее: А1.0 — `3218bdcf9`,
`scan64.py` a16 и документы — `83b9e2f1f` (запушены).

## Сделано в сессии

- **А1.1** — адреса в форматах печати → `PRIxVIR`/`PRIxPHYS`/`PRIxDMA`, 63
  строки (список и решения владельца — `work-plan.md` §5). Правки без изменения
  числа строк (`__LINE__`). Проверено: строки форматов после подстановки
  макросов совпадают с прежними, кроме согласованных `%lX` → `%lx`.
- `minix/memtypes.h`: `PRIuPHYS` (`"lu"`).
- `scan64.py`: атрибут `__format__` у `printf` из `minix/sysutil.h` во всех
  режимах; `PRIuPHYS` в p64 (`"llu"`) и a16 (`"u"`) (`types-audit.md` §2.1).
- Проверка: `obj/test-logs/check-A1.1.summary` (сборка с нуля, все rc=0, compare
  rc=1 — ожидаемо: `memory`, `usb_hub`, `dp8390`, `dpeth` только данные);
  `do_safecopy.c:241` — после цепочки, `quick` ядра + снимок `obj/disasm/A1.1b`
  (ядро = `A1.0`), earm `minix/kernel` собран. Скан — `obj/abi64-scan-A1.1`.
- Коммиты (по команде владельца): А1.1 — исходники + `memtypes.h`; `scan64.py`;
  документы. Эталон для А1.2а — снимок `A1.1b` (ядро) / `A1.1` (остальное):
  при следующей проверке взять `BASE=A1.1b` (снимок полный, ядро в нём новое).

## Перенесено в А1.5 (длины и смещения в `vir_bytes`/`phys_bytes`)

a16 `obj/abi64-scan-A1.1/new-a16.txt` (20 мест): `ahci.c` 1036/1155/1192,
`at_wini.c:1358`, `virtio_blk.c` 212/321/371, `procfs/pid.c:129`,
`i386/memory.c` 321/587 (+ earm 294/488), `do_safecopy.c` 210/243–244
(`offset_in`, `cp_len`/`bytes` с `%d`), `vm/mem_cache.c:225`,
`vm/pagetable.c:1226`, `vm/region.c` 46/324/333/1077/1530. Вне скана:
`at_wini.c:1350` (`VERBOSE_DMA`, `%ld`), `amddev.c:186` (`#if 0`),
`vm/region.c` 157/222 (`SANITYCHECKS`, `pr->offset`), `vm/mmap.c:113` (`#if 0`).
Тип ≠ смысл (в А1.5): `vm_memset` `pfa`/`ptr` — `phys_bytes`, но адреса в окне
ядра (печать по типу, `PRIxPHYS`).

## Предложение разбиения А1.2а (не обсуждалось с владельцем)

Сейчас физический путь: `SYS_VIRCOPY`/`SYS_PHYSCOPY` — один `do_copy`, `NONE` =
физический (`virtual_copy_f` → `lin_lin_copy(NULL, …)`); `SYS_MEMSET` с `NONE`.
Потребители — только VM: `sys_abscopy` (`pb.c:153`, `pagetable.c:1303`,
`region.c:891`), `sys_physcopy(NONE…)` (`main.c:301,358`, `pagetable.c:122`),
`sys_vircopy(NONE…)` (`pagetable.c:1266/1269` i386/earm — дыра),
`sys_memset(NONE…)` (`mem_file.c:76`, `alloc.c:454,470`); в ядре
`do_readbios.c:23`. Права `PHYSCOPY` у tty и memory в `etc/system.conf` не
используются (вызовов нет).
1. **А1.2а-1 ядро внутри:** `copy_phys_vir`/`copy_vir_phys`/`copy_phys_phys`
   (и `memset_phys`) поверх нынешнего `lin_lin_copy`/`vm_memset`, i386 и earm;
   `do_readbios` на них. Kernel calls не меняются. Проверка полная.
2. **А1.2а-2 новые kernel calls:** `SYS_COPY_PHYS` (направление флагом или три
   номера — решить) и `SYS_MEMSET_PHYS`, сообщения по §14а (адреса и длина —
   `uint64_t`, проверка помещается ли), права в `minix-service/parse.c` и
   `etc/system.conf`, статические права VM (`rs/table.c`); libsys
   `sys_copy_phys_*`, `sys_memset_phys`; `host-test abi64`. Потребителей нет.
3. **А1.2а-3 VM на новые вызовы** (10 мест выше, i386 и earm); после этого
   «`NONE` = физический» никто не использует — удаление в А1.2б.
Вопросы владельцу: один номер с флагом направления или три; оставить ли
`SYS_PHYSCOPY` номером для нового вызова; убирать ли права tty/memory сразу.

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

1. А1.2а — обсудить разбиение (выше), затем вносить; проверка полная.
2. Отдельный шаг (после А1.7): атрибут `__format__` у `printf` в `sysutil.h`
   и 25 старых несовпадений формата (`types-audit.md` §2.1).
