# ReMinix — Device Tree: DTS, `dtc`, libfdt (пункты 9–10 модернизации)

> Долгоживущий справочный документ к пунктам 9 и 10 `docs/modernization.md`.
> Фиксирует выбранные версии апстрима, то, как DTS, `dtc` и libfdt встроены в
> NetBSD (проверено по исходникам NetBSD-current, сентябрь 2026), и как это
> повторяется в ReMinix. При реализации шагов 0.4–0.6 возвращаться сюда.
> Обновлять при смене версий или решений.

## 1. Выбранные версии апстрима (2026-09-25)

Правило для всего проекта: **только апстрим, без форков, версия фиксируется
тегом** (и хэшем коммита там, где это важно). Смена версии — осознанное решение с
записью в этой таблице.

| Компонент | Версия | Откуда | Примечание |
|---|---|---|---|
| U-Boot | **v2026.07** (выбор владельца) | апстрим U-Boot | вышел в июле 2026; для эталонной платы — `orangepi_pc_plus_defconfig` |
| devicetree-rebasing (DTS) | **v7.2-dts** | `git.kernel.org/pub/scm/linux/kernel/git/devicetree/devicetree-rebasing.git` | последний стабильный тег на 2026-09-25 (v7.3 в стадии rc). См. предупреждение ниже |
| dtc + libfdt | **v1.8.1** | `git.kernel.org/pub/scm/utils/dtc/dtc.git` | последний тег выпуска |
| QEMU | **11.1.0** (11 августа 2026) | qemu.org | перед сборкой проверить, нет ли точечного выпуска 11.1.x. В пакетах Ubuntu такой версии нет, поэтому в Docker-образе QEMU собирается из исходников (нужны `-M orangepi-pc` и свежий i386) |

**Предупреждение по devicetree-rebasing.** В августе 2026 в репозитории
испортилась история: начиная с v7.2-rc1-dts, включая `master` и **v7.2-dts**,
репозиторий раздулся до ~2,5 ГБ вместо ~200 МБ. 23 августа 2026 все эти теги были
**перезаписаны** (force-push) исправленной историей. Поэтому при импорте:
- брать v7.2-dts после 23.08.2026 и записывать **хэш коммита** рядом с тегом;
- убедиться, что размер клона порядка 200 МБ.

## 2. Как это сделано в NetBSD (эталон для повторения)

### 2.1 Раскладка по дереву

| Путь в NetBSD | Что там | Роль |
|---|---|---|
| `sys/external/bsd/libfdt/dist/` | libfdt из выпуска `dtc` (BSD-2-Clause) | **единственная копия** libfdt: для ядра, host tools и userland |
| `external/gpl2/dtc/dist/` | `dtc` без каталога `libfdt` (GPL-2.0) | исходники компилятора и утилит |
| `external/gpl2/dtc/dtc2netbsd` | скрипт импорта | раскладывает выпуск `dtc`: `libfdt/` → `sys/external/bsd/libfdt/dist`, остальное → `external/gpl2/dtc/dist`; добавляет RCS-теги, чистит права |
| `external/gpl2/dtc/usr.bin/dtc/version_gen.h` | `#define DTC_VERSION "DTC x.y.z"` | обновляется вручную при импорте |
| `tools/libfdt/Makefile` | host-библиотека | `HOSTLIB=fdt`, исходники из `sys/external/bsd/libfdt/dist` через `Makefile.libfdt` (`LIBFDT_SRCS`), `bsd.hostlib.mk`, совместимость через `nbtool_config.h` |
| `tools/dtc/Makefile` | host tool | `HOSTPROGNAME=${_TOOL_PREFIX}dtc` (`nbdtc`), `HOST_SRCDIR=external/gpl2/dtc/usr.bin/dtc`, линкуется с `-lfdt` из объектного каталога `tools/libfdt` |
| `tools/Makefile` | порядок сборки tools | для `evbarm`, `evbmips`, `evbppc`, `riscv` и др.: `SUBDIR+= mkubootimage`, `libfdt .WAIT`, `dtc` |
| `share/mk/bsd.own.mk` | переменные | `TOOL_DTC=${TOOLDIR}/bin/${_TOOL_PREFIX}dtc`; `MKDTC` (ставить ли `dtc` в систему, по умолчанию да); `MKDTB` (собирать ли `.dtb`) — да для `earmv5/6/7*`, `aarch64*`, `riscv*`; `DTBDIR?=/boot/dtb` |
| `external/gpl2/Makefile` | userland | `.if ${MKDTC} != "no"` → `SUBDIR+= dtc` |
| `external/gpl2/dtc/lib/libfdt/Makefile` | libfdt для userland | `LIB=fdt`, **`LIBISPRIVATE=yes`** (в систему не ставится, только для сборки `dtc`) |
| `external/gpl2/dtc/usr.bin/dtc/Makefile` | `dtc` для системы | `PROG=dtc`, исходники `DTC_SRCS` из `Makefile.dtc` + `dtc-lexer.l`, `dtc-parser.y` (`YFLAGS+=-d -L`), `-DNO_YAML`. Ставится **только `dtc`**, без `fdtdump`/`fdtget`/`fdtput`/`fdtoverlay` |
| `sys/external/gpl2/dts/dist/` | DTS в раскладке Linux: `arch/<arch>/boot/dts/<vendor>/…`, `include/dt-bindings` | исходники `.dts`/`.dtsi` |
| `sys/external/gpl2/dts/README` | лицензионное правило | заголовки там GPL-2.0 или GPL/X11; **`dt-bindings` использовать только для сборки `.dtb`, никогда не включать в исходники ядра** |
| `share/mk/bsd.dtb.mk` | правила сборки `.dtb` | см. 2.2 |
| `sys/dtb/Makefile`, `sys/dtb/{arm,arm64,riscv}/<vendor>/Makefile` | что собирать | по `MACHINE_ARCH` выбирается архитектура, по вендорам — подкаталоги; `sys/Makefile`: `.if ${MKDTB} != "no"` → `SUBDIR+=dtb` |
| `distrib/sets/lists/dtb/ad.<arch>` | списки наборов установки | генерируются `make update-sets` в `sys/dtb` |
| `sys/external/bsd/libfdt/conf/files.libfdt` | libfdt в ядре | `config(1)`: `define libfdt`, `CPPFLAGS+=-I$S/external/bsd/libfdt/dist`, список `fdt*.c` |
| `sys/external/bsd/libfdt/dist/libfdt_env.h` | **локальный патч** | ветка `#if defined(_KERNEL) \|\| defined(_STANDALONE)`: `<sys/param.h>`, `<sys/types.h>`, `<lib/libkern/libkern.h>` вместо libc |

