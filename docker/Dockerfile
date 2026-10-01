# ReMinix cross-build environment.
#
# This image supplies only the *host*-side prerequisites for
# `build.sh` / `releasetools/*.sh`. The actual cross-compiler (GCC,
# LLVM/Clang, binutils) and every other build tool (nbmake, pax, mtree,
# makefs, ...) are bootstrapped FROM SOURCE by build.sh itself, out of
# tools/, the first time it runs -- this image does not pre-build or
# replace any of that, and does not need updating when a new target
# architecture is added to minix/kernel/arch/. See docs/docker-build.md
# for the full rationale and a package-by-package justification.
ARG BASE_IMAGE=ubuntu:24.04

# ---------------------------------------------------------------------
# Stage: gcc-7, used only to bootstrap nbmake
# ---------------------------------------------------------------------
# build.sh bootstraps its own copy of nbmake (usr.bin/make, ancient
# NetBSD bmake sources) using whatever compiler $HOST_CC names -- see
# build.sh, function rebuildmake(): "env CC=${HOST_CC-cc} ... configure
# && buildmake.sh". That's the *only* place build.sh reads HOST_CC; the
# real cross-compiler bootstrap (tools/gcc, tools/llvm*, ...) is a
# separate, unrelated path and isn't affected by it.
#
# Confirmed by hands-on builds of this exact tree: nbmake fails to
# build with a modern host gcc (Ubuntu 24.04's default is gcc-13) --
# most likely because gcc 10+ switched its default from -fcommon to
# -fno-common, which the old K&R-style bmake sources (relying on
# tentative global definitions repeated across files) trip over as
# "multiple definition" link errors. gcc-7, the last release line
# before that default changed, builds it fine.
#
# ubuntu:24.04 no longer carries a gcc-7 package (dropped after 20.04),
# and copying a prebuilt gcc-7 .deb from an unrelated EOL Ubuntu
# release risks a glibc/library-version mismatch against this image's
# userspace. So it's built from the official GNU sources instead,
# natively on this same base image: slower the first time this image
# is built (Docker caches the layer afterwards), but reproducible
# independent of whatever an old-releases mirror or PPA happens to
# serve years from now.
FROM ${BASE_IMAGE} AS gcc7-builder
ARG DEBIAN_FRONTEND=noninteractive
ARG GCC7_VERSION=7.5.0
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential wget ca-certificates xz-utils \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /usr/src
RUN wget -q https://ftp.gnu.org/gnu/gcc/gcc-${GCC7_VERSION}/gcc-${GCC7_VERSION}.tar.xz \
    && tar xf gcc-${GCC7_VERSION}.tar.xz \
    && rm gcc-${GCC7_VERSION}.tar.xz
WORKDIR /usr/src/gcc-${GCC7_VERSION}
# Fetches pinned gmp/mpfr/mpc source snapshots into the tree -- avoids
# any dependency on whichever libgmp-dev/libmpfr-dev happen to be
# packaged for the (much newer) base image.
# gcc-7's download_prerequisites fetches from ftp://gcc.gnu.org, which no
# longer answers reliably (wget then hangs forever). The same files are
# served over HTTPS from the same host; checksums are still verified
# against contrib/prerequisites.sha512.
RUN sed -i 's|ftp://gcc.gnu.org/pub/gcc/infrastructure/|https://gcc.gnu.org/pub/gcc/infrastructure/|' \
        ./contrib/download_prerequisites \
 && grep -q 'https://gcc.gnu.org/pub/gcc/infrastructure/' ./contrib/download_prerequisites \
 && ./contrib/download_prerequisites
RUN mkdir /usr/src/build
WORKDIR /usr/src/build
# gcc-7 here is only ever a bootstrap *tool* for nbmake, not a
# production compiler, so a single-stage build (using this image's own
# host gcc to compile it, --disable-bootstrap) is enough and
# considerably faster than gcc's normal 3-stage self-bootstrap.
#
# --program-suffix=-7: plain "make install" from upstream GCC sources
# installs binaries named just "gcc"/"g++" (the "gcc-7" naming is a
# distro packaging convention, not something GCC's own build system
# does on its own) -- pin the suffix explicitly so the binary really is
# at .../bin/gcc-7, matching HOST_CC below.
RUN /usr/src/gcc-${GCC7_VERSION}/configure \
        --prefix=/opt/gcc-7 \
        --program-suffix=-7 \
        --enable-languages=c,c++ \
        --disable-multilib \
        --disable-bootstrap \
        --disable-werror \
        --disable-libsanitizer \
    && make -j"$(nproc)" \
    && make install-strip \
    && rm -rf /usr/src/gcc-${GCC7_VERSION} /usr/src/build

