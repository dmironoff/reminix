# Сборка ReMinix для x86 (i386)

> Пошаговое описание процесса сборки и создания загрузочных образов для архитектуры
> **i386**, реконструированное по содержимому `releasetools/` (`Makefile` верхнего
> уровня, `build.sh`, `releasetools/{Makefile,image.defaults,image.functions,
> x86_hdimage.sh,x86_cdimage.sh,x86_usbimage.sh,x86_ramimage.sh,mkboot}`). Это
> единственная архитектура, для которой пайплайн проверен и завершён от начала до
> конца (жёсткий диск / CD / USB / RAM-образы). См. также `docs/architecture.md`
> §5–7 и `docs/build-arm32.md` для сравнения с ARM-пайплайном.
>
> Всё, что описано ниже, можно прогнать и без ручной настройки хоста — см.
> `docs/docker-build.md` (`make -C docker -f build.mk hdimage`).

## 1. Общая схема

```
build.sh -m i386 ...           # 1. кросс-тулчейн + полная сборка мира
        │
        ▼
make build  (корневой Makefile) # включает minix/ последним подкаталогом
        │
        ▼
releasetools/Makefile: make hdboot
        │  ├─ services  (kernel + servers + drivers + sbin)
        │  └─ do-hdboot (упаковка kernel+модулей в /boot/minix/<версия>)
        │
        ▼
releasetools/x86_{hd,cd,usb,ram}image.sh  # создание конкретного образа диска/ISO
```

Ключевая деталь: **`make build`/`make hdboot` устанавливают систему в `DESTDIR`**
(корень будущей ФС), а отдельный набор скриптов `x86_*image.sh` уже **упаковывает
`DESTDIR` в конкретный формат образа** (жёсткий диск, CD, USB, RAM-диск). Это две
разные стадии, и `x86_*image.sh` умеют сами вызвать `build.sh`, если образ ещё не
собирался (см. §3).

## 2. Переменные архитектуры

Во всех `x86_*image.sh`:

```sh
: ${ARCH=i386}
: ${OBJ=../obj.${ARCH}}
: ${TOOLCHAIN_TRIPLET=i586-elf32-minix-}
: ${BUILDSH=build.sh}
```

- `ARCH=i386` — передаётся в `build.sh -m i386`. Важно: `i386` — стандартное значение
  `MACHINE` из общей (немодифицированной под MINIX) таблицы `valid_MACHINE_ARCH`
  внутри `build.sh` — то есть само `build.sh` не содержит MINIX-специфичных
  ограничений на список архитектур (см. `docs/porting.md`, замечание о `build.sh`).
  Реальное ограничение — в наличии `minix/kernel/arch/i386/` и т.п.
- `TOOLCHAIN_TRIPLET=i586-elf32-minix-` — префикс кросс-инструментов
  (`${TOOLCHAIN_TRIPLET}objcopy`, `...strip`, `...clang` и т.д.), которые лежат в
  `${OBJ}/tooldir.<хост>/bin/` после сборки тулчейна.
- `OBJ=../obj.i386` — объектная директория сборки (создаётся `build.sh`), рядом с ней
  `DESTDIR=${OBJ}/destdir.i386` — куда устанавливается собранная система.

## 3. Стадия 1 — сборка системы (`build.sh` → `make build`)

Вызывается либо вручную (`sh build.sh -j N -m i386 -O ../obj.i386 -D ../obj.i386/destdir.i386 -U -u release`),
либо автоматически из `releasetools/image.functions` (функция, выполняющаяся в
каждом `x86_*image.sh`, если `CREATE_IMAGE_ONLY` не установлен в `1`):

```sh
sh ${BUILDSH} -j ${JOBS} -m ${ARCH} -O ${OBJ} -D ${DESTDIR} ${BUILDVARS} -U -u release
```

Флаги:
- `-m i386` — целевая архитектура.
- `-O`, `-D` — объектная директория и DESTDIR.
- `-U` — сборка непривилегированным пользователем (`MKUNPRIVED`), обычная практика
  для CI/контейнеров.
- `-u` — не чистить перед сборкой (`MKUPDATE`, инкрементальная сборка).
- `release` — целевой make-таргет (`make release`, см. `docs/architecture.md` §5,
  соответствует корневому `Makefile`: `distribution` + генерация `sets`).

