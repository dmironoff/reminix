# ReMinix — Docker-обёртка над build.sh/releasetools.
#
# Ничего не меняет и не подменяет в самой системе сборки (build.sh,
# releasetools/*.sh) — только даёт воспроизводимое хостовое окружение
# (см. Dockerfile) и удобные команды поверх него. Подробности и
# обоснование выбора пакетов — docs/docker-build.md, тесты — docs/testing.md.
#
# Справка: строки "#>" ниже печатает "make -C docker -f build.mk help"
# (без Docker). ОБНОВЛЯТЬ ПРИ КАЖДОМ ИЗМЕНЕНИИ ЦЕЛЕЙ И ПАРАМЕТРОВ ЭТОГО ФАЙЛА.
#
#> ReMinix — сборка и запуск в Docker.  Запускать из корня репозитория:
#>     make -C docker -f build.mk <цель> [ПАРАМЕТР=значение ...]
#> Подробно: docs/docker-build.md (сборка), docs/testing.md (тесты, дампы).
#>
#> ОКРУЖЕНИЕ
#>   help            эта справка (цель по умолчанию; Docker не запускается)
#>   image           собрать Docker-образ окружения reminix-build:latest.
#>                   Его вызывают все цели ниже; шаг ходит в сеть за базовым
#>                   образом. Без сети или чтобы не пересобирать: make -o image ...
#>   shell           интерактивная оболочка в контейнере       [ARCH]
#>   clean-obj       удалить obj/<ARCH>[-smp] (следующая сборка — с нуля) [ARCH SMP]
#>   clean-image     удалить Docker-образ
#>
#> СБОРКА
#>   build           build.sh для ARCH, без образа диска      [ARCH BUILD_TARGET BUILDVARS JOBS]
#>   hdimage         образ диска i386: minix_x86[_smp][_serial].img
#>                   (releasetools/x86_hdimage.sh)            [SMP CONSOLE PACK_ONLY MAX_CPUS BKL_DEBUG JOBS BUILDVARS]
#>   cdimage         загрузочный ISO i386: minix_x86[_smp][_live][_serial].iso; корень — сам CD
#>                   (только чтение, /var /tmp /root на RAM-дисках)
#>                   (releasetools/x86_cdimage.sh)            [SMP CONSOLE ISO_SETS PACK_ONLY JOBS]
#>   usbimage        образ диска с RAM-диском: minix_x86[_smp][_serial]_usb.img; корень
#>                   (minix-base) вшит в mod06_memory, гостю нужно 2 ГБ
#>                   (releasetools/x86_usbimage.sh)           [SMP CONSOLE PACK_ONLY JOBS]
#>   ramimage        то же без образа диска: ядро и модули в obj/i386[-smp]/work-ram/
#>                   для run-i386 / boot-test MEDIA=ram (releasetools/x86_ramimage.sh)
#>                   [SMP PACK_ONLY JOBS]
#>                   У образов SMP=yes в меню загрузчика есть пункт «one CPU (no_apic=1)»;
#>                   каждый образ собирается в своём obj/<ARCH>[-smp]/work*, quick
#>                   освежает только образ последнего hdimage
#>   sdimage         SD-образ earm для платы BOARD: minix_arm_<BOARD>.img
#>                   (releasetools/arm_sdimage.sh; U-Boot платы TI — готовые MLO/u-boot.img
#>                   из дерева u-boot MINIX, зеркало на GitHub, в obj/evbearm-el/u-boot)
#>                   BOARD=beaglexm | beaglebone; orangepi-pc — после Б1/Б2   [BOARD JOBS]
#>   quick           быстрая пересборка: только каталоги QUICK_DIRS (ядро, серверы,
#>                   драйверы, ФС, сеть, minix/lib) + загрузочные модули + освежение
#>                   образа без build.sh (секунды–минуты вместо 5–10 мин).  Нужен
#>                   полный hdimage с тем же SMP= раньше (освежается образ последнего
#>                   hdimage, с его CONSOLE; cd/usb/ram — только их целями).  Новые файлы, списки
#>                   наборов, /etc от скриптов образа — только через hdimage
#>                   (docker/quick-build.sh)                  [SMP BKL_DEBUG QUICK_DIRS JOBS]
#>
#> ТЕСТЫ (minix/tests в QEMU; i386 — сначала hdimage с тем же SMP=, earm — sdimage BOARD=)
#>   test-i386       автоматический прогон i386, журнал obj/test-logs/i386[-smp]-cpuN-<дата>.log,
#>                   итог — в <журнал>.result (с «tests time» — время набора от BEGIN до END),
#>                   время каждого теста — <журнал>.durations, метки строк — <журнал>.times.
#>                   Ctrl-C: дамп и остановка (второй — без дампа)
#>                   [SMP CPUS TESTS TEST_JOBS TEST_RANDOM TEST_TIMES HANG_IDLE TEST_TIMEOUT
#>                    KARGS SNAPSHOT_AT ACCEL CONSOLE]  (CONSOLE — только выбор hd-образа)
#>   test-earm       то же на машине QEMU платы BOARD (TCG), журнал
#>                   obj/test-logs/earm-<BOARD>-cpuN-<дата>.log.  Машина есть только у
#>                   orangepi-pc; параметры ядра (testrun) на earm передаёт U-Boot — до Б2
#>                   цель сообщает об этом и завершается
#>                   [BOARD CPUS TESTS HANG_IDLE TEST_TIMEOUT KARGS SNAPSHOT_AT]
#>   boot-test       проверка загрузки i386 с MEDIA: вход root, uname, hw.ncpuonline (сверка
#>                   с EXPECT_CPUS), корневая ФС, halt -p.  Итог — строка RESULT: PASS|FAIL
#>                   в выводе и в .result, журнал obj/test-logs/i386[-smp]-MEDIA-cpuN-boot-<дата>.log;
#>                   зависание — дамп в <журнал>.postmortem/
#>                   [MEDIA SMP CPUS CONSOLE ISO_SETS ENTRY EXPECT_CPUS BOOT_TIMEOUT KARGS ACCEL]
#>   host-test       тесты кода ReMinix на хосте (minix/tests/host): сборка gcc
#>                   хоста в вариантах HOST_VARIANTS, вывод TAP, журнал
#>                   obj/test-logs/host-<дата>.log, итог — в .result; секунды,
#>                   образ диска не нужен                     [HOST_TESTS HOST_VARIANTS]
#>   test-snapshot   из другого терминала: снимок идущего test-*/run-* в
#>                   <журнал>.snapshot-<N>s/, система работает дальше
#>   test-stop       из другого терминала: дамп в <журнал>.postmortem/ и остановка
#>                   (обе — для любой архитектуры, рассчитаны на один запущенный
#>                   тестовый контейнер; при нескольких — отказ)
#>
#> ЗАПУСК СИСТЕМЫ
#>   run-i386        загрузить систему в QEMU с консолью MINIX на последовательном
#>                   порту в этом терминале (вход: root без пароля). MEDIA=multiboot —
#>                   ядро из DESTDIR напрямую, корень с hd-образа (как test-i386);
#>                   hd | cd | usb — штатно, загрузчиком образа (меню загрузчика тоже в
#>                   терминале; образ с CONSOLE=serial, KARGS не действует); ram — ramimage.
#>                   Журнал obj/test-logs/i386[-smp][-MEDIA]-cpuN-console-<дата>.log, снимки и дамп —
#>                   как у test-i386 (test-snapshot / test-stop), плюс автоснимок
#>                   на каждую новую строку с PANIC_RE на консоли или на экране VGA
#>                   (паника ядра видна только там; в дампе — vga.txt).  Образ открыт с
#>                   snapshot=on — изменения не сохраняются.
#>                   Ctrl-C уходит в MINIX; Ctrl-A x — выйти из QEMU сразу; Ctrl-A h — клавиши QEMU.
#>                   Ввод можно подать конвейером:  (sleep 90; echo root; sleep 5; echo top) | make ...
#>                   [MEDIA SMP CPUS CONSOLE ISO_SETS KARGS SNAPSHOT_AT ACCEL PANIC_RE]
#>   run-earm        то же для earm: машина QEMU платы BOARD, консоль — её UART
#>                   [BOARD CPUS SNAPSHOT_AT PANIC_RE]
#>   qemu-hdimage    старый запуск: QEMU с загрузчиком из образа, окно VGA, без журнала и
#>                   дампов (вместо него — run-i386 MEDIA=hd)  [SMP CONSOLE]
#>
#> ПАРАМЕТРЫ (по умолчанию)
#>   ARCH=i386          архитектура: i386, evbearm-el, ...
#>   SMP=yes (i386; no для earm)   yes: ядро с CONFIG_SMP; свой obj/<ARCH>-smp и образы
#>                      *_smp*; no: однопроцессорное ядро, obj/<ARCH> и образы без _smp.
#>                      SMP-ядро само включает все CPU (no_apic=0 по умолчанию), no_apic=1 —
#>                      один CPU на PIC (KARGS или пункт меню «one CPU»)
#>   MAX_CPUS=8         CONFIG_MAX_CPUS для SMP=yes
#>   BKL_DEBUG=no       yes (только SMP=yes): отладочная обёртка BKL в ядре (CONFIG_BKL_DEBUG:
#>                      владелец и журнал lock/unlock для дампа, bkl.txt); стоит вызова и
#>                      общих записей на каждом входе в ядро.  При переключении build/
#>                      hdimage/quick сами удаляют объекты ядра
#>   JOBS=<nproc>       параллельность build.sh
#>   BUILD_TARGET=release   цель build.sh для "build" (tools, distribution, release, ...)
#>   BUILDVARS=         доп. аргументы build.sh (-V VAR=value ...)
#>   CPUS=1 (4 при SMP=yes; 4 для earm)   число виртуальных CPU
#>   CONSOLE=vga        образы i386: serial — загрузчик (consdev=com0) и консоль MINIX
#>                      (console=tty00) на COM1, к имени образа _serial; для MEDIA=hd|cd|usb
#>                      по умолчанию serial
#>   ISO_SETS=install   cdimage: install — minix-base и все наборы для установки (как в MINIX);
#>                      live — base, comp, games, man, без наборов (к имени _live)
#>   PACK_ONLY=no       yes: образ из готовых наборов, без build.sh (наборы не обновляет
#>                      и quick — после правок нужна полная сборка)
#>   MEDIA=multiboot    run-i386, boot-test: multiboot | hd | cd | usb | ram
#>   ENTRY=             boot-test: пункт меню загрузчика (hd|cd|usb), например 2 у cd SMP —
#>                      «one CPU» (тогда EXPECT_CPUS=1)
#>   EXPECT_CPUS=       boot-test: ожидаемое hw.ncpuonline (CPUS при SMP=yes, иначе 1)
#>   BOOT_TIMEOUT=300   boot-test: секунд от старта до приглашения shell
#>   BOARD=             earm: beaglexm | beaglebone | orangepi-pc (имена машин QEMU; у плат TI
#>                      машины в апстримном QEMU нет — для них только sdimage)
#>   TESTS=             список тестов через запятую (43,71,sh1); пусто — все
#>   TEST_JOBS=1        стресс: брать тесты по N подряд и запускать каждую группу
#>                      одновременно (тест можно повторять: TESTS=90,90,90,90 TEST_JOBS=4;
#>                      без TESTS — все); итоги группы печатаются, когда она вся
#>                      закончилась — HANG_IDLE не меньше времени самой долгой группы;
#>                      в .durations время группы у первого теста, у остальных около 0
#>   TEST_RANDOM=       стресс: перемешать список перед разбивкой на группы, чтобы вместе
#>                      шли разные тесты; yes — seed выбирается сам (печатается в итоге),
#>                      число — этот seed (повторить прогон); время таких прогонов не
#>                      сравнивать
#>   TEST_TIMES=1       каждый тест списка (без TESTS — каждый из всех) запустить N раз;
#>                      с TEST_RANDOM — больше разных сочетаний в группах
#>   HANG_IDLE=600      секунд тишины консоли до признания зависания (дамп).
#>                      SMP: полный прогон — 1800 (тест 70 долго молчит), на 4 CPU — 3000
#>                      (тест 79 молчит до ~40 мин; и TEST_TIMEOUT=9000); TESTS=41 — не меньше 200
#>   TEST_TIMEOUT=5400  предел времени всего прогона, секунд
#>   KARGS=             доп. аргументы ядра: no_apic=1 (SMP-ядро на 1 CPU), tickdebug=N, ...
#>   SNAPSHOT_AT=       через N секунд снять один снимок работающей системы
#>   ACCEL=             tcg | kvm вместо автовыбора (KVM, если есть /dev/kvm)
#>   QUICK_DIRS="minix/lib minix/kernel minix/servers minix/fs minix/net minix/drivers"
#>                      quick: что пересобирать (каталоги дерева, по порядку)
#>   HOST_TESTS=        host-test: модули через запятую (abi64,bitmap,fdt,selftest); пусто — все
#>   HOST_VARIANTS="m32 m64 m32-san m64-san"
#>                      host-test: варианты сборки (-m32/-m64, -san — ASan+UBSan)
#>   PANIC_RE=panic     run-*: regexp строк консоли (и экрана VGA на i386) для автоснимка;
#>                      пусто — выключить
#>
#> ПРИМЕРЫ
#>   make -C docker -f build.mk hdimage                       # SMP-ядро (по умолчанию)
#>   make -C docker -f build.mk hdimage SMP=no                # однопроцессорное ядро
#>   make -C docker -f build.mk test-i386 SMP=no              # однопроцессорное ядро, 1 CPU
#>   make -C docker -f build.mk test-i386 SMP=yes CPUS=2 HANG_IDLE=1800
#>   make -C docker -f build.mk test-i386 SMP=yes CPUS=4 TESTS=31,31,31
#>   make -C docker -f build.mk test-i386 SMP=yes CPUS=4 TESTS=90,90,90,90 TEST_JOBS=4
#>   make -C docker -f build.mk test-i386 SMP=yes CPUS=8 TEST_JOBS=4 TEST_RANDOM=yes TEST_TIMES=3 \
#>        HANG_IDLE=3000 TEST_TIMEOUT=20000
#>   make -C docker -f build.mk test-i386 SMP=yes CPUS=4 KARGS=no_apic=1
#>   make -C docker -f build.mk quick SMP=yes QUICK_DIRS="minix/kernel minix/net/uds"
#>   make -C docker -f build.mk run-i386 SMP=yes CPUS=4
#>   make -C docker -f build.mk cdimage SMP=yes CONSOLE=serial
#>   make -C docker -f build.mk boot-test MEDIA=cd SMP=yes CPUS=4
#>   make -C docker -f build.mk boot-test MEDIA=cd SMP=yes CPUS=4 ENTRY=2 EXPECT_CPUS=1
#>   make -C docker -f build.mk run-i386 MEDIA=hd SMP=yes          # штатно, через загрузчик
#>   make -C docker -f build.mk usbimage PACK_ONLY=yes CONSOLE=serial
#>   make -C docker -f build.mk sdimage BOARD=beaglebone
#>   make -C docker -f build.mk host-test HOST_TESTS=bitmap HOST_VARIANTS=m64-san
#>   make -C docker -f build.mk host-test HOST_TESTS=abi64    # раскладки типов 32/64 (types-audit.md)
#>   make -C docker -f build.mk run-earm BOARD=orangepi-pc
#>   make -o image -C docker -f build.mk test-i386       # не трогать Docker-образ
#>   Долгий прогон, не привязанный к терминалу:
#>   (setsid nohup make -o image -C docker -f build.mk test-i386 ... > obj/test-logs/run.out 2>&1 < /dev/null &)
#
# Состояние сборки (объектные файлы, DESTDIR, releasedir) живёт в
# obj/<ARCH>[-smp] внутри репозитория — эта директория уже покрыта
# .gitignore (шаблон "obj") и переживает между запусками контейнера.
# Кросс-тулчейн — в obj/<ARCH>[-smp]/tooldir, имя не зависит от версии ядра
# хоста (см. CONTAINER_TOOLDIR ниже).

