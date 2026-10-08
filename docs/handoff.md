# Handoff — снимок состояния

> Этот файл перезаписывается каждую сессию. Не храните здесь архитектурные решения —
> они идут в `docs/architecture.md` / `docs/porting.md` / `docs/modernization.md` /
> `docs/memory.md` / `docs/threads.md` / `docs/testing.md` / `docs/build-*.md` /
> `docs/docker-build.md` / `docs/types-audit.md` / `docs/messages.md`.
> **Текущий план работ — `docs/work-plan.md`** (решения владельца 2026-10-07, порядок,
> правила проверки); выполненное отмечается там.

## Дата и контекст

2026-10-08. Начата работа по `work-plan.md`: доклад о восстановлении среды
(сборки и прогоны владельца 2026-10-07), §4.1 (среда и А5), §4.2 (отложенные
исправления), §8 (правка документов). Закоммичены §4.1 (`2c879b721`) и
документы §8 (следующий коммит); **исправления кода §4.2 не закоммичены** —
идёт их проверка, коммиты — по команде владельца.

## Сделано в сессии

- **Эталон HEAD** (`fc5cf831c`, сборка с нуля 2026-10-07,
  `obj/test-logs/restore-20261007-1116.summary`): все сборки, `host-test` PASS,
  три полных прогона 103/103 — однопроцессорное ядро 1 CPU 1386,0 с, SMP-ядро
  1 CPU 1413,4 с, 4 CPU 1572,6 с (`tests time`; `testing.md` §2). scan64 —
  эталон в `types-audit.md` §2.1 («Эталон перед А1»; раскладок 814/54 вместо
  1082/63 в аудите — причина не установлена).
- **§4.1 выполнен.** `SMP=yes` — умолчание `docker/build.mk` для i386
  (`$(if $(filter i386,$(ARCH)),yes,no)`; на earm `no`), справка `#>`, примеры
  в `docker-build.md`, `testing.md` §1, `CLAUDE.md`. А5 отмечен в
  `modernization.md`. Новый **`docker/disasm.sh snapshot|compare`**
  (`docker-build.md` §4.1); эталон — `obj/disasm/fc5cf831c/{i386,i386-smp}`.
- **§4.2 — исправления внесены, проверка идёт** (пакет `batch1`, см. ниже).
  Каждое — в своих файлах, коммитить по отдельности:

  | № | Файлы | Что | Код |
  |---|---|---|---|
  | 1 | `kernel/system/do_safecopy.c` | `els < 0 \|\| els > SCPVEC_NR` → `EINVAL` | меняется |
  | 9 | `kernel/system/do_memset.c` | `return vm_memset(...)` (был всегда `OK`) | меняется |
  | 3 | `fs/ptyfs/ptyfs.c` | `snprintf(name, size, …)` | меняется |
  | 4 | `servers/vfs/mount.c` | копируется `label_len`; пустая или без `'\0'` — `EINVAL` (как `do_mapdriver`) | меняется |
  | 10 | `lib/libmthread/pthread_compat.c` | `pthread_mutex_trylock` → `mthread_mutex_trylock` | меняется |
  | 8 (часть) | `kernel/system/do_irqctl.c` | `(irq_id_t) 1 << notify_id` | не должен |
  | 7 (часть) | `include/minix/u64.h`, `timers.h`, `param.h`; `filter/sum.c`, `fbdctl.c`, `btrace.c`, `test53.c`; `libc/sys/getdents.c`, `ipc/inc.h`, `ext2/proto.h`, `libfsdriver/call.c`, `memory.c`, `fbd.c`, `vnd.c`, `mmcblk.c`; `kernel/arch/{i386,earm}/protect.c` | `ex64lo/ex64hi/make64` → `uint32_t` и форматы их аргументов `%l*` → `%*`; `int`↔`ssize_t` в прототипах (getdents → `int` как в `dirent.h`; `fsdriver_bread/bwrite` → `int` как таблица; ipc, ext2 `fs_rdlink`, `bdr_transfer` memory/fbd/vnd/mmcblk → `ssize_t`); `kinfo.vm_allocated_bytes` и `alloc_for_vm` → `size_t` | не должен |

