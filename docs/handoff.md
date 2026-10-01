# Handoff — снимок состояния

> Этот файл перезаписывается каждую сессию. Не храните здесь архитектурные решения —
> они идут в `docs/architecture.md` / `docs/porting.md` / `docs/modernization.md` /
> `docs/memory.md` / `docs/threads.md` / `docs/testing.md` / `docs/build-*.md` /
> `docs/docker-build.md`.

## Дата и контекст

2026-09-30 / 10-01. **Этап 0.1** (тестовая инфраструктура) — почти завершён.

Сделано в сессии (подробности — `docs/testing.md`, `docs/docker-build.md`):

1. Ручная точка отсчёта `minix/tests` на i386 — 100/101, после исправлений 101/101.
2. Исправлены ошибки (все проверены прогоном):
   - ядро i386: зависание в `halt_cpu` при прерывании в «тени» `sti` под KVM
     (`SKIP_IDLE_HLT`) — `testing.md` §4.1;
   - `makefs`: atime/mtime в Rock Ridge `TF` (из NetBSD trunk) — §4.2;
   - test43: гонка с `procfs`, тест не спускается в `procfs`, добавлена
     диагностика пути — §4.3.
3. **Автоматический прогон**: `make -C docker -f build.mk test-i386` — QEMU
   грузит ядро и модули через `-kernel`/`-initrd`, `rc.d/minixtests` по
   `testrun=1` запускает `run -T` и выключает машину, `docker/run-tests.sh`
   разбирает TAP. **Первый полный автоматический прогон: 101/101 PASS, ~24 мин.**
4. Docker: QEMU 11.1.0 из исходников (стадия `qemu-builder`); gcc-7
   prerequisites по HTTPS; `TOOLDIR=$OBJ/tooldir` (тулчейн больше не
   пересобирается после обновления ядра хоста); вариант `SMP=yes`
   (`obj/i386-smp`, `minix_x86_smp.img`).
5. Документация: `testing.md` (новый), `docker-build.md`, этот файл, `CLAUDE.md`.

## Доступ к среде

- Консоли на машине пользователя нет (`device_bash` не поддерживается в Claude
  Desktop для Ubuntu). Работа — через файловый мост; сборки и QEMU запускает
  владелец. Код — прямо в `master` (https://github.com/dmironoff/reminix).
- Подключены: `…/reminix` (проект) и `…/reminix(тупик)` (**только чтение**).
- **Правила для моста** (выяснено на практике):
  - мост иногда записывает **предыдущую** версию файла; размер при этом может и
    совпасть. После каждой записи — `device_stage_files` и **сравнение `md5sum`** с
    локальной версией; при расхождении записать ещё раз (`force: true`);
  - не записывать файл параллельно с его правкой в одном блоке вызовов;
  - файлы с именем `Makefile` мост не пишет (protected) — правку делает владелец
    (поэтому `docker/build.mk`, а не `docker/Makefile`).
- Журналы автоматических прогонов — `obj/test-logs/` (читать самому, не просить
  присылать). Отладка зависшего гостя — `docs/testing.md` §4.1 (gdbserver, дамп
  `proc[]`).

## Текущий статус

- Сборочная среда: готова. Автоматический прогон i386, 1 CPU: **готов, 101/101**.
- **Этап 0.1: не сделано** — сборка и прогон с `CONFIG_SMP` на 4 CPU; каркас
  тестов библиотек на хосте (понадобится к 0.6 и А2–А3).
- Этапы 0.2–0.6 и далее: не начаты.

## Изменения, ожидающие коммита у владельца

Все проверены полным автоматическим прогоном. Предлагаемые коммиты:
1. `test43: не обходить procfs, диагностика пути` — `minix/tests/test43.c`;
2. `docker: QEMU 11.1 из исходников, TOOLDIR в $OBJ/tooldir, SMP-вариант, test-i386` —
   `docker/Dockerfile`, `docker/build.mk`, `docker/run-tests.sh`,
   `docker/tests-known-failures.i386`;
3. `rc.d/minixtests: автоматический прогон тестов по testrun=1` —
   `etc/rc.d/minixtests`, `etc/rc.d/Makefile`, `distrib/sets/lists/minix-base/mi`;
4. `docs: автоматический прогон, test43, docker` — `docs/testing.md`,
   `docs/docker-build.md`, `docs/handoff.md`.

## Замечено в коде (для будущей работы)

- `minix/servers/vm/alloc.c`: битовая карта жёстко на 4 ГБ, номера страниц `int`.
- `minix/servers/vm/region.h`: `phys_block.refcount` — `u8_t`.
- `minix/servers/vm/pt.h`: `pt_t` жёстко двухуровневый, на `u32_t`;
  `pagetable.c` — ~50 ветвлений `__i386__`/`__arm__`.
- `minix/kernel/spinlock.h`: простой спинлок + BKL; SMP только с `CONFIG_SMP`.
- `minix/lib/libmthread/pthread_compat.c`: `pthread_mutex_trylock` рекурсивно
  вызывает сам себя.
- libc без `_REENTRANT`, без TLS, `_lwp_*` в `MISSING_SYSCALLS`.
- i386 в QEMU работает через 8259 (IOAPIC замаскирован); таймер LAPIC — one-shot.
- Тест 2, вероятно, оставляет после себя процессы (см. `testing.md` §4.3) —
  не проверено.

## Следующие шаги

1. **Владельцу:** коммиты (выше); на ночь — `make -C docker -f build.mk hdimage
   SMP=yes && make -C docker -f build.mk test-i386 SMP=yes CPUS=4` (первая сборка
   MINIX с `CONFIG_SMP` за много лет — может не собраться/не загрузиться, это и
   есть результат).
2. **Claude:** быстрая цель «пересобрать только ядро и серверы и запустить
   test-i386» (цикл минуты вместо часов) — до начала этапа А.
3. По результату SMP-прогона — разбор, затем закрыть 0.1 (каркас тестов на хосте).
4. Этап 0.2 — аудит 64-битных типов; 0.3 — `kyield`; 0.4–0.6 — dtc/DTS/libfdt.
5. Мелкие огрехи в `docs/architecture.md`: «form» вместо «fork» (§1), артефакт
   `vfs? / ipc/` и дублирующийся `MAKEDEV` в дереве §3.