SHELL := /bin/sh

IMAGE_NAME  ?= reminix-build
IMAGE_TAG   ?= latest
IMAGE       := $(IMAGE_NAME):$(IMAGE_TAG)

ARCH         ?= i386
JOBS         ?= $(shell nproc 2>/dev/null || echo 1)
BUILD_TARGET ?= release
BUILDVARS    ?=

# SMP=yes: сборка с CONFIG_SMP (флаг идёт в CPPFLAGS всего дерева, см.
# share/mk/bsd.own.mk), поэтому отдельный объектный каталог и образ.
# По умолчанию yes только для i386 (А5, docs/modernization.md); на earm SMP
# нет. "?=" даёт рекурсивную переменную: ARCH берётся с учётом ARCH цели
# (sdimage, test-earm, ...), не только глобальный.
SMP          ?= $(if $(filter i386,$(ARCH)),yes,no)
MAX_CPUS     ?= 8
# BKL_DEBUG=yes (только с SMP=yes): отладочная обёртка BKL в ядре
# (CONFIG_BKL_DEBUG, minix/kernel/spinlock.h, smp.c). Флаг идёт в окружение
# контейнера, nbmake берёт его оттуда (обёртка nbmake-i386 его не фиксирует,
# поэтому hdimage и quick ведут себя одинаково). Объектные файлы от флагов не
# зависят: при переключении bkl_sync удаляет объекты ядра; последнее значение —
# obj/<ARCH>-smp/.bkl_debug (нет файла — старая сборка, обёртка была всегда).
BKL_DEBUG    ?= no

