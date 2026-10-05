# Handoff — снимок состояния

> Этот файл перезаписывается каждую сессию. Не храните здесь архитектурные решения —
> они идут в `docs/architecture.md` / `docs/porting.md` / `docs/modernization.md` /
> `docs/memory.md` / `docs/threads.md` / `docs/testing.md` / `docs/build-*.md` /
> `docs/docker-build.md` / `docs/types-audit.md` / `docs/messages.md`.

## Дата и контекст

2026-10-05, вечер. Разбор паники `uds` на SMP (бывшая `testing.md` §5.7):
причина найдена и исправлена — `testing.md` §4.21. Добавлен стресс-режим
прогона `TEST_JOBS`. Первая проверка системы на **8 CPU**.

## Коммиты сессии

- `uds`: приём нулевой длины, возобновлённый за другим приёмом, разбирал
  заголовок пустого буфера (`uds_recv()`, `io.c`); регрессионный подтест
  `test90aa` (подтест 27, без исправления роняет uds каждый раз);
  `TEST_JOBS=N` (`build.mk` → `run-tests.sh` → `testjobs=` →
  `rc.d/minixtests`: тесты группами по N одновременно, каждая копия в своём
  каталоге `/usr/tests/minix-posix.jobK`); `quick`: метка на 2 с раньше
  (файл, слинкованный в ту же секунду, не попадал в образ).

## Итог прогонов (2026-10-05, KVM)

| набор | результат |
|---|---|
| 4 CPU, `TESTS=4,43,73,79,90,91 ipcstat=1`, ловушка v1 | паника uds в тесте 90 на 196 с (до ловушки: в окне внутри `uds_recv`) |
| 8 CPU, `TESTS=90×12 TEST_JOBS=4`, ловушка v3 | поймано за ~30 с: пустой буфер, вызов из `sockevent_process` |
| 8 CPU, `TESTS=90`, подтест 27, uds **без** исправления | паника каждый раз, `not ok test 90` |
| 8 CPU, `TESTS=90×12 TEST_JOBS=4`, с исправлением | 1-я группа: 3 ok, 1 not ok (подтесты 6/21 — задержки теста под нагрузкой), паник нет; остановлен |
| 8 CPU, полный прогон (`7347fd558`, `HANG_IDLE=3000 TEST_TIMEOUT=9000`) | **103/103 PASS**, `tests time` **1563,2 с** (на 4 CPU было 1960,6 с, с `ipcstat=1` и до `idlepoll` по умолчанию) |

Система на 8 CPU грузится (`CPU 0..7`, APIC id 0–7).

Затем (по заданию владельца): `TEST_RANDOM=yes|SEED` и `TEST_TIMES=N` для
стресса случайными группами (`testing.md` §1); при этом исправлены stdin
параллельных копий (тест 3 падал на `ttyname(0)`) и подсчёт `failed` при
повторах (давал `INCOMPLETE`). Проверка: 4 CPU, `TESTS=1,2,3,4 TEST_JOBS=2
TEST_RANDOM=782893 TEST_TIMES=2` — PASS 8/8, тот же seed — те же группы.
Случайный стресс по всему набору ещё не запускался.

## Открыто

- Подтесты 6 и 21 теста 90 под `TEST_JOBS` (несколько копий одновременно)
  падают из-за задержек самого теста — не ошибка системы; для стресса uds
  это шум.
- Через CPU по-прежнему идут вызовы пользователей к серверам (`pm ↔ user`,
  `vfs ↔ user`): дело балансировщика SCHED (следующий шаг п. 4).
- B4 (x2APIC) — отложен.
- §5.3, §5.5 — не встречались.
- **Ждёт решения владельца — тип физического адреса для А1** (`types-audit.md`
  §9.1, `memory.md` §10 в. 7) и §9.2 (раскладка сообщений на 64 битах).
