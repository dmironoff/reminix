/*
 * Простенький драйвер UART для вывода информации во время старта системы
 * до загрузки MMU и старта драйвера tty
 * Мы работаем на стандартных настройках чипа
 * ОН работает на скорости 115200 8 бит 1 стопбит
 *
 */


#include <assert.h>
#include <sys/types.h>
#include <machine/cpu.h>
#include <minix/type.h>
#include <minix/board.h>
#include <io.h>

#include "kernel/kernel.h"
#include "kernel/proc.h"
#include "kernel/vm.h"
#include "kernel/proto.h"
#include "arch_proto.h"

#include "h3_serial.h"

struct h3_serial
{
	vir_bytes base;
	vir_bytes size;
};

static struct h3_serial h3_serial = {
	.base = 0,
};

static kern_phys_map serial_phys_map;

void
bsp_ser_init(void)
{
    h3_serial.base = H3_UART0_BASE;
	h3_serial.size = H3_UART_SIZE;

	kern_phys_map_ptr(h3_serial.base, h3_serial.size,
	    VMMF_UNCACHED | VMMF_WRITE, &serial_phys_map,
	    (vir_bytes) & h3_serial.base);

    assert(h3_serial.base);
}

void
bsp_ser_putc(char c)
{
	int i;
	assert(h3_serial.base);

	/* Ждём пока регистр передачи будет чистым */
	for (i = 0; i < 100000; i++) {
		if (mmio_read(h3_serial.base + H3_UART_LSR) & H3_UART_LSR_THRE) {
			break;
		}
	}

	/* Отправляем */
	mmio_write(h3_serial.base + H3_UART_THR, c);

	/* И снова ждём отправки что бы избежать перезаписывания данных
	 * в случае если какая-то другая часть системы будет туда отправлять данные
	 * например у нас уже стартанул драйвер tty
	 * а ядру нужно срочно отправить какую-то дичь на прямую */
	for (i = 0; i < 100000; i++) {
		if (mmio_read(h3_serial.base + H3_UART_LSR) & (H3_UART_LSR_THRE | H3_UART_LSR_TEMT)) {
			break;
		}
	}
}