# Прогон тестов (test-i386).
CPUS         ?= $(if $(filter yes,$(SMP)),4,1)
TESTS        ?=
# сколько тестов из TESTS запускать одновременно (rc.d/minixtests, testjobs=)
TEST_JOBS    ?= 1
# перемешать список (yes или seed) и сколько раз повторить каждый тест
TEST_RANDOM  ?=
TEST_TIMES   ?= 1
TEST_TIMEOUT ?= 5400
# секунд тишины на консоли, после которых прогон считается зависшим
# (снимается post-mortem: docker/qemu-postmortem.py)
HANG_IDLE    ?= 600
# дополнительные аргументы ядра (при CPUS>1 run-tests.sh сам добавляет
# no_apic=0, иначе SMP-ядро стартует в однопроцессорном режиме)
KARGS        ?=
# снимок работающей системы (как post-mortem) через SNAPSHOT_AT секунд
SNAPSHOT_AT  ?=
# tcg|kvm вместо автоматического выбора (KVM, если доступен)
ACCEL        ?=
# метка контейнера test-i386, по ней его находят test-snapshot / test-stop
TEST_LABEL   := reminix-test=1
# earm: плата (sdimage, test-earm, run-earm). Имена — как у машин QEMU; у
# плат TI машины в апстримном QEMU нет, их образы только собираются.
BOARD        ?=
EARM_BOARDS  := beaglexm beaglebone orangepi-pc
QEMU_MACHINE_orangepi-pc := orangepi-pc
EARM_IMG      = minix_arm_$(BOARD).img
# проверки BOARD (раскрываются только в рецептах целей earm)
need_board    = $(if $(filter $(BOARD),$(EARM_BOARDS)),,$(error BOARD= обязателен: $(EARM_BOARDS)))
need_machine  = $(if $(QEMU_MACHINE_$(BOARD)),,$(error BOARD=$(BOARD): в апстримном QEMU нет такой машины -- только сборка образа, проверка на плате))

