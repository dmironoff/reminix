# Handoff — снимок состояния

> Этот файл перезаписывается каждую сессию. Не храните здесь архитектурные решения —
> они идут в `docs/architecture.md` / `docs/porting.md` / `docs/modernization.md` /
> `docs/memory.md` / `docs/threads.md` / `docs/testing.md` / `docs/build-*.md` /
> `docs/docker-build.md` / `docs/types-audit.md` / `docs/messages.md`.

## Дата и контекст

2026-10-05, день. Сборка образов i386 в `docker/build.mk` (задание владельца):
цели `cdimage`, `usbimage`, `ramimage`, запуск с любого носителя
(`run-i386 MEDIA=`), проверка загрузки `boot-test`; SMP-ядро включает все CPU
по умолчанию. **Не закоммичено** — ждёт команды владельца.

## Сделано в сессии (не закоммичено)

- Ядро (`arch/i386/pre_init.c`): SMP-ядро без `no_apic` ставит `no_apic=0` в
  параметры загрузки (видно `sysenv`, `ramdisk/rc` запускает ACPI);
  однопроцессорное — по-прежнему `no_apic=1`. Из `run-tests.sh` и
  `run-console.sh` убрана добавка `no_apic=0` при `CPUS>1`.
- releasetools: `BOOT_CONSOLE=com0` (загрузчик `consdev=com0`, ядро
  `console=tty00`), `BOOT_SMP` (по `CONFIG_SMP=y` в `BUILDVARS`) — пункт меню
  «one CPU (no_apic=1)» в hd/cd/usb; без них `boot.cfg` байт в байт прежний.
  `create_input_spec` — без `size=` (`nbmtree -R size`): устаревший размер из
  `METALOG` обрезал файлы на ISO (`system.conf`, `rc.d/minixtests`).
- `build.mk`: `cdimage` (`ISO_SETS=install|live`), `usbimage`, `ramimage`,
  `CONSOLE=vga|serial`, `PACK_ONLY=yes`, свои `WORK_DIR` (`work-cd`,
  `work-usb`, `work-ram`; `work` — за `hdimage`/`quick`, имя образа в
  `work/.image`), `run-i386 MEDIA=multiboot|hd|cd|usb|ram`, `boot-test`
  (`docker/boot-test.py`), общий `docker/qemu-i386.sh`; справка `#>`.
  `*.iso` — в `.gitignore`.
- Документация: `build-x86.md` §4.5, `docker-build.md` §3, §4, §7,
  `testing.md` §1, §1в, §1е, `CLAUDE.md`.

## Проверка (2026-10-05, KVM; полный набор тестов не гонялся — по заданию)

| что | результат |
|---|---|
| `test-i386 SMP=yes TESTS=4`, `CPUS=1` и `CPUS=4`, без `no_apic` в аргументах | PASS; режим APIC на 1 CPU, на 4 — CPU 0–3 |
| `test-i386 TESTS=4` (однопроцессорное ядро) | PASS |
| `boot-test MEDIA=cd`: SMP 4 CPU / SMP `ENTRY=2` / без SMP / `ISO_SETS=live` | PASS (ncpu 4 / 1 / 1 / 1), корень `/dev/c0d2` isofs, RAM-диски `/var /tmp /usr/run /root` |
| `boot-test MEDIA=hd` SMP 4 CPU / `ENTRY=4` | PASS (4 / 1) |
| `boot-test MEDIA=usb` SMP 4 CPU / без SMP | PASS, корень `bootramdisk` |
| `boot-test MEDIA=ram` SMP 4 CPU / без SMP | PASS |
| `run-i386 MEDIA=cd SMP=yes CPUS=4`, ввод конвейером | вход, `hw.ncpuonline = 4`, `halt -p` |
| `quick` после `cdimage`/`usbimage`/`ramimage` (`SMP=no`) | освежил `minix_x86.img`, 29 с |
| earm `sdimage BOARD=beaglebone` (общие `image.*`) | образ собран (`SDIMAGE-RC=0`), без запуска |

Сравнить дизассемблирование однопроцессорного ядра «до/после» не вышло:
прежняя сборка `obj/i386` была от 00:34, до пяти коммитов в ядро; правка для
него — только код под `#ifdef CONFIG_SMP` и комментарий.

## Открыто

- **Остановка стенда при случайном стрессе** (`testing.md` §5.9): после
  нехватки памяти VM (тест 64) `rc` получил ENOENT на `/dev/null` и прогон
  исчез. Повтор: `TEST_RANDOM=896425` с теми же параметрами.

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

1. Коммит образов i386 и `no_apic` по умолчанию, push (по команде владельца).
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
