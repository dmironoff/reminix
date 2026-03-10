#
# BSP for TI hardware
#

.PATH: ${.CURDIR}/arch/${MACHINE_ARCH}/bsp/allwinner/h3

.for unpaged_obj in h3_serial.o h3_rtc.o h3_reset.o
BSP_OBJS_UNPAGED += ${unpaged_obj}
.endfor

SRCS+=	h3_init.c h3_serial.c h3_timer.c h3_padconf.c h3_intr.c h3_rtc.c \
	h3_reset.c

