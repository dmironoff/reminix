# Makefile for REVM server
.include <bsd.own.mk>

PROG=	vm
SRCS=	main.c acl.c apt_utils.c break.c exit.c fork.c fscache.c mmap_utils.c pagefault.c \
		rs.c sysmmap.c usageinfo.c vfs.c


.if ${MKPAE:Uno} != "no"
CPPFLAGS+= -DPAE=1
.endif

DPADD+=	${LIBSYS} ${LIBEXEC}
LDADD+=	-lsys -lexec

CPPFLAGS+= -I${.CURDIR} -I${.CURDIR}/arch/${MACHINE_ARCH}
CPPFLAGS+= -I${NETBSDSRCDIR}/minix

# For all other services, magic instrumentation involves instrumenting the
# libc malloc code, hooking its nested mmap/munmap calls, and ignoring its
# data. For VM, we need to do the exact opposite, since for VM, the malloc
# state is transferred as is. Thus, if the magic pass is enabled, tell it
# to skip the regular malloc instrumentation features.
MAGICFLAGS= -magic-disable-mem-functions -magic-disable-malloc-skip

.include <minix.service.mk>