# Handoff — снимок состояния

> Этот файл перезаписывается каждую сессию. Не храните здесь архитектурные решения —
> они идут в `docs/architecture.md` / `docs/porting.md` / `docs/modernization.md` /
> `docs/memory.md` / `docs/threads.md` / `docs/testing.md` / `docs/build-*.md` /
> `docs/docker-build.md`.

## Дата и контекст

2026-09-30 / 10-01. Начат **этап 0.1** (точка отсчёта тестов). Первые правки кода.

Что сделано в сессии:

1. **Точка отсчёта `minix/tests` на i386** (`-smp 1`, KVM, PIO): **100 из 101**.
   Подробно, с метриками и ожидаемым «шумом» журнала — `docs/testing.md` §2–3.
2. **Найдена и исправлена ошибка ядра**: зависание в `halt_cpu`, если прерывание
   принято в «тени» `sti` (под KVM). Правка в `minix/kernel/arch/i386/{klib.S,
   sconst.h,mpx.S,apic_asm.S}` — `docs/testing.md` §4.1. Проверено: тест 43 —
   39 с (раньше зависал), полный прогон проходит.
3. **Найдена и исправлена ошибка `makefs`** (atime/mtime в Rock Ridge `TF`, падение
   `isofs`): `usr.sbin/makefs/cd9660/iso9660_rrip.c`, перенос из NetBSD trunk —
   `docs/testing.md` §4.2. **Не проверено** — нужна пересборка и `./run -t isofs`.
4. **Docker:** `docker/Dockerfile`, стадия `gcc7-builder` — `download_prerequisites`
   качает по HTTPS вместо зависающего FTP gcc.gnu.org. Проверено: образ собрался.
5. Документация: создан `docs/testing.md`; ветка — `master` (исправлено в
   `modernization.md`).

## Доступ к среде

- Консоли на машине пользователя (`device_bash`) нет — не поддерживается в Claude
  Desktop для Ubuntu. Работа — через файловый мост (`device_list_dir` /
  `device_stage_files` / `device_commit_files`); сборки и запуски QEMU выполняет
  владелец и присылает вывод. Код — прямо в `master`
  (https://github.com/dmironoff/reminix).
- Подключены две папки: `…/reminix` (основной проект) и `…/reminix(тупик)`
  (**только чтение**).
- **Правила для моста:**
  - после `device_commit_files` перечитывать каталог и сверять размер в байтах;
    при расхождении перезаписать с `force: true`;
  - **не запускать запись параллельно с правкой того же файла** в одном блоке
    вызовов: запись может захватить предыдущую версию (случилось с
    `iso9660_rrip.c`).
- Отладка зависшего гостя: монитор QEMU (Ctrl+Alt+2) `gdbserver tcp::1234`, дамп
  памяти `gdb -batch … dump binary memory obj/dbg/…` (каталог `obj/` не в git и
  виден мосту). Символы — `obj/i386/minix/kernel/kernel` (без `-g`); таблица
  `proc[]`: 261 слот × 560 байт. Парсер дампа был в песочнице сессии —
  при необходимости восстановить по `docs/testing.md` §4.1.

## Текущий статус

- **Сборочная среда: готова** (i386, `make -C docker -f build.mk hdimage`).
- **Этап 0.1: частично.** Ручная точка отсчёта есть (1 CPU). Не сделано: QEMU 11.1
  в Docker, автоматический прогон, прогон с 4 CPU (`CONFIG_SMP`), каркас тестов на
  хосте.
- Этапы 0.2–0.6 и далее: не начаты.

## Изменения, ожидающие коммита у владельца

Предлагаемые отдельные коммиты:
1. `kernel/i386: не останавливать CPU с IF=0, если прерывание пришло до hlt` —
   `minix/kernel/arch/i386/{klib.S,sconst.h,mpx.S,apic_asm.S}`;
2. `makefs: порядок mtime/atime в Rock Ridge TF, как в NetBSD trunk` —
   `usr.sbin/makefs/cd9660/iso9660_rrip.c` (после проверки `isofs`);
3. `docker: gcc-7 prerequisites по HTTPS` — `docker/Dockerfile`;
4. `docs: testing.md, handoff` — `docs/testing.md`, `docs/handoff.md`, `CLAUDE.md`.

## Замечено в коде (для будущей работы)

- `minix/servers/vm/alloc.c`: битовая карта жёстко на 4 ГБ, номера страниц `int`.
- `minix/servers/vm/region.h`: `phys_block.refcount` — `u8_t`.
- `minix/servers/vm/pt.h`: `pt_t` жёстко двухуровневый, на `u32_t`;
  `pagetable.c` — ~50 ветвлений `__i386__`/`__arm__`.
- `minix/kernel/spinlock.h`: простой спинлок + Big Kernel Lock; SMP только с
  `CONFIG_SMP` и только для i386.
- `minix/lib/libmthread/pthread_compat.c`: `pthread_mutex_trylock` рекурсивно
  вызывает сам себя.
- libc без `_REENTRANT`, без TLS, `_lwp_*` в `MISSING_SYSCALLS`.
- Таймер LAPIC — one-shot, перевзводится в `idle()`; сейчас i386 в QEMU работает
  через 8259 (IOAPIC замаскирован).

## Следующие шаги

1. **Владельцу:** пересобрать (`make -C docker -f build.mk hdimage`), загрузиться
   с DMA (пункт 6 меню, см. `docs/testing.md` §1), выполнить `./run -t isofs` и
   `for t in 43 71 74 79; do time ./run -t $t; done`, прислать журнал; затем коммиты (выше).
2. **Этап 0.1 (Claude):** QEMU 11.1.0 в `docker/Dockerfile` (из исходников);
   автоматический прогон: пункт `boot.cfg` с последовательной консолью и DMA,
   запуск `run -T` при загрузке, выключение, разбор TAP на хосте, цель
   `test-i386 SMP=1|4` в `docker/build.mk`; сборка с `CONFIG_SMP` и прогон на
   4 CPU (проверить `SKIP_IDLE_HLT` на AP).
3. Этап 0.2 — аудит 64-битных типов; 0.3 — `kyield`; 0.4–0.6 — dtc/DTS/libfdt
   (`docs/modernization.md`).
4. Открытые вопросы — `docs/modernization.md`, «Открытые вопросы».
5. Мелкие огрехи в `docs/architecture.md`: «form» вместо «fork» (§1), артефакт
   `vfs? / ipc/` и дублирующийся `MAKEDEV` в дереве §3.