### 2.2 Сборка `.dtb` (`share/mk/bsd.dtb.mk`)

- В объектном каталоге создаются ссылки `dts/<arch>` → `…/dts/dist/arch/<arch>/boot/dts`
  (для `DTSGNUARCH`, например `arm arm64 riscv`: DTS для arm32 ссылаются на
  arm64-файлы, и наоборот).
- Пути поиска: `DTSINC` (`dist/include`), `DTSDIR` — собственные DTS NetBSD в
  `sys/arch/<arch>/dts` плюс каталоги апстрима.
- Правило (препроцессор, затем `dtc`):
  ```
  ${CPP} -P -xassembler-with-cpp -I<пути> -include <файл.dts> /dev/null | \
  ${TOOL_DTC} -i<пути> -I dts -O dtb -p 1024 -b 0 -@ -o <файл.dtb>
  ```
  `-p 1024` оставляет место для правок загрузчиком (U-Boot дописывает `/chosen`,
  `/memory`, `/psci`), `-@` сохраняет символы для overlay.
- Список `.dtb` вендора берётся из Makefile апстрима: `make -C <dts/vendor>
  CONFIG_MACH_SUN8I=y … -v dtb-y`. Дополнительно — свои `.dts` NetBSD.
- Установка в `/boot/dtb/<vendor>/`; цель `dtblist` печатает строки для списков
  наборов.

## 3. Как повторяем в ReMinix

Раскладку берём **как в NetBSD**, чтобы можно было сверяться с ней и переносить
исправления. `sys/external/` в дереве ReMinix уже есть, в `tools/Makefile` уже
есть блок `evbarm` с `mkubootimage`.

### 3.1 Отличия от NetBSD (решения владельца и особенности MINIX)

1. **libfdt ставится в систему публично**: без `LIBISPRIVATE`, `libfdt.a` в
   `/usr/lib`, заголовки `fdt.h`, `libfdt.h`, `libfdt_env.h` в `/usr/include`,
   записи в списках наборов (`base`/`comp`). Нужна утилитам и будущим драйверам.
2. **В систему ставятся все утилиты**, а не только `dtc`: `dtc`, `fdtdump`,
   `fdtget`, `fdtput`, `fdtoverlay` (исходники — `Makefile.utils` из
   `external/gpl2/dtc/dist`, каждая = свой `.c` + `util.c`). Каталоги
   `external/gpl2/dtc/usr.bin/{dtc,fdtdump,fdtget,fdtput,fdtoverlay}`. `MKDTC=yes`
   на **всех** архитектурах, включая i386. `-DNO_YAML` (libyaml нет).
