# Handoff — снимок состояния

> Этот файл перезаписывается каждую сессию. Не храните здесь архитектурные решения —
> они идут в `docs/architecture.md` / `docs/porting.md` / `docs/modernization.md` /
> `docs/memory.md` / `docs/threads.md` / `docs/testing.md` / `docs/build-*.md` /
> `docs/docker-build.md` / `docs/types-audit.md` / `docs/messages.md`.

## Дата и контекст

2026-10-03. **Этап 0 плана выполнен целиком**: 0.3 (`kyield`), 0.4 (`dtc`
1.8.1 и libfdt), 0.5 (DTS, `.dtb` Orange Pi PC Plus), 0.6 (libfdt в ядре earm,
тесты на хосте). Всё закоммичено и запушено (`ea5756356`, `d25053952`,
`61e00a8c1`, `b4ac6844e`, `91a419229`, `79e7ce39b`). Владелец: ждать указаний. Решения владельца в сессии: ловушка вместо
kernel call; 0.4 после `kyield`; earm — только проверка сборки, без прогонов;
тест 79 на 4 CPU — проблема быстродействия, не ошибка.

## Сделано в сессии

- **0.3 `kyield`** (`ea5756356`): ловушка IPC `MINIX_YIELD` (7), `do_yield()`/
  `MF_YIELD`; `ipc_minix_yield()` (libc, libminc; i386 `int`, ARM `svc`),
  `kyield()` (libsys), `sched_yield()`/`__libc_thr_yield` (libc), `mthread_yield()`
  уступает CPU при пустой очереди; тест 95. Документы: `modernization.md` п. 1 и
  вопрос 6, `threads.md`, `architecture.md` §2.1, `testing.md`.
- **0.4 dtc/libfdt** (`devicetree.md` §3.3–3.4): `dtc2reminix`, `dist` без
  `tests/` и без `AGENTS.md`/`CLAUDE.md` апстрима; парсер и лексер `dtc`
  сгенерированы bison/flex при импорте (byacc дерева не тянет `%locations`);
  локальных патчей `dist` нет; libfdt и пять утилит в системе; `nbdtc` при
  `MKDTB` (earm); `TOOL_DTC`, `MKDTC`, `MKDTB`; `external/gpl2` подключён к сборке
  MINIX; тест `fdt` (`testfdt.sh`).
- **0.5 DTS** (`devicetree.md` §3.3, §3.5): devicetree-rebasing v7.2-dts
  (`1826bba6`, 23.08.2026) целиком в `sys/external/gpl2/dts/dist` + README;
  `share/mk/bsd.dtb.mk` под раскладку `dist/src/<arch>`, явный список `DTS`;
  `sys/dtb/arm/allwinner` → `sun8i-h3-orangepi-pc-plus.dtb` в `/boot/dtb/allwinner`
  (mtree `NetBSD.dist.earm`, `minix-base/md.evbarm`); `DTBDIR` и пр. в
  `bsd.own.mk`; `sys/Makefile` — `dtb` при `MKDTB`.
- **0.6 libfdt в ядре earm** (`devicetree.md` §3.1 п. 6, §3.6): `fdt.c`,
  `fdt_ro.c`, `fdt_strerror.c` + `memchr`/`memcmp`/`strnlen`/`strrchr` —
  unpaged; без `fdt_addresses.c` (тянет запись) и **без патча `libfdt_env.h`**
  (не понадобился) — оба отступления от плана записаны. Модуль тестов на хосте
  `fdt` (9 тестов). В `bsd.dtb.mk` добавлены флаги cpp `-nostdinc -undef
  -D__DTS__` (cpp хоста портил `linux,code`; `.dtb` earm не изменился).

## Итог прогонов

- 0.3: i386 1 CPU — 102/102; SMP 4 CPU — 102/102 (полный прогон с
  `HANG_IDLE=1800` встал ложным `HANG` на 79; `TESTS=79` — PASS за 2318 с; 80+ —
  23/23). earm собирается.
- 0.4: полные сборки i386 (`hdimage`) и earm (`sdimage BOARD=beaglebone`) —
  успешно, `checkflist` чист; `build.sh tools` earm собирает `nbdtc` с нуля;
  QEMU i386 `TESTS=fdt,1,2,95` — 4/4 PASS. Полный прогон не делали: изменения
  0.4 только добавляют новые программы, библиотеку и тест.
- 0.5: полные сборки earm и i386 — успешно, `checkflist` чист; `.dtb` —
  `xunlong,orangepi-pc-plus`, `/cpus` (4 ядра), `__symbols__`; `/memory` в DTS
  апстрима нет — добавит U-Boot. Запусков нет: `.dtb` пока никто не грузит.
- 0.6: ядро earm собирается и компонуется (55 символов `__k_unpaged_fdt*`),
  полная сборка earm — успешно; ядро i386 не пересобирается (хэш тот же);
  `host-test` — все 16 + 4 программы PASS (fdt 9/9 во всех вариантах); `.dtb`
  из тестов и из сборки earm совпадают побайтно (`93a4e32e…`).

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

- i386: обычное ядро 1 CPU, SMP 4 CPU — 102/102 (с тестом 95); с тестом `fdt` —
  103 теста (`fdt` прогнан отдельно). SMP 1/2 CPU в этой сессии не гонялись.
- **Открыто** (`testing.md` §5): §5.4 — медленный межпроцессорный IPC.
- **Известно, не воспроизводится:** §5.7 — паника `uds` в тесте 90 на 4 CPU.
- Отладочный код, оставленный сознательно: `bkl_lock/bkl_unlock`,
  `dbg_cpu_events`, `dbg_boot_event`, `tickdebug`, диагностика в `test41.c`.
- Этап 0 (0.1–0.6) выполнен. Дальше — направления А и Б.
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

1. Ждать указаний владельца.
2. **Направление А** (ядро и память, i386) начинается с **А1** — 64-битные физ.
   адреса в ядре и VM, `refcount` → 32 бита (М1). **Нужны решения владельца**:
   `types-audit.md` §9.1 (тип физического адреса, `memory.md` §10 вопрос 7) и
   §9.2 (раскладка сообщений на 64 битах; материал — `messages.md` §14).
3. **Направление Б** (ARM, Orange Pi PC Plus) начинается с **Б1**. До него —
   решения владельца по открытым вопросам `modernization.md` 1 (soft-/hard-float
   на ARM) и 2 (GCC или Clang), и проверка [Claude] 3 (QEMU `orangepi-pc`:
   PSCI апстримного U-Boot v2026.07, загрузка с SD через BootROM) — её можно
   сделать без решений.
4. Отложенные исправления (раздел выше) — когда владелец скажет; первым —
   `do_vsafecopy`.
5. Отложено по `kyield`: быстрые входы `sysenter`/`syscall` для семейства
   быстрых ловушек (вместе с `_lwp_park`/`_lwp_unpark`, п. 8); `kyield` в
   `spin_check` libsys — не делали, меняет тайминги опроса.
6. §5.7 — только если проявится снова.
