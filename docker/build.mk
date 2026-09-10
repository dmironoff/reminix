# ReMinix — Docker-обёртка над build.sh/releasetools.
#
# Ничего не меняет и не подменяет в самой системе сборки (build.sh,
# releasetools/*.sh) — только даёт воспроизводимое хостовое окружение
# (см. Dockerfile) и удобные команды поверх него. Подробности и
# обоснование выбора пакетов — docs/docker-build.md.
#
# Запускать из корня репозитория:
#
#   make -C docker -f build.mk image                    # собрать образ окружения
#   make -C docker -f build.mk shell                     # интерактивная оболочка (ARCH=i386 по умолчанию)
#   make -C docker -f build.mk shell ARCH=evbearm-el     # то же, под другую архитектуру
#   make -C docker -f build.mk build ARCH=amd64 BUILD_TARGET=tools
#   make -C docker -f build.mk hdimage                   # собрать minix_x86.img (i386, releasetools/x86_hdimage.sh)
#   make -C docker -f build.mk sdimage                   # собрать minix_arm_sd.img (evbearm-el/BeagleBoard-xM)
#   make -C docker -f build.mk qemu-hdimage              # запустить x86-образ в QEMU
#   make -C docker -f build.mk qemu-sdimage              # запустить ARM SD-образ в QEMU
#   make -C docker -f build.mk clean-obj ARCH=i386       # снести obj/i386 и пересобрать с нуля
#
# Состояние сборки (объектные файлы, DESTDIR, releasedir) живёт в
# obj/<ARCH> внутри репозитория — эта директория уже покрыта
# .gitignore (шаблон "obj") и переживает между запусками контейнера,
# так что бутстрап тулчейна не повторяется на каждый вызов.

SHELL := /bin/sh

IMAGE_NAME  ?= reminix-build
IMAGE_TAG   ?= latest
IMAGE       := $(IMAGE_NAME):$(IMAGE_TAG)

ARCH         ?= i386
JOBS         ?= $(shell nproc 2>/dev/null || echo 1)
BUILD_TARGET ?= release
BUILDVARS    ?=

HOST_UID := $(shell id -u)
HOST_GID := $(shell id -g)

REPO_ROOT      := $(abspath $(CURDIR)/..)
CONTAINER_REPO := /work/reminix

# Через "=" (не ":="), чтобы честно пересчитывались при
# ARCH-переопределении конкретной цели (см. hdimage/sdimage ниже).
CONTAINER_OBJ    = $(CONTAINER_REPO)/obj/$(ARCH)
CONTAINER_DEST   = $(CONTAINER_OBJ)/destdir.$(ARCH)
# ВАЖНО: RELEASEDIR должен быть "голым" (без .../$(ARCH)/binary) --
# build.sh сам добавляет "${RELEASEMACHINEDIR}/binary/sets" при сборке
# release-наборов (см. build.sh: `-R release  Set RELEASEDIR to
# release. [Default: releasedir]` и `setdir=${RELEASEDIR}/${RELEASEMACHINEDIR}/binary/sets`
# в реализации release-таргета). Если передать уже с суффиксом (как
# по ошибке делали раньше, повторяя дефолт releasetools/image.defaults
# -- у него РАЗНЫЕ переменные RELEASEDIR и SETS_DIR, и только SETS_DIR
# включает "$(ARCH)/binary/sets"), build.sh удвоит суффикс и
# releasetools/x86_hdimage.sh не найдёт наборы по ожидаемому пути
# (SETS_DIR считается отдельно, из OBJ/ARCH напрямую, и с удвоенным
# RELEASEDIR никогда не совпадёт). См. docs/build-x86.md §2/§3.
CONTAINER_RELDIR = $(CONTAINER_OBJ)/releasedir

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
	-e JOBS=$(JOBS)

DOCKER_RUN = $(DOCKER_RUN_BASE) $(IMAGE)

.PHONY: help image shell build hdimage sdimage qemu-hdimage qemu-sdimage clean-obj clean-image

help:
	@sed -n '2,22p' $(lastword $(MAKEFILE_LIST))

image:
	docker build \
		--build-arg BUILD_UID=$(HOST_UID) \
		--build-arg BUILD_GID=$(HOST_GID) \
		-t $(IMAGE) \
		-f Dockerfile \
		.

shell: image
	$(DOCKER_RUN_BASE) -it $(IMAGE) bash

# Прямой вызов build.sh — без упаковки в конкретный образ диска.
# Годится и для "как есть" непортированных пока архитектур (amd64 и
# т.д.): дойдёт настолько далеко, насколько дерево уже поддерживает
# ARCH, что и является дымовым тестом прогресса портирования.
build: image
	$(DOCKER_RUN) bash -lc '\
		mkdir -p "$$OBJ" && \
		sh build.sh -j "$$JOBS" -m "$$ARCH" -O "$$OBJ" -D "$$DESTDIR" $(BUILDVARS) -U -u $(BUILD_TARGET)'

# Архитектура зафиксирована в самих releasetools-скриптах (см.
# docs/build-x86.md / docs/build-arm32.md) — здесь только пробрасываем
# соответствующий ARCH, чтобы obj/<arch> совпадал с тем, что построит
# сам скрипт.
hdimage: ARCH := i386
hdimage: image
	$(DOCKER_RUN) bash -lc './releasetools/x86_hdimage.sh'

sdimage: ARCH := evbearm-el
sdimage: image
	$(DOCKER_RUN) bash -lc './releasetools/arm_sdimage.sh'

qemu-hdimage: ARCH := i386
qemu-hdimage: image
	$(DOCKER_RUN_BASE) $(KVM_DEVICE) $(KVM_GROUPADD) $(IMAGE) bash -lc \
		'test -f minix_x86.img || { echo "minix_x86.img не найден -- сначала: make -C docker -f build.mk hdimage" >&2; exit 1; }; \
		 qemu-system-i386 $(if $(KVM_DEVICE),--enable-kvm,) -m 256 -drive file=minix_x86.img,format=raw,if=ide'

qemu-sdimage: ARCH := evbearm-el
qemu-sdimage: image
	$(DOCKER_RUN_BASE) $(IMAGE) bash -lc \
		'test -f minix_arm_sd.img || { echo "minix_arm_sd.img не найден -- сначала: make -C docker -f build.mk sdimage" >&2; exit 1; }; \
		 qemu-system-arm -M beaglexm -serial stdio -drive if=sd,cache=writeback,file=minix_arm_sd.img'

clean-obj:
	rm -rf $(REPO_ROOT)/obj/$(ARCH)

clean-image:
	-docker rmi $(IMAGE)
