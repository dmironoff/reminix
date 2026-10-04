# Handoff — снимок состояния

> Этот файл перезаписывается каждую сессию. Не храните здесь архитектурные решения —
> они идут в `docs/architecture.md` / `docs/porting.md` / `docs/modernization.md` /
> `docs/memory.md` / `docs/threads.md` / `docs/testing.md` / `docs/build-*.md` /
> `docs/docker-build.md` / `docs/types-audit.md` / `docs/messages.md`.

## Дата и контекст

2026-10-05. Разобран §5.8 (тест 4 минутами на 4 CPU с MCS): причина — не
замок, а голодание VFS за PM на BSP. По решению владельца сделан «гибрид»:
защита от голодания в планировщике ядра (`max_wait`), размещение системных
сервисов по CPU в RS (`cpu auto`, `apart`, PM и VFS — на разных CPU) и
прерывания, идущие за CPU драйвера. Всё закоммичено и запушено. **Размещение
серверов по CPU вскрыло старые гонки между серверами — две из трёх открыты,
загрузка на 4 CPU падает примерно в половине прогонов** (ниже).

## Разбор §5.8

Снимки и замеры через монитор QEMU (`testing.md` §5.8): три потомка теста 4
крутят `getpid()` на AP и держат очередь PM непустой; PM (приоритет 4,
планирует ядро, квант просто продлевается) не блокируется, VFS (5) на том же
BSP не получает процессора — `fork`/`exit` ждут VFS. `smp_schedule_sync` и
очередь BKL ни при чём (`sched_ipi_data.flags` = 0 во всех замерах, в очереди
≤ 1 узла). Test-and-set скрывал это паузами до 65536 `pause`: AP медленнее
возвращались к PM, его очередь пустела. Контроль: 1 CPU — 0,2 с; старый замок
(`e4ef8ce81`, отдельное рабочее дерево) на 4 CPU — 1,0 с; MCS — 183 и 134 с.

## Сделано в сессии

- **`max_wait` — защита от голодания** (ядро: `sched_starve_check()` на
  каждом тике CPU, `pick_proc()` берёт `starved_pick`; поля `p_max_wait`,
  `p_ready_since` в конце `struct proc`, `MF_STARVE_SLOT`). Готовый процесс,
  ждущий в очереди своего CPU дольше `max_wait` за более приоритетными,
  получает один слот (до блокировки, не дольше тика); вытесненный встаёт в
  голову своей очереди. Ключ `max_wait <мс>` в `system.conf` (`system.conf.5`),
  умолчания `SRV_MAXWAIT` 20 мс / `USR_MAXWAIT` 0 (`<minix/priv.h>`); путь —
  RS → `sys_schedctl` / `SCHEDULING_START` → SCHED → `sys_schedule` (новые поля
  `max_wait` в сообщениях). Работает и без SMP.
- **Размещение системных сервисов** (RS `sched_pick_cpu()`): `cpu auto|bsp|N`
  (`auto` — умолчание `DSRV_CPU`), `apart <метки>` (симметрично); выбирается
  CPU с наименьшим числом системных сервисов без конфликтов `apart`. `pm` в
  `etc/system.conf` — `apart vfs`. SCHED принимает CPU от RS (`cpu_fixed`).
- **Перенос процесса между CPU** (`smp_move_proc()` вместо
  `smp_schedule_migrate_proc()`): всегда через старый CPU с сохранением
  контекста и FPU, и для неготового процесса; `switch_to_user()` не продолжает
  текущий процесс, если его `p_cpu` уже другой (RS переносит сам себя по
  `edit rs` — раньше вышел бы процесс на двух CPU).
- **IRQ за владельцем**: `irq_follow_owner()` / `irq_follow_owner_proc()`
  (`interrupt.c`) направляют линию на CPU драйвера при `IRQ_SETPOLICY` и при
  переносе; `hw_intr_set_cpu()` — i386 IOAPIC (`ioapic_set_irq_cpu()`, только
  старшее слово, без маскирования), earm — заглушка. `dbg_cpu_events` — пятый
  счётчик: принятые аппаратные IRQ по CPU.
- SCHED: `cpu_proc[]` больше не уменьшается для системных процессов
  (беззнаковый счётчик переполнялся).
- VFS: гонка асинхронного `close()` сокета исправлена (гонка 1 ниже).

## Гонки между серверами, вскрытые размещением по CPU

До этого все серверы жили на BSP, и порядок их работы задавали приоритеты
(VM к тому же не `PREEMPTIBLE`). На разных CPU порядок не гарантирован:

1. **VFS, асинхронный `close()` сокета — исправлено** (`vfs/filedes.c`,
   `close_filp`). `sdev_close(may_suspend)` отправлял `SDEV_CLOSE` и
   приостанавливал процесс, а поток затем ждал PFS в `put_vnode()`; `lwip` на
   своём CPU отвечал сразу, `sdev_reply()` будил процесс, тот делал
   `socket()` — паника VFS «process has two calls (105, 131)» (`ifconfig`
   при «Starting network», 3/3 до исправления). Теперь приостанавливающий
   `SDEV_CLOSE` отправляется последним шагом.