По итогам этой стадии: полностью собранная и установленная в `DESTDIR` система
(ядро, серверы, драйверы, юзерленд), плюс tar-архивы наборов (`sets`).

> **Важно про `RELEASEDIR` (проверено на реальной сборке, было наступлено на
> практике)**: `releasetools/image.defaults` определяет `RELEASEDIR` как
> `${OBJ}/releasedir/${ARCH}/binary` — но это **не** то значение, которое нужно
> передавать/экспортировать в `build.sh`. Сам `build.sh` (см. `-R release
> Set RELEASEDIR to release. [Default: releasedir]` в его `usage()`, и код
> release-таргета: `setdir=${RELEASEDIR}/${RELEASEMACHINEDIR}/binary/sets`, где
> `RELEASEMACHINEDIR` — просто имя архитектуры, `i386`) **сам** дописывает
> `/${ARCH}/binary/sets` поверх `RELEASEDIR`. Если `RELEASEDIR` уже передан
> "с суффиксом" (как в дефолте `image.defaults`) — что как раз происходит, если
> эта переменная **экспортирована** в окружение перед вызовом
> `releasetools/x86_hdimage.sh` (обычная переменная shell, назначенная через
> `: ${VAR=...}`, экспортированной автоматически не становится, но `docker run
> -e RELEASEDIR=...` или явный `export RELEASEDIR=...` в оболочке — экспортирует)
> — `build.sh` удваивает суффикс: наборы оказываются в
> `${OBJ}/releasedir/i386/binary/i386/binary/sets/`, а не там, где их ищет тот же
> `image.defaults` через **отдельную**, не связанную с `RELEASEDIR` переменную
> `SETS_DIR=${OBJ}/releasedir/${ARCH}/binary/sets`. `releasetools/image.functions`
> в этом случае падает на `cd .../releasedir/i386/binary/sets: No such file or
> directory`, хотя сама сборка (`make release`) отработала полностью успешно.
>
> Правильное значение, которое нужно **экспортировать** в окружение перед
> запуском `x86_hdimage.sh` (или передавать `build.sh` напрямую как `-R`):
> `RELEASEDIR=${OBJ}/releasedir` — **без** `/${ARCH}/binary`. Тогда
> `build.sh` сам допишет `/${ARCH}/binary/sets`, и итоговый путь совпадёт с тем,
> что ожидает `SETS_DIR`. См. `docker/build.mk` (`CONTAINER_RELDIR`) — там этот
> нюанс учтён и подробно прокомментирован.

Также именно на этом этапе (через `releasetools/Makefile`, таргет `hdboot`, который
`make build`/`make release` вызывает как `${MAKEDIRTARGET} releasetools do-hdboot`,
см. `docs/architecture.md` §5, шаг «releasetools do-hdboot») формируется набор
загружаемых модулей ядра в `${DESTDIR}/boot/minix/.temp/`:

```
releasetools/Makefile: do-hdboot
    mod01_ds, mod02_rs, mod03_pm, mod04_sched, mod05_vfs,
    mod06_memory, mod07_tty, mod08_mib, mod09_vm,
    mod10_pfs, mod11_mfs, mod12_init   + kernel
```

Порядок в списке `PROGRAMS` (`releasetools/Makefile`) — это **порядок загрузки
модулей мультизагрузчиком** (multiboot), файлы переименовываются с префиксом
`modNN_<имя>` именно для сохранения алфавитного = загрузочного порядка, затем
сжимаются `gzip`, после чего `mkboot hdboot ${DESTDIR}` (см. `releasetools/mkboot`)
кладёт их в `${DESTDIR}/boot/minix/<версия>[rN][-gitrev]/` и обновляет симлинк
`boot/minix_latest`, с ротацией старых версий (`rotate_oldest`, хранится не более
3 последних загрузочных наборов).

## 4. Стадия 2 — упаковка в конкретный образ

### 4.1 `x86_hdimage.sh` — образ жёсткого диска (основной, для установки/разработки)

Создаёт **один файл-образ** (`minix_x86.img` по умолчанию) с разметкой:

| Раздел | Размер по умолчанию | ФС | Точка монтирования |
|---|---|---|---|
| boot-сектор (`bootxx_minixfs3`) | 32 сектора (`BOOTXX_SECS`) | — | загрузчик первой стадии |
| root | 128 МБ − boot | MFS | `/` |
| usr | 1792 МБ | MFS | `/usr` |
| home | 128 МБ | MFS | `/home` |
| EFI (опционально, `EFI_SIZE>0`) | — | FAT32 | `/boot/efi` (GRUB EFI, см. §4.4) |

