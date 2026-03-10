#
# BSP for TI hardware
#

.PATH: ${.CURDIR}/arch/${MACHINE_ARCH}/bsp/ti/am335x

.for unpaged_obj in am335x_serial.o am335x_rtc.o am335x_reset.o
BSP_OBJS_UNPAGED += ${unpaged_obj}
.endfor

SRCS+=	am335x_init.c am335x_serial.c am335x_timer.c am335x_padconf.c am335x_intr.c am335x_rtc.c \
	am335x_reset.c

