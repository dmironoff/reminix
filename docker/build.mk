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
#>   hdimage         образ диска i386: minix_x86.img, с SMP=yes — minix_x86_smp.img
#>                   (releasetools/x86_hdimage.sh)            [SMP MAX_CPUS JOBS BUILDVARS]
#>   sdimage         SD-образ earm (BeagleBoard-xM): minix_arm_sd.img
#>
#> ТЕСТЫ (minix/tests в QEMU, i386; сначала hdimage с тем же SMP=)
#>   test-i386       автоматический прогон, журнал obj/test-logs/i386[-smp]-cpuN-<дата>.log,
#>                   итог — в <журнал>.result.  Ctrl-C: дамп и остановка (второй — без дампа)
#>                   [SMP CPUS TESTS HANG_IDLE TEST_TIMEOUT KARGS SNAPSHOT_AT ACCEL]
#>   test-snapshot   из другого терминала: снимок идущего test-i386/run-i386
#>                   в <журнал>.snapshot-<N>s/, система работает дальше
#>   test-stop       из другого терминала: дамп в <журнал>.postmortem/ и остановка
#>
#> ЗАПУСК СИСТЕМЫ
#>   run-i386        загрузить систему в QEMU с консолью MINIX на последовательном
#>                   порту в этом терминале (вход: root без пароля). Журнал
#>                   obj/test-logs/i386[-smp]-cpuN-console-<дата>.log, снимки и дамп —
#>                   как у test-i386 (test-snapshot / test-stop), плюс автоснимок
#>                   на каждую новую строку с PANIC_RE на консоли или на экране VGA
#>                   (паника ядра видна только там; в дампе — vga.txt).  Образ открыт с
#>                   snapshot=on — изменения не сохраняются.
#>                   Ctrl-C уходит в MINIX; Ctrl-A x — выйти из QEMU сразу; Ctrl-A h — клавиши QEMU.
#>                   Ввод можно подать конвейером:  (sleep 90; echo root; sleep 5; echo top) | make ...
#>                   [SMP CPUS KARGS SNAPSHOT_AT ACCEL PANIC_RE]
#>   qemu-hdimage    старый запуск: QEMU с загрузчиком из образа, без журнала и дампов [SMP]
#>   qemu-sdimage    QEMU beaglexm с SD-образом earm
#>
#> ПАРАМЕТРЫ (по умолчанию)
#>   ARCH=i386          архитектура: i386, evbearm-el, ...
#>   SMP=no             yes: ядро с CONFIG_SMP; свой obj/<ARCH>-smp и minix_x86_smp.img
#>   MAX_CPUS=8         CONFIG_MAX_CPUS для SMP=yes
#>   JOBS=<nproc>       параллельность build.sh
#>   BUILD_TARGET=release   цель build.sh для "build" (tools, distribution, release, ...)
#>   BUILDVARS=         доп. аргументы build.sh (-V VAR=value ...)
#>   CPUS=1 (4 при SMP=yes)   число виртуальных CPU; при CPUS>1 ядру добавляется no_apic=0
#>   TESTS=             список тестов через запятую (43,71,sh1); пусто — все
#>   HANG_IDLE=600      секунд тишины консоли до признания зависания (дамп).
#>                      SMP: полный прогон — 1800 (тест 70 долго молчит), TESTS=41 — не меньше 200
#>   TEST_TIMEOUT=5400  предел времени всего прогона, секунд
#>   KARGS=             доп. аргументы ядра: no_apic=1 (SMP-ядро на 1 CPU), tickdebug=N, ...
#>   SNAPSHOT_AT=       через N секунд снять один снимок работающей системы
#>   ACCEL=             tcg | kvm вместо автовыбора (KVM, если есть /dev/kvm)
#>   PANIC_RE=panic     run-i386: regexp строк консоли и экрана VGA для автоснимка; пусто — выключить
#>
#> ПРИМЕРЫ
#>   make -C docker -f build.mk hdimage SMP=yes
#>   make -C docker -f build.mk test-i386 SMP=yes CPUS=2 HANG_IDLE=1800
#>   make -C docker -f build.mk test-i386 SMP=yes CPUS=4 TESTS=31,31,31
#>   make -C docker -f build.mk test-i386 SMP=yes CPUS=4 KARGS=no_apic=1
#>   make -C docker -f build.mk run-i386 SMP=yes CPUS=4
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
SMP          ?= no
MAX_CPUS     ?= 8

# Прогон тестов (test-i386).
CPUS         ?= $(if $(filter yes,$(SMP)),4,1)
TESTS        ?=
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
IMG_NAME    = $(if $(filter yes,$(SMP)),minix_x86_smp.img,minix_x86.img)

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
ALL_BUILDVARS     = -T $(CONTAINER_TOOLDIR) $(SMP_VARS) $(BUILDVARS)

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
	-e JOBS=$(JOBS)

DOCKER_RUN = $(DOCKER_RUN_BASE) $(IMAGE)

.PHONY: help image shell build hdimage sdimage test-i386 run-i386 test-snapshot test-stop qemu-hdimage qemu-sdimage clean-obj clean-image

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
	$(DOCKER_RUN) bash -lc '\
		mkdir -p "$$OBJ" && \
		sh build.sh -j "$$JOBS" -m "$$ARCH" -O "$$OBJ" -D "$$DESTDIR" $$BUILDVARS -U -u $(BUILD_TARGET)'