# ---------------------------------------------------------------------
# QEMU from upstream sources, pinned by version (docs/devicetree.md §1).
# Ubuntu 24.04 ships QEMU 8.2; the project needs a current release for
# -M orangepi-pc (reference ARM board) and a recent i386/x86_64. Only the
# system emulators the project uses are built; add riscv64/mips64 here
# when those ports start.
FROM ${BASE_IMAGE} AS qemu-builder
ARG DEBIAN_FRONTEND=noninteractive
ARG QEMU_VERSION=11.1.0
RUN set -eu; \
    . /etc/os-release; \
    { \
      printf 'deb http://archive.ubuntu.com/ubuntu %s main restricted universe multiverse\n' "$VERSION_CODENAME"; \
      printf 'deb http://archive.ubuntu.com/ubuntu %s-updates main restricted universe multiverse\n' "$VERSION_CODENAME"; \
      printf 'deb http://security.ubuntu.com/ubuntu %s-security main restricted universe multiverse\n' "$VERSION_CODENAME"; \
    } > /etc/apt/sources.list
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential pkg-config ninja-build python3 python3-venv \
        flex bison wget ca-certificates xz-utils bzip2 \
        libglib2.0-dev libpixman-1-dev libfdt-dev libslirp-dev zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /usr/src
RUN wget -q https://download.qemu.org/qemu-${QEMU_VERSION}.tar.xz \
    && tar xf qemu-${QEMU_VERSION}.tar.xz \
    && rm qemu-${QEMU_VERSION}.tar.xz
WORKDIR /usr/src/qemu-${QEMU_VERSION}/build
RUN ../configure \
        --prefix=/opt/qemu \
        --target-list=i386-softmmu,x86_64-softmmu,arm-softmmu,aarch64-softmmu \
        --enable-kvm --enable-slirp --enable-fdt=system \
        --disable-docs --disable-werror \
    && make -j"$(nproc)" \
    && make install \
    && /opt/qemu/bin/qemu-system-i386 --version

# ---------------------------------------------------------------------
# Final image
# ---------------------------------------------------------------------
FROM ${BASE_IMAGE}

ARG DEBIAN_FRONTEND=noninteractive

# Ubuntu's default sources.list on the docker base image only has
# "main". qemu-system-* and a few build deps below live in
# universe/multiverse.
RUN set -eu; \
    . /etc/os-release; \
    { \
      printf 'deb http://archive.ubuntu.com/ubuntu %s main restricted universe multiverse\n' "$VERSION_CODENAME"; \
      printf 'deb http://archive.ubuntu.com/ubuntu %s-updates main restricted universe multiverse\n' "$VERSION_CODENAME"; \
      printf 'deb http://security.ubuntu.com/ubuntu %s-security main restricted universe multiverse\n' "$VERSION_CODENAME"; \
    } > /etc/apt/sources.list

