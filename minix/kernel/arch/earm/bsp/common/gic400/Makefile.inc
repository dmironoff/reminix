
.PATH: ${.CURDIR}/arch/earm/bsp/common/gic400

SRCS+=	gic.c

CPPFLAGS+= -I${NETBSDSRCDIR}/minix/kernel/arch/earm/bsp/common/gic400/