Шаги внутри скрипта:
1. `build_workdir "$SETS"` (`image.functions`) — распаковывает наборы (`minix-base
   minix-comp minix-games minix-man minix-tests tests` по умолчанию) во временный
   `ROOT_DIR`, генерирует `master.passwd`, mtree-спецификации.
2. `workdir_add_hdd_files` — прописывает `/etc/fstab` под 3 MFS-раздела на
   `/dev/c0d0p{1,2,3}` + `devman`/`ptyfs`.
3. Добавляются **два комплекта ядра+модулей**: `minix_default` (используется при
   каждой загрузке) и `minix/<версия>` (конкретный релиз) — оба берутся из
   `${DESTDIR}/boot/minix/.temp` функцией `workdir_add_kernel`.
4. Генерируется `boot.cfg` — меню загрузчика MINIX (обычная загрузка, "latest",
   single-user, вариант для встраиваемых плат ALIX с серийной консолью
   `console=tty00 consdev=com0`, и т.д.).
5. `create_input_spec` + `create_protos "usr home"` — строят mtree-спецификацию и
   `.proto`-файлы для `nbmkfs.mfs` (по одному на каждый будущий раздел).
6. Три вызова `nbmkfs.mfs` создают образы `root`/`usr`/`home` **последовательно
   внутри одного файла** (`${IMG}`) со смещениями, вычисленными по факту
   предыдущего раздела.
7. `nbpartition -m ${IMG} ...` записывает MBR-таблицу разделов инструментом,
   собранным нативно под MINIX (`nbpartition`).
8. `nbinstallboot -f -m i386 ${IMG} .../bootxx_minixfs3` — устанавливает
   загрузчик первой стадии в boot-сектор.

Готовый образ запускается так, как подсказывает сам скрипт в конце:
```
qemu-system-i386 --enable-kvm -m 256 -hda minix_x86.img
```

### 4.2 EFI-вариант (`EFI_SIZE >= 512`)

Отдельная ветка: `fetch_and_build_grub` клонирует и собирает **GRUB** (конкретный
зафиксированный коммит апстрима, платформа `--with-platform=efi --target=i386`),
собирает `booti386.efi`, генерирует `grub.cfg` с двумя пунктами меню (обычная
загрузка и с serial-консолью), кладёт всё в FAT32-раздел `EF:1+`. Это единственное
место во всём `releasetools/`, где явно фигурирует **UEFI**-путь загрузки — и он
существует только для i386/x86; для остальных архитектур такого готового решения
нет (важно для `docs/porting.md`, если для amd64 тоже понадобится UEFI).

### 4.3 `x86_cdimage.sh` — загрузочный ISO (инсталляционный CD)

Отличия от hdimage: один плоский набор файлов (без разбиения root/usr/home —
`create_protos` без аргументов), `SETS="minix-base"`, `BUNDLE_SETS=1` (все `.tgz`
наборов кладутся на сам ISO, чтобы инсталлятор мог их развернуть на целевой диск —
см. `workdir_add_sets` в `image.functions`), собирается через
`nbmakefs -t cd9660 ... bootimage=i386;.../bootxx_cd9660,label=MINIX`.

### 4.4 `x86_usbimage.sh` и `x86_ramimage.sh` — образ с RAM-диском

Оба используют `workdir_add_ramdisk_files` + `create_ramdisk_image` — весь корень
пакуется в **один MFS-образ, вкомпилированный в память** (превращается через
`objcopy -Ibinary ... -Oi586-elf32-minix imgrd.mfs imgrd.o` и линкуется прямо в
драйвер `memory` как `mod06_memory`), а не в отдельные разделы на диске. Разница
между ними: `x86_usbimage.sh` дополнительно заворачивает результат в
загружаемый образ с MBR/бутсектором (для записи на настоящую флешку/диск),
`x86_ramimage.sh` просто оставляет файлы в `${WORK_DIR}` для передачи QEMU через
`-kernel`/`-initrd` без создания образа диска вообще.

### 4.5 Сводка вариантов, консоль и SMP в `boot.cfg` (ReMinix)