- **§8 (документы) выполнен:** `messages.md` §12 (`do_memset`, псевдогрант
  `SYS_UMAP`), §13 (`SCHEDCTL_FLAG_KERNEL` живой, `VMCTL_I386_INVLPG` мёртв),
  §14а (сценарий расчёта не сохранён); `types-audit.md` §8 (`AC_LOWER4G` → А3,
  подшаги А1), §9а (`-Wformat` и порядок `PRI*`); `arch-i386.md` (`lapic_addr`);
  `modernization.md` (правило 4 — проверка по виду шага, фактический порядок,
  строки А1/MSG/А3/А4).

## Проверка пакета `batch1` (идёт)

Владелец решил проверить исправления №1, 3, 4, 7, 8, 9, 10 **одним пакетом**:
сборка, сравнение дизассемблера, один полный прогон. Цепочка —
`obj/test-logs/check-chain.sh` (вне дерева), запущена 2026-10-08 15:43 с
`CLEAN=yes` (объекты удалены, кроме `tooldir`, `tools`, `u-boot`, — чтобы правка
заголовков `minix/include` пересобрала всех потребителей). Шаги: `hdimage SMP=no`,
`hdimage SMP=yes`, `sdimage BOARD=beaglebone`, `disasm.sh snapshot batch1`,
`disasm.sh compare fc5cf831c batch1`, `host-test`, `test-i386` UP/1, SMP/1,
SMP/4. Итог — `obj/test-logs/check-batch1.summary` (`CHECK-DONE` или
`CHECK-FAILED`), вывод — `check-batch1.out`.

Что осталось по пакету:
1. Дождаться `CHECK-DONE`; при `CHECK-FAILED` — причина в `check-batch1.out`
   (вероятно `-Wformat` от `u64.h` где-то, где форматы не нашлись поиском).
2. Разобрать сравнение по функциям: ожидаемо изменились только `do_vsafecopy`,
   `do_memset`, `make_name` (ptyfs), `do_mount` (vfs), `pthread_mutex_trylock`
   (libmthread и всё, что с ней слинковано) и места со сдвинутым `__LINE__`
   (`mount.c` и `do_safecopy.c` выросли на строку). №7 и №8 — без изменений
   кода. Сравнение по функциям — `docker/disasm.sh compare --funcs fc5cf831c batch1`.
3. Сравнить прогоны с эталоном (`tests time` 1386,0 / 1413,4 / 1572,6 с).
4. Отметить №1, 3, 4, 7, 8, 9, 10 в `work-plan.md` §4.2, `messages.md` §12 и
   `types-audit.md` §8 как исправленные; коммиты — по команде владельца.

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
| 1 | `do_vsafecopy`: нет проверки `0 <= els <= SCPVEC_NR` — **безопасность** | внесено, проверка `batch1` |
| 9 | `do_memset()` теряет `VMSUSPEND` | внесено, проверка `batch1` |
| 3 | `ptyfs.c:59` `sizeof(name)` | внесено, проверка `batch1` |
| 4 | `vfs/mount.c:113` `sizeof(mount_label)` | внесено, проверка `batch1` |
| 8 (часть) | `do_irqctl.c:166` `1 << notify_id` | внесено, проверка `batch1` |
| 10 | `pthread_mutex_trylock` вызывает сам себя | внесено, проверка `batch1` |
| 7 (часть) | `u64.h`, `int`↔`ssize_t`, `kinfo.vm_allocated_bytes` | внесено, проверка `batch1` |
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

1. Закончить проверку `batch1` (см. выше), доложить; коммиты по одному
   исправлению — по команде владельца (сначала §4.1 и документы).
2. А1.0 — заголовок с типами §9а синонимами нынешних, `PRIx*`, `phys_to_dma()`;
   проверка — `disasm.sh compare` с эталоном после `batch1`.
3. Дальше — `work-plan.md` §5: А1.1 (форматы по каталогам), А1.2а …