# Образы i386 (hdimage, cdimage, usbimage, ramimage; docs/build-x86.md §4).
# CONSOLE=serial: загрузчик и консоль MINIX на COM1 (BOOT_CONSOLE=com0 для
# releasetools); без него консоль на VGA, как в MINIX. По умолчанию serial для
# MEDIA=hd|cd|usb (run-i386, boot-test): иначе на последовательном порту
# ничего не видно.
MEDIA        ?= multiboot
CONSOLE      ?= $(if $(filter hd cd usb,$(MEDIA)),serial,vga)
# install: base и наборы для установщика (как в MINIX); live: base, comp,
# games, man без наборов
ISO_SETS     ?= install
# yes: только упаковка образа из готовых наборов, без build.sh
PACK_ONLY    ?= no
# boot-test: пункт меню загрузчика, ожидаемое hw.ncpuonline, предел загрузки
ENTRY        ?=
EXPECT_CPUS  ?= $(if $(filter yes,$(SMP)),$(CPUS),1)
BOOT_TIMEOUT ?= 300
# переменные releasetools/x86_*image.sh для этих параметров
IMAGE_ENV     = $(if $(filter serial,$(CONSOLE)),BOOT_CONSOLE=com0) $(if $(filter yes,$(PACK_ONLY)),CREATE_IMAGE_ONLY=1)
ISO_ENV       = $(if $(filter live,$(ISO_SETS)),SETS="minix-base minix-comp minix-games minix-man" BUNDLE_SETS=0)
need_console  = $(if $(filter $(CONSOLE),vga serial),,$(error CONSOLE=$(CONSOLE): vga | serial))
need_isosets  = $(if $(filter $(ISO_SETS),install live),,$(error ISO_SETS=$(ISO_SETS): install | live))
need_media    = $(if $(filter $(MEDIA),multiboot hd cd usb ram),,$(error MEDIA=$(MEDIA): multiboot | hd | cd | usb | ram))$(if \
	$(filter $(MEDIA),hd cd usb),$(if $(filter serial,$(CONSOLE)),,$(error MEDIA=$(MEDIA) грузится загрузчиком образа: нужен образ с CONSOLE=serial)))
