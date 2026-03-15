#ifndef _H3_TIMER_REGISTERS_H
#define _H3_TIMER_REGISTERS_H


/*
 * Это не те регистры, вернее не тот таймер что мне нужен для разделения процессорного времени
 * Но так как я уже написал этот кусок кода, то пусть он тут полежит
 * Позже использую его для драйвера переферийных таймеров
 *
 */

#define H3_TIMER_BASE 0x012C20C00
#define H3_TIMER_SIZE 0x1000

#define H3_TIMER_IRQ_EN 0x00
#define H3_TIMER_IRQ_STA 0x04
#define H3_TIMER0_CTRL  0x10
#define H3_TIMER0_INTV_VALUE 0x14
#define H3_TIMER0_CUR_VALUE 0x18
#define H3_TIMER1_CTRL  0x20
#define H3_TIMER1_INTV_VALUE 0x24
#define H3_TIMER1_CUR_VALUE 0x28
#define H3_TIMER_AVS_CNT_CTL 0x80
#define H3_TIMER_AVS_CNT0 0x84
#define H3_TIMER_AVS_CNT1 0x88
#define H3_TIMER_AVS_DIV 0x8C
#define H3_TIMER_WDOG0_IRQ_EN 0xA0
#define H3_TIMER_WDOG0_IRQ_STA 0xA4
#define H3_TIMER_WDOG0_CTRL 0xB0
#define H3_TIMER_WDOG0_CFG 0xB4
#define H3_TIMER_MODE 0xB8


#endif /* _H3_TIMER_REGISTERS_H */
