# Сборка ReMinix для x86 (i386)

> Пошаговое описание процесса сборки и создания загрузочных образов для архитектуры
> **i386**, реконструированное по содержимому `releasetools/` (`Makefile` верхнего
> уровня, `build.sh`, `releasetools/{Makefile,image.defaults,image.functions,
> x86_hdimage.sh,x86_cdimage.sh,x86_usbimage.sh,x86_ramimage.sh,mkboot}`). Это
> единственная архитектура, для которой пайплайн проверен и завершён от начала до
> конца (жёсткий диск / CD / USB / RAM-образы). См. также `docs/architecture.md`
> §5–7 и `docs/build-arm32.md` для сравнения с ARM-пайплайном.

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
(ядро, серверы, драйверы, юзерленд), плюс tar-архивы наборов (`sets`) в
`RELEASEDIR=${OBJ}/releasedir/i386/binary`.

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

## 7. Что переносится "как есть" на новые x86-подобные цели (amd64)

Так как `amd64` в NetBSD — это тот же x86 с 64-битным адресным пространством и тем
же семейством загрузчиков (BIOS MBR + опционально UEFI), пайплайн `x86_*image.sh`,
скорее всего, переносится с минимальными правками: замена `TOOLCHAIN_TRIPLET`,
`nbinstallboot -m amd64`, при необходимости — увеличение размеров разделов. Основная
работа — не здесь, а в `minix/kernel/arch/amd64/` и `minix/servers/vm/arch/amd64/`
(см. `docs/porting.md`).