RUN apt-get update && apt-get install -y --no-install-recommends \
        # --- what build.sh itself needs on the host: a C/C++ compiler
        # and the standard POSIX toolset it bootstraps nbmake/pax/mtree/
        # makefs/... with (see docs/docker-build.md §2) ---
        build-essential \
        gawk \
        m4 \
        bison \
        flex \
        # --- fetching sources the tree itself doesn't vendor: U-Boot
        # fork for ARM (fetch_u-boot.sh), GRUB upstream for x86 EFI
        # (fetch_and_build_grub in x86_hdimage.sh) ---
        git \
        curl \
        ca-certificates \
        # --- unpacking sets/sources, general archive handling ---
        xz-utils \
        bzip2 \
        unzip \
        rsync \
        # --- releasetools/sort_set.pl, and GRUB's own build scripts ---
        perl \
        python3 \
        # --- building GRUB for the x86 EFI path (fetch_and_build_grub) ---
        autoconf \
        automake \
        pkg-config \
        gettext \
        texinfo \
        # --- linked into the host binutils/gcc/llvm that build.sh
        # bootstraps out of tools/ ---
        zlib1g-dev \
        libssl-dev \
        # --- QEMU: the emulators themselves come from the qemu-builder
        # stage above (/opt/qemu); here only qemu-img and the shared
        # libraries the self-built emulators link against ---
        qemu-utils \
        libglib2.0-0t64 \
        libpixman-1-0 \
        libfdt1 \
        libslirp0 \
        # --- quality of life for interactive `make -C docker shell` use ---
        sudo \
        less \
        vim-tiny \
        locales \
    && rm -rf /var/lib/apt/lists/*

RUN locale-gen en_US.UTF-8
ENV LANG=en_US.UTF-8 LC_ALL=en_US.UTF-8

# gcc-7 from the builder stage above, wired in through HOST_CC/HOST_CXX
# -- the two variables the NetBSD-style build system uses for every
# *host*-run GNU-autoconf tool it builds out of tools/ (nbmake's own
# bootstrap in build.sh's rebuildmake(), and -- confirmed by tracing
# share/mk/bsd.host.mk + tools/Makefile.gnuhost -- every tools/<pkg>
# build too: Makefile.gnuhost's CONFIGURE_ENV sets CC=${HOST_CC} and
# CXX=${HOST_CXX} for the ./configure + make of each one).
#
# HOST_CC alone was enough to get nbmake building, but tools/binutils
# (which builds host-run binutils/gold as a build tool) then failed
# separately: bsd.host.mk defaults HOST_CXX?=c++ independently of
# HOST_CC, so CXX stayed pinned at the system g++-13, and gold's ~2013
# C++ sources (external/gpl3/binutils/dist/gold/errors.h and friends)
# don't compile under it -- 'std::string' is used without #include
# <string>, relying on an implicit transitive include that newer
# libstdc++ headers no longer provide (removed as part of GCC's
# standard-library header cleanup around GCC 11). Same generation of
# problem as the nbmake/-fcommon one, just in a different host tool
# and needing HOST_CXX instead of/alongside HOST_CC.
#
# Deliberately NOT put ahead of the system gcc-13 in PATH and NOT
# exported as plain CC/CXX: every other build step (the real
# cross-compiler bootstrap, GRUB's own ./configure, ...) should keep
# using the image's normal, current host compiler -- only the
# HOST_CC/HOST_CXX variables this build system reads for its own
# host-tool builds should point at gcc-7/g++-7.
COPY --from=gcc7-builder /opt/gcc-7 /opt/gcc-7
COPY --from=qemu-builder /opt/qemu /opt/qemu
# Self-built QEMU first in PATH; fail the image build right here if a
# runtime library is missing.
ENV PATH="/opt/qemu/bin:${PATH}"
RUN qemu-system-i386 --version && qemu-system-arm --version
ENV PATH="${PATH}:/opt/gcc-7/bin"
ENV HOST_CC=/opt/gcc-7/bin/gcc-7
ENV HOST_CXX=/opt/gcc-7/bin/g++-7

# Unprivileged build user: build.sh is normally run with -U (MKUNPRIVED)
# and building as root is a documented bad idea for NetBSD-style trees.
# UID/GID come in as build args and should be set to match the host user
# (docker/build.mk does this automatically), so that files written into
# the bind-mounted source tree come back owned by the host user instead
# of root.
#
# Recent ubuntu:24.04 images already ship a "ubuntu" user/group at
# 1000:1000 -- the single most common host UID/GID out there (first
# user on any fresh Linux install). If BUILD_UID/BUILD_GID collide with
# something that's already in the image, drop it first: we don't need
# that account, and useradd/groupadd would otherwise fail with "not
# unique".
ARG BUILD_UID=1000
ARG BUILD_GID=1000
RUN existing_user="$(getent passwd ${BUILD_UID} | cut -d: -f1 || true)"; \
    if [ -n "$existing_user" ] && [ "$existing_user" != "builder" ]; then \
        userdel -r "$existing_user" 2>/dev/null || userdel "$existing_user"; \
    fi; \
    existing_group="$(getent group ${BUILD_GID} | cut -d: -f1 || true)"; \
    if [ -n "$existing_group" ] && [ "$existing_group" != "builder" ]; then \
        groupdel "$existing_group"; \
    fi; \
    groupadd -g ${BUILD_GID} builder \
    && useradd -m -u ${BUILD_UID} -g ${BUILD_GID} -s /bin/bash builder \
    && echo 'builder ALL=(ALL) NOPASSWD:ALL' > /etc/sudoers.d/builder

RUN mkdir -p /work/reminix && chown -R ${BUILD_UID}:${BUILD_GID} /work

WORKDIR /work/reminix
USER builder

# Deliberately no ENTRYPOINT: docker/build.mk always passes an explicit
# command ("bash -lc '...'" for one-shot runs, "bash" for the
# interactive shell target). An ENTRYPOINT here would get that command
# appended as *its* arguments instead of replacing it -- e.g. "bash -lc
# cmd" would run as "<entrypoint> bash -lc cmd", not as "bash -lc cmd".