3. **Host tool `dtc` и `.dtb`** — только для архитектур с FDT (как `MKDTB` в
   NetBSD): сейчас `evbearm`, потом aarch64, riscv64, mips64.
4. **Источник DTS — devicetree-rebasing**, а не дерево Linux. Его раскладка —
   `src/<arch>/<vendor>/…` и `include/`. Вариант по умолчанию: импортировать
   как есть в `sys/external/gpl2/dts/dist` и поправить пути в нашем
   `bsd.dtb.mk` (`…/dist/src/<arch>` вместо `…/dist/arch/<arch>/boot/dts`).
   Проверить при импорте, есть ли в devicetree-rebasing Makefile-ы каталогов
   вендоров для приёма `make -v dtb-y`. Если нет — списки `.dtb` задаются явно в
   `sys/dtb/<arch>/<vendor>/Makefile`; для начала там одна плата —
   `sun8i-h3-orangepi-pc-plus.dtb`.
5. **Собственные DTS ReMinix** (тонкие `.dts` поверх апстримных) — по соглашению
   NetBSD в `sys/arch/<arch>/dts/`, например `sys/arch/arm/dts/`.
6. **Ядро собирается не `config(1)`, а Makefile-ами MINIX.** В
   `minix/kernel/arch/earm/Makefile.inc`:
   - `.PATH: ${NETBSDSRCDIR}/sys/external/bsd/libfdt/dist`;
   - в сборку только часть для чтения: `fdt.c`, `fdt_ro.c`, `fdt_addresses.c`,
     `fdt_strerror.c`;
   - эти объекты — unpaged, тем же механизмом, что уже есть в
     `Makefile.inc` (`objcopy --prefix-symbols=__k_unpaged_`, списки
     `*_OBJS_UNPAGED`). Нужные строковые функции берутся из уже имеющихся
     unpaged-объектов `minc` (при необходимости список расширяется);
   - `libfdt_env.h`: локальный патч по образцу NetBSD — ветка для ядра MINIX
     (без libc, типы и строковые функции из окружения ядра). Патч вносится в
     `dist` (файл подключается через `#include "…"`, поэтому подменить его
     порядком `-I` нельзя) и **перечисляется в заметках импорта** (3.3), чтобы
     его повторить при обновлении;
   - на i386 libfdt в ядро не собирается.
7. **Доставка `.dtb`**: собранный `.dtb` платы кладётся в fitImage (узел `fdt`,
   `docs/modernization.md` п. 6) и дополнительно ставится в `/boot/dtb/<vendor>/`.
   Сам fitImage собирается `mkimage` из U-Boot v2026.07 (`make tools-only`) —
   NetBSD-шный `mkubootimage` fitImage не делает.

### 3.2 Шаги и проверки (этап 0 плана)

| Шаг | Что сделать | Проверка |
|---|---|---|
| 0.4 (**выполнено 2026-10-03**, 3.4) | импорт `dtc` v1.8.1 (3.3); `tools/libfdt`, `tools/dtc`; `TOOL_DTC`, `MKDTC`, `MKDTB` в `bsd.own.mk`; `external/gpl2/dtc/lib/libfdt` (публично) и пять утилит в `usr.bin`; списки наборов | `build.sh tools` собирает `nbdtc`; в образе i386 работают `dtc -v`, `fdtdump`, `fdtget` на тестовом `.dtb` |
| 0.5 (**выполнено 2026-10-03**, 3.5) | импорт devicetree-rebasing v7.2-dts целиком (3.3); `bsd.dtb.mk` с путями под его раскладку; `sys/dtb/arm/allwinner` со списком из `sun8i-h3-orangepi-pc-plus.dtb` | сборка `evbearm` даёт `.dtb`; `fdtdump` показывает `compatible = "xunlong,orangepi-pc-plus"`, узел `/cpus`. Узла `/memory` в апстримных DTS sunxi нет — его добавляет U-Boot (место — `-p 1024`) |
| 0.6 | libfdt в `minix/kernel/arch/earm/Makefile.inc` (unpaged) + патч `libfdt_env.h`; тесты на хосте, разбирающие `.dtb` из 0.5 | ядро earm собирается и линкуется; ядро i386 не меняется; тесты на хосте проходят |

### 3.3 Процедура импорта (повторяемая)

**dtc / libfdt** (выполнено 2026-10-03 для v1.8.1, коммит апстрима
`8f48565e5cfedc74d3f7512f1e0188e9d85dc1de`):
1. Скачать выпуск: `git clone --depth 1 --branch v1.8.1
   https://git.kernel.org/pub/scm/utils/dtc/dtc.git /tmp/dtc`.
