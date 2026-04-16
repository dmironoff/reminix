#
# BSP for TI hardware
#

.PATH: ${.CURDIR}/arch/${MACHINE_ARCH}/bsp/allwinner/h3

CPPFLAGS+= -DARCH_ARM_CORTEX_A7=1

.for unpaged_obj in h3_serial.o h3_rtc.o h3_reset.o h3_devices_mmap.o h3_smp_info.o h3_cpu.o
BSP_OBJS_UNPAGED += ${unpaged_obj}
.endfor

SRCS+=	h3_init.c h3_serial.c h3_timer.c h3_padconf.c h3_intr.c h3_rtc.c \
	h3_reset.c h3_devices_mmap.c h3_smp_info.c h3_cpu.c

.include  "${NETBSDSRCDIR}/minix/kernel/arch/earm/bsp/common/gic400/Makefile.inc"
.include  "${NETBSDSRCDIR}/minix/kernel/arch/earm/bsp/common/generic_timer/Makefile.inc"