Все четыре скрипта — `image.defaults` + `image.functions`: `build_workdir`
(распаковка наборов), `create_input_spec`/`create_protos`, без
`CREATE_IMAGE_ONLY=1` — сначала `build.sh … release`.

| | hd | cd | usb | ram |
|---|---|---|---|---|
| наборы (`SETS`) | base, comp, games, man, minix-tests, tests | base + все `.tgz` для установщика (`BUNDLE_SETS=1`) | base | base |
| корень | MFS `c0d0p0`, отдельные `/usr`, `/home` (128 МБ / 1792 МБ / 128 МБ) | ISO9660 (Rock Ridge), только чтение; `cdproberoot` ищет CD (`cdprobe`), `/var`, `/tmp`, `/usr/run`, `/root` — RAM-диски (`/etc/rc.cd`) | MFS `imgrd`, вшит в `mod06_memory`, `bootramdisk=1` | то же, что usb |
| загрузчик | `bootxx_minixfs3` → boot monitor | `bootxx_cd9660` → boot monitor (`/minixboot`) | `bootxx_minixfs3` → boot monitor, в разделе `kernel`, `mod*` | нет (`-kernel/-initrd`) |
| ядро | `minix_default` и `minix/<версия>` | `minix_default`, модули — `load=` по одному | `/kernel`, `/mod*` | `$WORK_DIR/kernel`, `mod*` |
| пункты меню | обычный, latest, single user, ALIX (COM1), ⟨SMP: one CPU⟩ | обычный, ⟨SMP: one CPU⟩, AHCI | обычный, ⟨SMP: one CPU⟩ | — |
| размер | 2 ГБ | ~540 МБ (base ~290 МБ + наборы ~235 МБ) | ~355 МБ, гостю нужно 2 ГБ | то же |

**Переменные `boot.cfg`** (`image.defaults`, вспомогательные функции —
`bootcfg_*` в `image.functions`); пустые — `boot.cfg` байт в байт как в MINIX:

- `BOOT_CONSOLE=com0` — `consdev=com0` первой строкой (меню и приглашение
  загрузчика на COM1, ввод оттуда же) и `console=tty00 consdev=com0` в каждом
  пункте (консоль MINIX — tty00). Пункт ALIX свою консоль задаёт сам.
- `BOOT_SMP=yes` — пункт «…, one CPU (no_apic=1)» после основного. По
  умолчанию `yes`, если в `BUILDVARS` есть `CONFIG_SMP=y`.

**SMP-ядро включает все CPU само**: без параметра `no_apic` оно ставит
`no_apic=0` (`minix/kernel/arch/i386/pre_init.c`, `get_parameters()`, до
разбора командной строки). Значение кладётся в параметры загрузки, а не только
в `config_no_apic`: по `sysenv no_apic` = 0 RAM-диск (`ramdisk/rc`) запускает
ACPI, без которого PCI в режиме APIC паникует. `no_apic=1` (пункт меню или
`KARGS`) — один CPU на PIC. Однопроцессорное ядро — по-прежнему `no_apic=1` по
умолчанию (`main.c`).

**Без `size=` в спецификации** (`create_input_spec`, `nbmtree -R size`,
2026-10-05). Списки наборов берут размер из `METALOG`, а тот хранит размер
первой установки файла: после инкрементального `build.sh -u`, поменявшего
`/etc/system.conf`, в спецификации оставался старый `size=8691` при файле
в 9898 байт. `nbmakefs -t cd9660` доверяет `size=` и обрезал файл — на ISO
`system.conf` обрывался («parse error at 'EOF'»), не поднимались RAM-диски и
службы. `nbtoproto` (образы MFS) этот ключ никогда не использовал, поэтому
hd-образ ошибки не показывал.

**`WORK_DIR`.** Скрипты стирают `$WORK_DIR` (по умолчанию `$OBJ/work`) целиком.
Docker-обёртка даёт cd/usb/ram свои `$OBJ/work-cd`, `work-usb`, `work-ram`:
`$OBJ/work` остаётся за `hdimage`, из него `quick` освежает образ
(`docs/docker-build.md` §4).

Через Docker (`docs/docker-build.md` §4):