2. Запустить `external/gpl2/dtc/dtc2reminix /tmp/dtc .` в Docker-образе сборки
   (нужны bison и flex; контейнер — от своего пользователя). Скрипт (по образцу
   `dtc2netbsd`, без RCS-тегов и `cvs import`):
   - `libfdt/` → `sys/external/bsd/libfdt/dist`, остальное →
     `external/gpl2/dtc/dist`;
   - **не импортирует** `.git`, `tests/` и `AGENTS.md`/`CLAUDE.md` — правила апстрима
     для ИИ-ассистентов, которые Claude Code принял бы за инструкции нашего
     репозитория (там, например, требование строк `Assisted-by`);
   - генерирует парсер и лексер `dtc` апстримными bison и flex в
     `external/gpl2/dtc/usr.bin/dtc/` (`dtc-parser.tab.{c,h}`, `dtc-lexer.lex.c`):
     `dtc-parser.y` использует `%locations`, а byacc дерева (20141128) реализует
     его с ошибками даже со скелетом btyacc (`-B`). NetBSD вместо этого правит
     `dtc-parser.y`/`dtc-lexer.l` в `dist`.
3. Обновить `external/gpl2/dtc/usr.bin/dtc/version_gen.h`:
   `#define DTC_VERSION "DTC 1.8.1"`.
4. Повторно наложить локальные патчи `dist` (список ведётся здесь):
   - пока нет. `Makefile.dtc` написан для GNU make (`ifneq`), поэтому наш
     `usr.bin/dtc/Makefile` его не подключает, а перечисляет `DTC_SRCS` явно —
     при обновлении сверить список; NetBSD правит `Makefile.dtc`;
   - с шага 0.6: `sys/external/bsd/libfdt/dist/libfdt_env.h` — ветка для ядра
     MINIX (3.1 п. 6).
5. Один коммит: «Import dtc 1.8.1».

**DTS** (выполнено 2026-10-03: тег `v7.2-dts`, объект тега `41930a1e…`, коммит
`1826bba6fb1c8c0b6d845f8308590dcfd1f7f859` от 23.08.2026 — исправленная история;
дата в объекте тега — 16.08.2026, тег при перезаписи сохранили):
1. Взять devicetree-rebasing на теге `v7.2-dts` (после 23.08.2026), записать
   хэш коммита, проверить размер (~200 МБ полный клон; неглубокий клон
   `--depth 1` — `.git` 19 МБ, дерево 106 МБ, 14 176 файлов).
2. Скопировать дерево без `.git` в `sys/external/gpl2/dts/dist` (`AGENTS.md`/
   `CLAUDE.md` в нём нет; проверять при каждом импорте).
3. Записать тег и хэш в `sys/external/gpl2/dts/README` (дополнив лицензионное
   правило NetBSD про `dt-bindings`).
4. Один коммит: «Import devicetree-rebasing v7.2-dts (<хэш>)».

### 3.4 Состояние после шага 0.4 (2026-10-03)

| Что | Где | Отличие от NetBSD |
|---|---|---|
| libfdt (BSD-2-Clause) | `sys/external/bsd/libfdt/dist` | — |
| dtc и утилиты (GPL-2.0) | `external/gpl2/dtc/dist` | без `tests/`, `AGENTS.md`, `CLAUDE.md` |
| скрипт импорта | `external/gpl2/dtc/dtc2reminix` | вместо `dtc2netbsd`; генерирует парсер и лексер |
| парсер и лексер `dtc` | `external/gpl2/dtc/usr.bin/dtc/dtc-{parser.tab,lexer.lex}.*` | сгенерированы bison/flex при импорте; NetBSD правит `.y`/`.l` |
| libfdt в системе | `external/gpl2/dtc/lib/libfdt`: `libfdt.a`, `libfdt_pic.a`, `fdt.h`, `libfdt.h`, `libfdt_env.h` | публично (NetBSD — `LIBISPRIVATE`) |
| утилиты в системе | `external/gpl2/dtc/usr.bin/{dtc,fdtdump,fdtget,fdtput,fdtoverlay}` → `/usr/bin` | все пять (NetBSD — только `dtc`); утилиты линкуются с libfdt из её объектного каталога |
| host tool | `tools/libfdt`, `tools/dtc` → `nbdtc` (версии из ветки netbsd-8: без `bsd.hostinit.mk`) | — |
| переменные | `bsd.own.mk`: `TOOL_DTC`; `MKDTC` (да везде); `MKDTB` (да для `earm*`, `aarch64*`, `riscv*`, `mips64*`) | `MKDTB` пока включает только host tool; `.dtb` — шаг 0.5 |
| подключение | `external/Makefile` += `gpl2`; `external/gpl2/Makefile`: `dtc` при `MKDTC`, NetBSD-ные `xcvs`/`lvm2` выключены на MINIX; `tools/Makefile`: `libfdt .WAIT dtc` при `MKDTB` | — |