# что грузит run-i386 / boot-test: образ и каталог ядра (символы для дампов;
# MEDIA=ram -- ядро и модули с корневой ФС)
MEDIA_IMG     = $(if $(filter cd,$(MEDIA)),$(ISO_IMG),$(if $(filter usb,$(MEDIA)),$(USB_IMG),$(if $(filter ram,$(MEDIA)),-,$(HD_IMG))))
MEDIA_MODDIR  = $(if $(filter ram,$(MEDIA)),$(RAM_DIR),$(CONTAINER_DEST)/boot/minix/.temp)
MEDIA_TAG     = $(if $(filter multiboot,$(MEDIA)),,-$(MEDIA))

# quick: каталоги дерева для быстрой пересборки (docker/quick-build.sh)
QUICK_DIRS   ?= minix/lib minix/kernel minix/servers minix/fs minix/net minix/drivers
# host-test: модули (подкаталоги minix/tests/host) и варианты сборки
HOST_TESTS    ?=
HOST_VARIANTS ?= m32 m64 m32-san m64-san
comma := ,
# run-i386: строки консоли, по которым снимается автоматический снимок
# (расширенное регулярное выражение; пусто — выключено)
PANIC_RE     ?= panic
# -t только при терминале на stdin: иначе (ввод из конвейера) docker run -t
# отказывается работать
TTY_FLAG     := $(shell test -t 0 && echo -t)

HOST_UID := $(shell id -u)
HOST_GID := $(shell id -g)

REPO_ROOT      := $(abspath $(CURDIR)/..)
CONTAINER_REPO := /work/reminix

FLAVOR      = $(if $(filter yes,$(SMP)),-smp,)
SMP_VARS    = $(if $(filter yes,$(SMP)),-V CONFIG_SMP=y -V CONFIG_MAX_CPUS=$(MAX_CPUS),)
SMP_SFX     = $(if $(filter yes,$(SMP)),_smp)
SER_SFX     = $(if $(filter serial,$(CONSOLE)),_serial)
# образы i386 (docs/build-x86.md §4): hdimage (его же грузят test-i386,
# run-i386 MEDIA=multiboot|hd, освежает quick), cdimage, usbimage; ramimage --
# каталог с ядром и модулями
HD_IMG      = minix_x86$(SMP_SFX)$(SER_SFX).img
ISO_IMG     = minix_x86$(SMP_SFX)$(if $(filter live,$(ISO_SETS)),_live)$(SER_SFX).iso
USB_IMG     = minix_x86$(SMP_SFX)$(SER_SFX)_usb.img
RAM_DIR     = $(CONTAINER_OBJ)/work-ram
BKL_ENV     = $(if $(filter yes,$(SMP)),$(if $(filter yes,$(BKL_DEBUG)),-e CONFIG_BKL_DEBUG=y))
HOST_OBJ    = $(REPO_ROOT)/obj/$(ARCH)$(FLAVOR)