# Архитектура зафиксирована в самих releasetools-скриптах (см.
# docs/build-x86.md / docs/build-arm32.md) — здесь только пробрасываем
# соответствующий ARCH, чтобы obj/<arch> совпадал с тем, что построит
# сам скрипт. BUILDVARS (с -T и SMP-флагами) скрипты передают build.sh.
hdimage: ARCH := i386
hdimage: image
	$(DOCKER_RUN) bash -lc 'mkdir -p "$$OBJ" && IMG=$(IMG_NAME) ./releasetools/x86_hdimage.sh'

sdimage: ARCH := evbearm-el
sdimage: image
	$(DOCKER_RUN) bash -lc 'mkdir -p "$$OBJ" && ./releasetools/arm_sdimage.sh'

# Автоматический прогон minix/tests (docs/testing.md): ядро и модули из
# DESTDIR грузятся QEMU напрямую (multiboot), корневая ФС — из образа
# (snapshot=on, образ не меняется), rc.d/minixtests запускает "run -T" и
# выключает машину. Журнал — obj/test-logs/. Образ не пересобирается:
# сначала hdimage (с тем же SMP=).
test-i386: ARCH := i386
test-i386: image
	$(DOCKER_RUN_BASE) $(KVM_DEVICE) $(KVM_GROUPADD) --label $(TEST_LABEL) -e HANG_IDLE=$(HANG_IDLE) -e KARGS="$(KARGS)" -e SNAPSHOT_AT=$(SNAPSHOT_AT) -e ACCEL=$(ACCEL) $(IMAGE) bash docker/run-tests.sh \
		$(CONTAINER_DEST)/boot/minix/.temp \
		$(IMG_NAME) \
		$(CPUS) \
		"$(TESTS)" \
		$(TEST_TIMEOUT) \
		obj/test-logs/i386$(FLAVOR)-cpu$(CPUS)-$(shell date +%Y%m%d-%H%M%S).log \
		docker/tests-known-failures.i386

# Интерактивный запуск (docs/testing.md §1в): та же загрузка, что у
# test-i386, но без testrun -- консоль MINIX на последовательном порту,
# подключённом к терминалу. Журнал obj/test-logs/i386[-smp]-cpuN-console-*.log,
# снимки и post-mortem -- как у тестов (test-snapshot / test-stop работают по
# той же метке), плюс автоматический снимок на каждую новую строку PANIC_RE.
run-i386: ARCH := i386
run-i386: image
	$(DOCKER_RUN_BASE) $(KVM_DEVICE) $(KVM_GROUPADD) --label $(TEST_LABEL) -i $(TTY_FLAG) -e KARGS="$(KARGS)" -e SNAPSHOT_AT=$(SNAPSHOT_AT) -e ACCEL=$(ACCEL) -e PANIC_RE="$(PANIC_RE)" $(IMAGE) bash docker/run-console.sh \
		$(CONTAINER_DEST)/boot/minix/.temp \
		$(IMG_NAME) \
		$(CPUS) \
		obj/test-logs/i386$(FLAVOR)-cpu$(CPUS)-console-$(shell date +%Y%m%d-%H%M%S).log

# Управление идущим прогоном из другого терминала (docs/testing.md §1б).
# Контейнер test-i386 помечен меткой TEST_LABEL; run-tests.sh — его PID 1 и
# обрабатывает сигналы сам: USR1 — снимок работающей машины
# (<журнал>.snapshot-<N>s/), TERM — post-mortem (<журнал>.postmortem/) и
# остановка, как Ctrl-C. docker kill -s, а не docker stop: тот через 10 с
# шлёт SIGKILL, и дамп может не успеть.
test-snapshot:
	@ids=$$(docker ps -q --filter label=$(TEST_LABEL)); \
	if [ -z "$$ids" ]; then echo "test-snapshot: no running test-i386"; exit 1; fi; \
	docker kill -s USR1 $$ids >/dev/null && echo "snapshot requested: $$ids"

test-stop:
	@ids=$$(docker ps -q --filter label=$(TEST_LABEL)); \
	if [ -z "$$ids" ]; then echo "test-stop: no running test-i386"; exit 1; fi; \
	docker kill -s TERM $$ids >/dev/null && echo "stop requested: $$ids"

qemu-hdimage: ARCH := i386
qemu-hdimage: image
	$(DOCKER_RUN_BASE) $(KVM_DEVICE) $(KVM_GROUPADD) $(IMAGE) bash -lc \
		'test -f $(IMG_NAME) || { echo "$(IMG_NAME) не найден -- сначала: make -C docker -f build.mk hdimage" >&2; exit 1; }; \
		 qemu-system-i386 $(if $(KVM_DEVICE),--enable-kvm,) -m 256 -drive file=$(IMG_NAME),format=raw,if=ide'

qemu-sdimage: ARCH := evbearm-el
qemu-sdimage: image
	$(DOCKER_RUN_BASE) $(IMAGE) bash -lc \
		'test -f minix_arm_sd.img || { echo "minix_arm_sd.img не найден -- сначала: make -C docker -f build.mk sdimage" >&2; exit 1; }; \
		 qemu-system-arm -M beaglexm -serial stdio -drive if=sd,cache=writeback,file=minix_arm_sd.img'

clean-obj:
	rm -rf $(REPO_ROOT)/obj/$(ARCH)$(FLAVOR)

clean-image:
	-docker rmi $(IMAGE)
