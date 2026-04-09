#include "kernel/kernel.h"
#include "kernel/clock.h"
#include <sys/types.h>
#include <machine/cpu.h>
#include <minix/board.h>
#include <minix/mmio.h>
#include <assert.h>
#include <io.h>
#include <stdlib.h>
#include <stdio.h>
#include "arch_proto.h"
#include "bsp_timer.h"
#include "bsp_intr.h"
#include "generic_timer.h"

/* interrupt handler hook */
static irq_hook_t h3_timer_hook;

/*
 * Это переменная, потому что я буду её вычислять относительно частоты источника тактирования таймера
 * Не знаю насколько это ликвидно, но мне кажется это правило хорошего тона
 * Хотя я видел как к процессорам прикручивают вместо 24Mhz более удобную частоту
 * что бы добиться, например, какой-то нестандартной скорости работы UART или другой подсистемы
 *
 */
static uint32_t tick_interval = 0;

/*
 * Регистрируем свой обработчик прерывания для таймера
 */

int bsp_register_timer_handler(const irq_handler_t handler)
{
	/* Initialize the CLOCK's interrupt hook. */
	h3_timer_hook.proc_nr_e = NONE;
	h3_timer_hook.irq = GENERIC_TIMER_IRQ_NUMBER;

	put_irq_handler(&h3_timer_hook, GENERIC_TIMER_IRQ_NUMBER, handler);

	bsp_irq_unmask(GENERIC_TIMER_IRQ_NUMBER);
    unmask_timer_irq();

	return 0;
}

void
bsp_timer_init(unsigned freq)
{
    uint32_t timer_clock = 0;
    /*
     * Расчитаем частоту прерываний от таймера
     *
     */
    timer_clock =  get_cntfrq();
    tick_interval = timer_clock / 1000; // Пока у нас будет тик каждую миллесекунду
    arm_frclock.hz = timer_clock;
    arm_frclock.tcrr = 0x0;
    set_next_tick(tick_interval);
    mask_timer_irq(); /* Отключим генерацию прерывания до регистрации обработчика прерываний */
    enable_timer();
}

void
bsp_timer_stop(void)
{
    disable_timer();
}

void
bsp_timer_int_handler(void)
{
    /* Что бы сбросить линию прерывания на процессоре нам нужно переустановить интервал */
    set_next_tick(tick_interval);
}

/*
 * Ещё одна информационная функция, которая зависит от реализации процессора
 */
uint32_t bsp_timer_tsc_per_ms(void) {
    return 24000;
}

/* Читаем 64 разрядное значение таймера */
void
read_tsc_64(u64_t * t)
{
	*t = (u64_t) read_cntpct();
}