2. **VFS / PM / VM при выходе процесса — открыто.** VFS шлёт VM
   `VM_PROCCTL HANDLEMEM` (с идентификатором транзакции) для процесса, которого
   PM и VM уже убрали: «VM: bogus endpoint VM_PROCCTL 33188», `do_procctl`
   возвращает `EINVAL` вместо `SUSPEND` → `assert "!IS_VFS_FS_TRANSID(transid)"`
   (`vm/main.c:184`). Журнал `obj/test-logs/i386-smp-cpu4-20261005-010021.*`.
3. **Ядро: копирование у процесса с `RTS_VMINHIBIT` — открыто.** Пока VM (на
   своём CPU) правит таблицы страниц процесса, сервер с другого CPU делает
   `safecopy` по гранту этого процесса: `assert "!RTS_ISSET(srcproc,
   RTS_VMINHIBIT)"` в `lin_lin_copy` (`do_safecopy_to` → `safecopy` →
   `verify_grant` → `data_copy`), паника ядра (видна только на VGA). Дамп
   `obj/test-logs/i386-smp-cpu4-20261005-010059.postmortem`.

**Временная мера (коммит после этой сессии):** серверам (rs, ds, vm, pm,
sched, vfs, mfs, ext2, ntfs-3g, pfs, mib, is, procfs, isofs, hgfs, ptyfs,
devman, ipc, uds, lwip, vbfs) прописано `cpu bsp` в `etc/system.conf` и их
`.conf`; драйверы (в т. ч. tty, memory, log) остаются на `cpu auto`, их IRQ
идут за ними. `apart vfs` у pm оставлен (действует только при `auto`).
Проверка: `TESTS=4`, 4 CPU, 6 прогонов — 5 PASS (20,0 / 22,0 / 27,8 / 30,7 /
37,0 с), 1 ложный HANG при `HANG_IDLE=30` (тест длиннее порога); паник нет.
Вопрос: с `max_wait` и всеми сервисами на BSP (включая драйверы) тест 4 шёл
2,0 с (один прогон) — почему с драйверами на AP 20–37 с, не выяснено. Чтобы
вернуть автоматическое размещение серверов — убрать эти строки `cpu bsp`.

Предложено (не сделано): для 3 — не падать, а приостанавливать вызов ядра и
повторять его, когда VM снимет `VMINHIBIT` (по образцу `MF_KCALL_RESUME`); для
2 — VM отвечает VFS ошибкой с идентификатором транзакции вместо ассерта
(проверить разбор в VFS). Возможны и другие гонки. Запасной вариант —
временно `cpu bsp` для основных серверов в `system.conf`, `auto` — драйверам.

## Итог прогонов (2026-10-05)

- Без размещения (RS игнорирует `auto`), с `max_wait`: `TESTS=4`, 4 CPU —
  PASS, **2,0 с** (было 134–717 с).
- Всё вместе, после исправления гонки 1: `TESTS=4`, 4 CPU — 4 прогона: PASS
  0,5 с ×2, паника VM (гонка 2), паника ядра (гонка 3). SMP-ядро на 1 CPU —
  PASS 0,1 с; обычное ядро — PASS 0,3 с.
- Проверка «процесс текущий на двух CPU» (временная, в `switch_to_user()`) за
  эти прогоны не сработала ни разу; из дерева убрана.
- Полный прогон, сравнение с 3993,7 с и сборка earm — **не делались**.

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


## Текущий статус

- i386: обычное ядро и SMP — собираются; серверы закреплены на BSP
  (`cpu bsp`), с ними `TESTS=4` на 4 CPU стабилен; с `auto` для серверов
  загрузка нестабильна из-за гонок 2–3 (выше). Последний полный прогон (до этой сессии, MCS) —
  103/103.
- **Открыто:** гонки 2–3; §5.4 — медленный межпроцессорный IPC; §5.3/§5.5;
  §5.7 (не воспроизводится).
- Отладочный код, оставленный сознательно: `bkl_lock/bkl_unlock` (только при
  `BKL_DEBUG=yes`), `dbg_cpu_events`, `dbg_boot_event`, `tickdebug`,
  диагностика в `test41.c`.
- **Ждёт решения владельца — тип физического адреса для А1** (`types-audit.md`
  §9.1, `memory.md` §10 в. 7) и §9.2 (раскладка сообщений на 64 битах);
  предложение прошлой сессии — в истории этого файла (`git log -p docs/handoff.md`).

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

1. Гонки 3 и 2 (с `cpu auto` для серверов), затем серия
   `TESTS=4` на 4 CPU, полный прогон на 4 CPU (`HANG_IDLE=3000
   TEST_TIMEOUT=9000`), сравнение `tests time` / `.durations` с 3993,7 с;
   полный прогон на 1 CPU (`max_wait` действует и там); сборка earm.
2. Обновить `testing.md` §2 таблицей прогонов и `modernization.md` п. 4
   (размещение, `max_wait`, IRQ за владельцем) после стабилизации.
3. Замечено: `pick_cpu()` в SCHED вызывается при каждом `schedule_process()`,
   `cpu_proc[]` пользовательских процессов растёт с каждым квантом; в
   `enqueue_head()`/`enqueue()` время входа в очередь пишется текущему
   процессу (`proc_ptr`), а не `rp` (исходный MINIX). Балансировщика во время
   работы нет — следующий шаг п. 4.
4. Решение по типу физ. адреса → план А1; Б1; отложенные исправления.

