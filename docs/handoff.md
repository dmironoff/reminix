# Handoff — снимок состояния

> Этот файл перезаписывается каждую сессию. Не храните здесь архитектурные решения —
> они идут в `docs/architecture.md` / `docs/porting.md` / `docs/modernization.md` /
> `docs/memory.md` / `docs/threads.md` / `docs/testing.md` / `docs/build-*.md` /
> `docs/docker-build.md` / `docs/types-audit.md` / `docs/messages.md`.

## Дата и контекст

2026-10-02, конец дня. **Этап 0.2 выполнен полностью:** аудит 64-битных типов
(`docs/types-audit.md`, модуль `minix/tests/host/abi64`, `scan64.py`) и
путеводитель по сообщениям IPC (`docs/messages.md`). Всё закоммичено и запушено в
`origin/master`; временных веток и worktree нет. Код системы в этапе 0.2 не
менялся (только тесты на хосте и документы).

## Сделано в сессии

- Аудит 64-битных типов (`038580154`): проход clang по всему дереву в режимах
  m32/m64/p64 (`scan64.py`), тест раскладок `abi64` (TAP `# TODO` + «храповик»),
  ручной разбор — 323 места в `types-audit.md`; `phys_addr_t` = `uint64_t`
  поставлен под вопрос (`memory.md` §10 вопрос 7).
- Путеводитель по сообщениям (`427e07137` и далее):
  - опись всех 256 типов `ipc.h`: поля, смещения i386/x86_64 (clang
    `-fdump-record-layouts` на дереве `abi64/mkinc.sh`), 129 псевдонимов общих
    раскладок (`com.h`, `vboxif.h`), прямые обращения к `m*_*` (PCI, `IOMMU_MAP`);
  - для каждого типа — направление, `m_type`, отправитель/получатель, назначение
    каждого поля по коду, пометки «64» (А/Ф/Р/С/У/Ж/В);
  - итог: А 132, Р 90, С 62, В 116, Ж 30, У 23, Ф 16 полей; 13 ошибок протоколов
    (`messages.md` §12), 10 мёртвых типов (§13), выводы для переработки (§14).

## Итог прогонов

Прогонов не было: код не менялся (abi64 и `host-test` — как в `038580154`,
12/12 PASS, 6 `# TODO`).

## Доступ к среде

- Ubuntu, AMD Ryzen 5 5600G, KVM без AVIC; проект —
  `/media/dmironov/1fe3f4f3-4210-4da4-b212-3c442d01f36e/reminix`.
- Долгие прогоны — отвязанно от сессии:
  `(setsid nohup make -o image -C docker -f build.mk test-i386 … > obj/test-logs/run-….out 2>&1 < /dev/null &)`.
- `HANG_IDLE`: полный прогон на SMP — 1800, `TESTS=41` — не меньше 200.
- Сборка параллельно с прогоном: `JOBS=6` и `renice -n 10` контейнеру сборки.
- `scan64.py` нужны собранные `obj/i386` и `obj/i386-smp` и clang ≥ 18 на хосте
  (в Docker-образе clang нет).
- Коммит и push — только по команде владельца; без строк соавторства.
- `…/reminix(тупик)` — тупиковая ветка, только чтение.

## Текущий статус

- i386: обычное ядро 1 CPU, SMP 1/2/4 CPU — 101/101 (без изменений).
- **Открыто** (`testing.md` §5): §5.4 — медленный межпроцессорный IPC.
- **Известно, не воспроизводится:** §5.7 — паника `uds` в тесте 90 на 4 CPU.
- Отладочный код, оставленный сознательно: `bkl_lock/bkl_unlock`,
  `dbg_cpu_events`, `dbg_boot_event`, `tickdebug`, диагностика в `test41.c`.
- Этапы 0.1–0.2 выполнены (0.2 — с путеводителем по сообщениям); 0.3–0.6 не начаты.
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

## Следующие шаги

1. **Решения владельца** по `types-audit.md` §9.1 (тип физического адреса,
   `memory.md` §10 вопрос 7) и §9.2 (сообщения на 64 битах; материал —
   `messages.md` §14). Нужны до А1 и до шага MSG.
2. **Этап 0.3 — `kyield`** (`modernization.md` п. 1, `threads.md`); проверка —
   `test-i386` на 1 и 4 CPU.
3. Параллельно (не пересекается по коду) — **этап 0.4**: импорт `dtc` v1.8.1 и
   libfdt (`devicetree.md` §3).
4. Отложенные исправления (раздел выше) — когда владелец скажет; первым —
   `do_vsafecopy`.
5. §5.7 — только если проявится снова.