| цель | файл | параметры |
|---|---|---|
| `hdimage` | `minix_x86[_smp][_serial].img` | `SMP CONSOLE PACK_ONLY` |
| `cdimage` | `minix_x86[_smp][_live][_serial].iso` | `SMP CONSOLE ISO_SETS PACK_ONLY` |
| `usbimage` | `minix_x86[_smp][_serial]_usb.img` | `SMP CONSOLE PACK_ONLY` |
| `ramimage` | `obj/i386[-smp]/work-ram/{kernel,mod*}` | `SMP PACK_ONLY` |

`CONSOLE=serial` → `BOOT_CONSOLE=com0`; `ISO_SETS=live` → `SETS="minix-base
minix-comp minix-games minix-man" BUNDLE_SETS=0` (без установщика; тесты на ISO
не кладутся — корень только для чтения, а `minix/tests` пишут в
`/usr/tests`); `PACK_ONLY=yes` → `CREATE_IMAGE_ONLY=1` (наборы не обновляются:
ядро на образ берётся из `minix-kernel.tgz`, `quick` его не пересобирает).
Запуск и проверка: `run-i386 MEDIA=hd|cd|usb|ram`, `boot-test MEDIA=…`
(`docs/testing.md` §1в, §1е).

## 5. Устаревший альтернативный путь — `release.sh`

В каталоге также лежит **более старый** скрипт `release.sh` + `release.functions` —
самостоятельный процесс релиза (клонирует исходники по git, собирает через
`make distribution`, монтирует RAM-диски (`/dev/ram0`, `/dev/ram1`) **на самой
MINIX-системе, на которой выполняется сборка** (`installboot_nbsd`, `ramdisk`,
`synctree`, `fitfs` — команды, специфичные для MINIX как хоста сборки, не для
кросс-сборки с Linux/NetBSD-хоста). Судя по стилю и отсутствию интеграции с
`image.functions`/`image.defaults`, это **более ранний, вероятно unmaintained**
способ сборки релизного ISO, предшествующий текущему `x86_cdimage.sh`. Для
современной кросс-сборки (в т.ч. в этой среде разработки) актуален путь через
`x86_*image.sh`, а не `release.sh`. Стоит явно решить (и отметить в
`docs/handoff.md`/здесь), держим ли мы `release.sh` дальше или удаляем как мёртвый
код — код содержит операции с блочными устройствами уровня хоста и не является
architecture-neutral.

## 6. Итоговый мини-рецепт (i386, с нуля)

```sh
cd reminix
JOBS=$(nproc) ./releasetools/x86_hdimage.sh      # соберёт всё и создаст minix_x86.img
qemu-system-i386 --enable-kvm -m 256 -hda minix_x86.img
```

(Если система уже собрана в `../obj.i386`, можно ускорить повтор упаковки образа
без пересборки: `CREATE_IMAGE_ONLY=1 ./releasetools/x86_hdimage.sh`.)

То же самое без ручной настройки хоста, через Docker-окружение (`docs/docker-build.md`):

```sh
make -C docker -f build.mk hdimage
make -C docker -f build.mk qemu-hdimage                        # окно VGA
make -C docker -f build.mk hdimage CONSOLE=serial PACK_ONLY=yes
make -C docker -f build.mk run-i386 MEDIA=hd                   # загрузчик и консоль в терминале
make -C docker -f build.mk cdimage SMP=yes CONSOLE=serial      # ISO, §4.5
make -C docker -f build.mk boot-test MEDIA=cd SMP=yes CPUS=4
```

## 7. Что переносится "как есть" на новые x86-подобные цели (amd64)

> **Будет пересмотрено.** Описанный в этом документе путь загрузки (multiboot,
> `bootxx_minixfs3`, GRUB EFI, `modNN_*`-модули) выводится из употребления на
> этапе модернизации: переход на U-Boot + fitImage для всех архитектур, включая
> x86 (`docs/modernization.md` п. 6). Раздел ниже отражает прежний расчёт и
> будет переписан после реализации нового пути; сам документ остаётся референсом
> текущего (унаследованного) пайплайна.

Так как `amd64` в NetBSD — это тот же x86 с 64-битным адресным пространством и тем
же семейством загрузчиков (BIOS MBR + опционально UEFI), пайплайн `x86_*image.sh`,
скорее всего, переносится с минимальными правками: замена `TOOLCHAIN_TRIPLET`,
`nbinstallboot -m amd64`, при необходимости — увеличение размеров разделов. Основная
работа — не здесь, а в `minix/kernel/arch/amd64/` и `minix/servers/vm/arch/amd64/`
(см. `docs/porting.md`).
