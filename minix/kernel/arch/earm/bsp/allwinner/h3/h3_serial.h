#ifndef _H3_SERIAL_H
#define _H3_SERIAL_H

/*
 * Здесь у нас находятся только базовые регистры для UART0
 * Он на этой плате выведен в отдельный трёхпиной разъём
 * И на схеме указан как debug uart
 *
 *
 */

#define H3_UART0_BASE 0x01C28000
#define H3_UART_SIZE 0x1000

#define H3_UART_RBR 0x00
#define H3_UART_THR 0x00
#define H3_UART_DLL 0x00
#define H3_UART_DLH 0x04
#define H3_UART_IER 0x04
#define H3_UART_IIR 0x08
#define H3_UART_FCR 0x08
#define H3_UART_LCR 0x0C
#define H3_UART_MCR 0x10
#define H3_UART_LSR 0x14
#define H3_UART_MSR 0x18
#define H3_UART_SCH 0x1C
#define H3_UART_USR 0x7C
#define H3_UART_TFL 0x80
#define H3_UART_RFL 0x84
#define H3_UART_HALT 0xA4

#define H3_UART_LSR_THRE (1 << 5)
#define H3_UART_LSR_TEMT (1 << 6)

#endif /* _H3_SERIAL_H */