- Отладочный код, оставленный сознательно: `bkl_lock/bkl_unlock` (только при
  `BKL_DEBUG=yes`), `dbg_cpu_events`, `dbg_boot_event`, `tickdebug`,
  `dbg_ipc_*` (`ipcstat=1`), диагностика в `test41.c`. Ловушка `uds` снята
  (описание — `testing.md` §4.21, при нужде восстанавливается по нему).

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
- `quick` не ставит заголовки в `destdir` (в NetBSD это цель `includes`):
  после правки `minix/include` — `nbmake-i386 -C minix/include includes` в
  контейнере или полный `hdimage`; `/etc/system.conf` и `minix/commands` —
  тоже только `hdimage` (или `QUICK_DIRS` с `minix/commands`).
- `scan64.py` нужны собранные `obj/i386` и `obj/i386-smp` и clang ≥ 18 на хосте
  (в Docker-образе clang нет).
- Коммит и push — только по команде владельца; без строк соавторства.
- `…/reminix(тупик)` — тупиковая ветка, только чтение.
- После правки `minix/include` (например `rs.h`) объекты, собранные со старым
  заголовком, make может не пересобрать (`install` сохраняет время файла):
  2026-10-05 RS с устаревшим `request.o` затирал свой `rp` на стеке
  (`init_slot`, паника «sig manager gets lethal signal»). Удалить объекты
  каталога и пересобрать.
- Снимок в конце набора: `run-tests.sh` сам его не делает (гость выключается),
  наблюдатель по `REMINIX-TESTS-END` в журнале шлёт `test-snapshot`
  (сценарий был в scratchpad сессии; при необходимости — добавить ключ в
  `run-tests.sh`, когда прогон не идёт).
- earm: перед сборкой ядра — `nbmake-evbearm-el -C minix/include includes`
  (заголовки в destdir earm отставали).
- earm, проверка сборки одного каталога без `sdimage`:
  `docker run --rm -u $(id -u):$(id -g) -v $PWD:/work/reminix -w /work/reminix reminix-build:latest obj/evbearm-el/tooldir/bin/nbmake-evbearm-el -C <каталог> dependall`.
- `pkill -f` с шаблоном, который есть в строке самой команды bash, убивает и
  её (2026-10-05 так оборвался запуск прогона).
- `quick` не ставит `etc/rc.d/*`, а `*.conf` служб попадают в образ только
  при свежем исходнике (`docker-build.md` §4): копировать в
  `obj/<arch>[-smp]/work/fs` вручную, затем `quick`.

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
- Кэш-строки SMP: `dbg_cpu_events` (шаг 20 байт) по-прежнему делит строки
  между CPU (отладочный, на нём держатся `snapdump.py`/`sdiff.py`);
  счётчики `dbg_ipc_*` при `ipcstat=1` пишутся всеми CPU; `smp_cpu_lock`,
  `dispq_lock` не используются (по 576 байт).
- SMP: `enqueue()` пишет время входа в очередь текущему процессу
  (`proc_ptr->p_accounting.enter_queue`), а не `rp` (исходный MINIX).
- `idlepoll` на тесте 4 (три потомка крутят `getpid()`): 0,3–0,7 с против
  0,1–0,2 с без опроса — доли секунды, не разбиралось.
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

1. Push коммитов сессии (по команде владельца).
2. Полный прогон на 4 CPU с `idlepoll` по умолчанию (`HANG_IDLE=3000
   TEST_TIMEOUT=9000`, без `ipcstat`) — новая точка отсчёта; полный прогон на
   1 CPU.
3. Паника `uds` §5.7 — отдельная сессия (ловушка из §5.7, снимки есть).
4. Балансировщик SCHED (п. 4 модернизации): `pick_cpu()` вызывается при
   каждом `schedule_process()`, `cpu_proc[]` пользовательских процессов растёт с
   каждым квантом; держать пользователя ближе к его серверу.
5. B4 (x2APIC) — по решению владельца; `modernization.md` п. 4 — дописать
   размещение, `max_wait`, IRQ за владельцем, шаги IPI.
6. Решение по типу физ. адреса → план А1; Б1; отложенные исправления.