define bkl_sync
	@if [ "$(SMP)" = yes ]; then \
		mkdir -p "$(HOST_OBJ)"; \
		old=$$(cat "$(HOST_OBJ)/.bkl_debug" 2>/dev/null || echo yes); \
		if [ "$$old" != "$(BKL_DEBUG)" ]; then \
			echo ">>> BKL_DEBUG $$old -> $(BKL_DEBUG): kernel objects removed"; \
			rm -f "$(HOST_OBJ)"/minix/kernel/*.o "$(HOST_OBJ)"/minix/kernel/kernel; \
		fi; \
		echo "$(BKL_DEBUG)" > "$(HOST_OBJ)/.bkl_debug"; \
	fi
endef

# Через "=" (не ":="), чтобы честно пересчитывались при
# ARCH-переопределении конкретной цели (см. hdimage/sdimage ниже).
CONTAINER_OBJ    = $(CONTAINER_REPO)/obj/$(ARCH)$(FLAVOR)
CONTAINER_DEST   = $(CONTAINER_OBJ)/destdir.$(ARCH)
# ВАЖНО: RELEASEDIR должен быть "голым" (без .../$(ARCH)/binary) --
# build.sh сам добавляет "${RELEASEMACHINEDIR}/binary/sets" при сборке
# release-наборов. Если передать уже с суффиксом (как в дефолте
# releasetools/image.defaults), build.sh удвоит суффикс и
# releasetools/x86_hdimage.sh не найдёт наборы. См. docs/build-x86.md §3.
CONTAINER_RELDIR = $(CONTAINER_OBJ)/releasedir

# Кросс-тулчейн. По умолчанию build.sh кладёт его в
# $OBJ/tooldir.<uname -s>-<uname -r>-<uname -m>, то есть имя зависит от
# версии ядра хоста: после каждого обновления ядра Ubuntu тулчейн
# собирался заново. Фиксируем имя: $OBJ/tooldir (-T для build.sh,
# CROSS_TOOLS для releasetools/image.defaults). Свой для каждого OBJ:
# инструменты всё равно собираются в $OBJ/tools, а общий TOOLDIR ломался бы
# при параллельных сборках.
# ВАЖНО: при смене TOOLDIR у существующего OBJ удалить $OBJ/tools, иначе
# build.sh -u сочтёт инструменты установленными и новый TOOLDIR останется
# неполным ("don't know how to make .../tooldir/bin/nbfile").
CONTAINER_TOOLDIR = $(CONTAINER_OBJ)/tooldir
# earm собирается GCC (docs/build-arm32.md §2): то же, что по умолчанию задаёт
# releasetools/arm_sdimage.sh, но его значение BUILDVARS перекрывается нашим
EARM_BUILDVARS    = -V MKGCCCMDS=yes -V MKLLVM=no
ALL_BUILDVARS     = -T $(CONTAINER_TOOLDIR) $(SMP_VARS) $(if $(filter evbearm-el,$(ARCH)),$(EARM_BUILDVARS)) $(BUILDVARS)

# /dev/kvm on the host is normally root:kvm mode 0660 -- --device=/dev/kvm
# alone gets the node into the container, but our unprivileged "builder"
# user (see Dockerfile) still isn't in a "kvm" group there and gets
# "Permission denied" opening it. Docker shares the host's UID/GID
# namespace (no remapping in this setup), so adding the *host's* kvm
# group GID as a supplementary group inside the container is enough --
# no need to know or match the group *name*, only the numeric GID.
KVM_DEVICE   := $(shell test -e /dev/kvm && echo --device=/dev/kvm)
KVM_GID      := $(shell getent group kvm 2>/dev/null | cut -d: -f3)
KVM_GROUPADD := $(if $(KVM_GID),--group-add $(KVM_GID))

# DOCKER_RUN_BASE holds every "docker run" flag but stops short of the
# image name, so extra flags (KVM_DEVICE, -it, ...) can still be
# inserted *before* $(IMAGE) -- anything after the image name is the
# container's command (there's no ENTRYPOINT in the image, see
# Dockerfile), not a flag to docker itself.
DOCKER_RUN_BASE = docker run --rm \
	-v $(REPO_ROOT):$(CONTAINER_REPO) \
	-w $(CONTAINER_REPO) \
	-e ARCH=$(ARCH) \
	-e OBJ=$(CONTAINER_OBJ) \
	-e DESTDIR=$(CONTAINER_DEST) \
	-e RELEASEDIR=$(CONTAINER_RELDIR) \
	-e CROSS_TOOLS=$(CONTAINER_TOOLDIR)/bin \
	-e BUILDVARS="$(ALL_BUILDVARS)" \
	-e JOBS=$(JOBS) $(BKL_ENV)

DOCKER_RUN = $(DOCKER_RUN_BASE) $(IMAGE)

.PHONY: help image shell build hdimage cdimage usbimage ramimage boot-test quick sdimage host-test test-i386 run-i386 test-snapshot test-stop qemu-hdimage test-earm run-earm clean-obj clean-image

help:
	@sed -n 's/^#> \{0,1\}//p' $(lastword $(MAKEFILE_LIST))

image:
	docker build \
		--build-arg BUILD_UID=$(HOST_UID) \
		--build-arg BUILD_GID=$(HOST_GID) \
		-t $(IMAGE) \
		-f Dockerfile \
		.

shell: image
	$(DOCKER_RUN_BASE) $(KVM_DEVICE) $(KVM_GROUPADD) -it $(IMAGE) bash

# Прямой вызов build.sh — без упаковки в конкретный образ диска.
# Годится и для "как есть" непортированных пока архитектур (amd64 и
# т.д.): дойдёт настолько далеко, насколько дерево уже поддерживает
# ARCH, что и является дымовым тестом прогресса портирования.
build: image
	$(bkl_sync)
	$(DOCKER_RUN) bash -lc '\
		mkdir -p "$$OBJ" && \
		sh build.sh -j "$$JOBS" -m "$$ARCH" -O "$$OBJ" -D "$$DESTDIR" $$BUILDVARS -U -u $(BUILD_TARGET)'

# Архитектура зафиксирована в самих releasetools-скриптах (см.
# docs/build-x86.md / docs/build-arm32.md) — здесь только пробрасываем
# соответствующий ARCH, чтобы obj/<arch> совпадал с тем, что построит
# сам скрипт. BUILDVARS (с -T и SMP-флагами) скрипты передают build.sh.
# Образы i386: у каждого свой WORK_DIR (скрипты стирают его целиком), $OBJ/work
# остаётся за hdimage -- из него quick освежает образ; имя образа quick берёт
# из $OBJ/work/.image.
hdimage: ARCH := i386
hdimage: image
	$(need_console)
	$(bkl_sync)
	$(DOCKER_RUN) bash -lc 'mkdir -p "$$OBJ" && $(IMAGE_ENV) IMG=$(HD_IMG) ./releasetools/x86_hdimage.sh && \
		echo $(HD_IMG) > "$$OBJ/work/.image"'

cdimage: ARCH := i386
cdimage: image
	$(need_console)$(need_isosets)
	$(bkl_sync)
	$(DOCKER_RUN) bash -lc 'mkdir -p "$$OBJ" && $(IMAGE_ENV) $(ISO_ENV) WORK_DIR="$$OBJ/work-cd" IMG=$(ISO_IMG) ./releasetools/x86_cdimage.sh'

usbimage: ARCH := i386
usbimage: image
	$(need_console)
	$(bkl_sync)
	$(DOCKER_RUN) bash -lc 'mkdir -p "$$OBJ" && $(IMAGE_ENV) WORK_DIR="$$OBJ/work-usb" IMG=$(USB_IMG) ./releasetools/x86_usbimage.sh'

ramimage: ARCH := i386
ramimage: image
	$(bkl_sync)
	$(DOCKER_RUN) bash -lc 'mkdir -p "$$OBJ" && $(IMAGE_ENV) WORK_DIR="$$OBJ/work-ram" ./releasetools/x86_ramimage.sh'

# Быстрая пересборка без build.sh: каталоги QUICK_DIRS, загрузочные модули и
# освежение образа из рабочего каталога последнего hdimage.
quick: ARCH := i386
quick: image
	$(bkl_sync)
	$(DOCKER_RUN) bash -lc 'IMG=$$(cat "$$OBJ/work/.image" 2>/dev/null || echo $(HD_IMG)) QUICK_DIRS="$(QUICK_DIRS)" bash docker/quick-build.sh'

sdimage: ARCH := evbearm-el
sdimage: image
	$(need_board)
	$(DOCKER_RUN) bash -lc 'mkdir -p "$$OBJ" && BOARD=$(BOARD) IMG=$(EARM_IMG) ./releasetools/arm_sdimage.sh'

# Тесты на хосте (docs/testing.md §1д): minix/tests/host/GNUmakefile собирает
# t_*.c gcc хоста в вариантах HOST_VARIANTS и прогоняет их (run.sh). Журнал и
# итог -- как у test-i386, но host-<дата>.
host-test: image
	@mkdir -p $(REPO_ROOT)/obj/test-logs
	$(DOCKER_RUN) bash -c 'log=obj/test-logs/host-$(shell date +%Y%m%d-%H%M%S); \
		make -C minix/tests/host check HT_OBJ=$(CONTAINER_REPO)/obj/host-tests \
			VARIANTS="$(HOST_VARIANTS)" $(if $(HOST_TESTS),MODULES="$(subst $(comma), ,$(HOST_TESTS))") \
			2>&1 | tee $$log.log; rc=$${PIPESTATUS[0]}; \
		sed -n "/host test summary/,/^=*\$$/p" $$log.log > $$log.result; \
		echo "log: $$log.log"; exit $$rc'

# Автоматический прогон minix/tests (docs/testing.md): ядро и модули из
# DESTDIR грузятся QEMU напрямую (multiboot), корневая ФС — из образа
# (snapshot=on, образ не меняется), rc.d/minixtests запускает "run -T" и
# выключает машину. Журнал — obj/test-logs/. Образ не пересобирается:
# сначала hdimage (с тем же SMP=).
test-i386: ARCH := i386
test-i386: image
	$(DOCKER_RUN_BASE) $(KVM_DEVICE) $(KVM_GROUPADD) --label $(TEST_LABEL) -e HANG_IDLE=$(HANG_IDLE) -e KARGS="$(KARGS)" -e TEST_JOBS=$(TEST_JOBS) -e TEST_RANDOM=$(TEST_RANDOM) -e TEST_TIMES=$(TEST_TIMES) -e SNAPSHOT_AT=$(SNAPSHOT_AT) -e ACCEL=$(ACCEL) $(IMAGE) bash docker/run-tests.sh \
		$(CONTAINER_DEST)/boot/minix/.temp \
		$(HD_IMG) \
		$(CPUS) \
		"$(TESTS)" \
		$(TEST_TIMEOUT) \
		obj/test-logs/i386$(FLAVOR)-cpu$(CPUS)-$(shell date +%Y%m%d-%H%M%S).log \
		docker/tests-known-failures.i386

# Интерактивный запуск (docs/testing.md §1в): MEDIA=multiboot -- та же
# загрузка, что у test-i386, но без testrun; hd/cd/usb -- штатно, загрузчиком
# образа; ram -- ядро и модули ramimage. Консоль MINIX на последовательном
# порту, подключённом к терминалу. Журнал
# obj/test-logs/i386[-smp][-MEDIA]-cpuN-console-*.log, снимки и post-mortem --
# как у тестов (test-snapshot / test-stop работают по той же метке), плюс
# автоматический снимок на каждую новую строку PANIC_RE.
run-i386: ARCH := i386
run-i386: image
	$(need_media)
	$(DOCKER_RUN_BASE) $(KVM_DEVICE) $(KVM_GROUPADD) --label $(TEST_LABEL) -i $(TTY_FLAG) -e MEDIA=$(MEDIA) -e KARGS="$(KARGS)" -e SNAPSHOT_AT=$(SNAPSHOT_AT) -e ACCEL=$(ACCEL) -e PANIC_RE="$(PANIC_RE)" $(IMAGE) bash docker/run-console.sh \
		$(MEDIA_MODDIR) \
		$(MEDIA_IMG) \
		$(CPUS) \
		obj/test-logs/i386$(FLAVOR)$(MEDIA_TAG)-cpu$(CPUS)-console-$(shell date +%Y%m%d-%H%M%S).log

# Проверка загрузки (docs/testing.md §1е): загрузить с MEDIA, войти root,
# uname / hw.ncpuonline / mount, halt -p. Итог -- строка RESULT: в выводе и в
# <журнал>.result; журнал obj/test-logs/i386[-smp]-MEDIA-cpuN-boot-*.log.
boot-test: ARCH := i386
boot-test: image
	$(need_media)
	$(DOCKER_RUN_BASE) $(KVM_DEVICE) $(KVM_GROUPADD) --label $(TEST_LABEL) -e MEDIA=$(MEDIA) -e KARGS="$(KARGS)" -e ACCEL=$(ACCEL) -e ENTRY=$(ENTRY) -e EXPECT_CPUS=$(EXPECT_CPUS) -e BOOT_TIMEOUT=$(BOOT_TIMEOUT) $(IMAGE) python3 docker/boot-test.py \
		$(MEDIA_MODDIR) \
		$(MEDIA_IMG) \
		$(CPUS) \
		obj/test-logs/i386$(FLAVOR)-$(MEDIA)-cpu$(CPUS)-boot-$(shell date +%Y%m%d-%H%M%S).log

# earm: то же, что test-i386 / run-i386, на машине QEMU платы BOARD (TCG,
# образ SD с snapshot=on). Символы для дампа -- из ELF ядра earm. Параметры
# ядра (testrun, KARGS) на earm передаёт U-Boot -- появится с загрузкой
# Orange Pi (Б2), до тех пор test-earm сообщает об этом и завершается.
test-earm run-earm: CPUS := $(if $(filter command line environment,$(origin CPUS)),$(CPUS),4)

test-earm: ARCH := evbearm-el
test-earm: image
	$(need_board)
	$(need_machine)
	$(DOCKER_RUN_BASE) --label $(TEST_LABEL) -e TARGET=earm -e MACHINE=$(QEMU_MACHINE_$(BOARD)) -e HANG_IDLE=$(HANG_IDLE) -e KARGS="$(KARGS)" -e SNAPSHOT_AT=$(SNAPSHOT_AT) $(IMAGE) bash docker/run-tests.sh \
		$(CONTAINER_OBJ)/minix/kernel \
		$(EARM_IMG) \
		$(CPUS) \
		"$(TESTS)" \
		$(TEST_TIMEOUT) \
		obj/test-logs/earm-$(BOARD)-cpu$(CPUS)-$(shell date +%Y%m%d-%H%M%S).log \
		docker/tests-known-failures.earm

run-earm: ARCH := evbearm-el
run-earm: image
	$(need_board)
	$(need_machine)
	$(DOCKER_RUN_BASE) --label $(TEST_LABEL) -i $(TTY_FLAG) -e TARGET=earm -e MACHINE=$(QEMU_MACHINE_$(BOARD)) -e KARGS="$(KARGS)" -e SNAPSHOT_AT=$(SNAPSHOT_AT) -e PANIC_RE="$(PANIC_RE)" $(IMAGE) bash docker/run-console.sh \
		$(CONTAINER_OBJ)/minix/kernel \
		$(EARM_IMG) \
		$(CPUS) \
		obj/test-logs/earm-$(BOARD)-cpu$(CPUS)-console-$(shell date +%Y%m%d-%H%M%S).log

# Управление идущим прогоном из другого терминала (docs/testing.md §1б).
# Контейнер test-i386 помечен меткой TEST_LABEL; run-tests.sh — его PID 1 и
# обрабатывает сигналы сам: USR1 — снимок работающей машины
# (<журнал>.snapshot-<N>s/), TERM — post-mortem (<журнал>.postmortem/) и
# остановка, как Ctrl-C. docker kill -s, а не docker stop: тот через 10 с
# шлёт SIGKILL, и дамп может не успеть.
# Общие для всех архитектур; рассчитаны на один запущенный тестовый
# контейнер (test-*/run-*), при нескольких -- отказ.
one_test_container = ids=$$(docker ps -q --filter label=$(TEST_LABEL)); \
	n=$$(echo $$ids | wc -w); \
	if [ "$$n" -eq 0 ]; then echo "$@: no running test-*/run-* container"; exit 1; fi; \
	if [ "$$n" -gt 1 ]; then echo "$@: $$n test containers running ($$ids), expected one"; exit 1; fi

test-snapshot:
	@$(one_test_container); \
	docker kill -s USR1 $$ids >/dev/null && echo "snapshot requested: $$ids"

test-stop:
	@$(one_test_container); \
	docker kill -s TERM $$ids >/dev/null && echo "stop requested: $$ids"

qemu-hdimage: ARCH := i386
qemu-hdimage: image
	$(DOCKER_RUN_BASE) $(KVM_DEVICE) $(KVM_GROUPADD) $(IMAGE) bash -lc \
		'test -f $(HD_IMG) || { echo "$(HD_IMG) не найден -- сначала: make -C docker -f build.mk hdimage" >&2; exit 1; }; \
		 qemu-system-i386 $(if $(KVM_DEVICE),--enable-kvm,) -m 256 -drive file=$(HD_IMG),format=raw,if=ide'

clean-obj:
	rm -rf $(REPO_ROOT)/obj/$(ARCH)$(FLAVOR)

clean-image:
	-docker rmi $(IMAGE)
