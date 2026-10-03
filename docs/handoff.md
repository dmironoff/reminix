# Handoff — снимок состояния

> Этот файл перезаписывается каждую сессию. Не храните здесь архитектурные решения —
> они идут в `docs/architecture.md` / `docs/porting.md` / `docs/modernization.md` /
> `docs/memory.md` / `docs/threads.md` / `docs/testing.md` / `docs/build-*.md` /
> `docs/docker-build.md` / `docs/types-audit.md` / `docs/messages.md`.

## Дата и контекст

2026-10-03. **Этап 0.3 (`kyield`) выполнен.** Ловушка IPC `MINIX_YIELD`, обёртки
`kyield()`/`sched_yield()`, тест 95; i386 1 CPU и SMP 4 CPU — 102/102, earm
собирается. Решения владельца в этой сессии: ловушка вместо kernel call; этап 0.4
— после `kyield`; earm — только проверка сборки, без тестовых прогонов, пока
владелец не скажет.

## Сделано в сессии

- Дизайн и реализация — `docs/modernization.md` п. 1 (там же ответ на открытый
  вопрос 6); класс ловушек IPC — `docs/architecture.md` §2.1, семейство быстрых
  ловушек — `docs/threads.md` §1.4, §3.
- Ядро: `MINIX_YIELD` = 7 (`ipcconst.h`), `do_yield()` и `MF_YIELD` в
  `proc.c`/`proc.h`, ветка в `switch_to_user()`.
- Библиотеки: `ipc_minix_yield()` (i386 `int`, ARM `svc`; в libc и libminc),
  `kyield()` в libsys, `sched_yield()` + `__libc_thr_yield` в libc (раньше не
  линковался вовсе), `mthread_yield()` при пустой очереди уступает CPU процессом.
  `trace(1)` знает `minix_yield`.
- Тест 95 (`minix/tests/test95.c`): коды возврата, пинг-понг через SysV shm, цена
  вызова — `testing.md` §3.
- Справка `build.mk` и `testing.md` §2: на 4 CPU полный прогон — `HANG_IDLE=3000
  TEST_TIMEOUT=9000` (тест 79 молчит до ~40 мин).

## Итог прогонов

- i386, обычное ядро, 1 CPU: **102/102 PASS**, 1460 с.
- i386, SMP, 4 CPU: полный прогон с `HANG_IDLE=1800` — 1–78 PASS, ложный `HANG` на
  79 (система жива, дамп `obj/test-logs/i386-smp-cpu4-20261003-135619.postmortem`);
  `TESTS=79` — PASS за 2318 с; 80–95 + скрипты — 23/23 PASS. Итого **102/102**.
- test95: `sched_yield` 250–333 нс (1 CPU), 2167 нс (4 CPU).
- earm (`sdimage BOARD=beaglebone`): собирается; заглушка ARM проверена
  дизассемблером. Прогонов нет (решение владельца).

## Доступ к среде

- Ubuntu, AMD Ryzen 5 5600G, KVM без AVIC; проект —
  `/media/dmironov/1fe3f4f3-4210-4da4-b212-3c442d01f36e/reminix`.
- Долгие прогоны — отвязанно от сессии:
  `(setsid nohup make -o image -C docker -f build.mk test-i386 … > obj/test-logs/run-….out 2>&1 < /dev/null &)`.
- `HANG_IDLE`: полный прогон на SMP — 1800, `TESTS=41` — не меньше 200.
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

- i386: обычное ядро 1 CPU, SMP 4 CPU — 102/102 (с тестом 95); SMP 1/2 CPU в этой
  сессии не гонялись (раньше 101/101).
- **Открыто** (`testing.md` §5): §5.4 — медленный межпроцессорный IPC.
- **Известно, не воспроизводится:** §5.7 — паника `uds` в тесте 90 на 4 CPU.
- Отладочный код, оставленный сознательно: `bkl_lock/bkl_unlock`,
  `dbg_cpu_events`, `dbg_boot_event`, `tickdebug`, диагностика в `test41.c`.
- Этапы 0.1–0.3 выполнены; 0.4–0.6 не начаты.
- **Решения владельца, нужные до А1** (`types-audit.md` §9): тип физического
  адреса (§9.1, `memory.md` §10 в. 7); раскладка сообщений на 64 битах (§9.2).

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
- SMP: `CLOCK_MONOTONIC` в одном процессе вернул значение чуть меньше прежнего
  (test95 на 4 CPU напечатал «-0.00 s», меньше тика) — не исследовано.

## Следующие шаги

1. **Этап 0.4** (решение владельца: после `kyield`): импорт `dtc` v1.8.1 и libfdt
   (`devicetree.md` §3).
2. **Решения владельца** по `types-audit.md` §9.1 (тип физического адреса,
   `memory.md` §10 вопрос 7) и §9.2 (сообщения на 64 битах; материал —
   `messages.md` §14). Нужны до А1 и до шага MSG.
3. Отложенные исправления (раздел выше) — когда владелец скажет; первым —
   `do_vsafecopy`.
4. Отложено по `kyield`: быстрые входы `sysenter`/`syscall` для семейства
   быстрых ловушек (вместе с `_lwp_park`/`_lwp_unpark`, п. 8); `kyield` в
   `spin_check` libsys (циклы `SPIN_UNTIL` драйверов) — не делали, меняет тайминги
   опроса.
5. §5.7 — только если проявится снова.