Проверка: полная сборка i386 и earm (`checkflist` чист), `build.sh tools` для
earm собирает `nbdtc` с нуля, тест `minix/tests/testfdt.sh` (`fdt` в
`minix/tests/run`): `dtc -v`, компиляция `.dts` с `-p 1024 -@`, `fdtdump`,
`fdtget`, `fdtput`, `fdtoverlay`, обратно в `.dts` и снова в `.dtb` — PASS в
QEMU i386. Тест можно прогнать и на хосте, собрав утилиты gcc из `dist`.

### 3.5 Состояние после шага 0.5 (2026-10-03)

| Что | Где |
|---|---|
| DTS апстрима | `sys/external/gpl2/dts/dist` (devicetree-rebasing v7.2-dts целиком, без изменений), `sys/external/gpl2/dts/README` — тег, хэш, лицензионное правило |
| правила сборки | `share/mk/bsd.dtb.mk`: по образцу NetBSD, пути `dist/src/<arch>` вместо `dist/arch/<arch>/boot/dts`; ссылки `dts/<arch>` в объектном каталоге; список `.dtb` — явный `DTS` (у devicetree-rebasing нет `Makefile` вендоров), без логики overlay NetBSD; `-p 1024 -b 0 -@` |
| что собирать | `sys/dtb/Makefile` (`earm*` → `arm`), `sys/dtb/arm/Makefile.inc` (`DTSARCH=arm`, `DTSGNUARCH=arm arm64 riscv`), `sys/dtb/arm/allwinner/Makefile` (`sun8i-h3-orangepi-pc-plus.dts`) |
| подключение | `sys/Makefile`: `SUBDIR+=dtb` при `MKDTB`; `bsd.own.mk`: `DTBDIR=/boot/dtb`, `DTBOWN`, `DTBGRP`, `DTBMODE` |
| установка | `/boot/dtb/allwinner/sun8i-h3-orangepi-pc-plus.dtb`; каталоги — `etc/mtree/NetBSD.dist.earm`, файлы — `distrib/sets/lists/minix-base/md.evbarm` (`make dtblist` в `sys/dtb` печатает строки) |

Проверка: `.dtb` (31 362 байта) — `compatible = "xunlong,orangepi-pc-plus",
"allwinner,sun8i-h3"`, модель «Xunlong Orange Pi PC Plus», `/cpus` с четырьмя
ядрами, `__symbols__` (для overlay); `/memory` нет — добавит U-Boot. Полная
сборка earm и i386 — `checkflist` чист. На плате и в QEMU `orangepi-pc` `.dtb`
пока не используется (шаги Б1/Б2).

## 4. Что дальше (вне этапа 0)

- **Б2:** разбор FDT в `pre_init` (`/memory`, `/reserved-memory`, `/chosen`,
  `/cpus`, `/psci`), заполнение bootmem (`docs/memory.md` §3.3).
- **Kernel call выдачи FDT** привилегированным процессам (RS, драйверы) и поиск
  устройств драйверами по `compatible` — проектируется вместе с переводом
  драйверов на Device Tree.
- При переходе на hard-float (открытый вопрос в `docs/modernization.md`) —
  проверить значения `MKDTB` для новых `MACHINE_ARCH`.

## 5. Источники

- NetBSD-current, исходники: `tools/dtc/Makefile`, `tools/libfdt/Makefile`,
  `tools/Makefile`, `share/mk/bsd.own.mk`, `share/mk/bsd.dtb.mk`,
  `external/gpl2/dtc/*`, `sys/external/bsd/libfdt/*`, `sys/external/gpl2/dts/README`,
  `sys/dtb/*` — https://github.com/NetBSD/src
- dtc: https://git.kernel.org/pub/scm/utils/dtc/dtc.git (теги до v1.8.1)
- devicetree-rebasing, инцидент с историей (август 2026):
  https://ratatoskr.run/linux-devicetree/2026/08/17448198/t
- QEMU 11.1.0: https://www.qemu.org/2026/08/11/qemu-11-1-0/
- U-Boot v2026.07: https://ratatoskr.run/u-boot/2026/07/17222016
