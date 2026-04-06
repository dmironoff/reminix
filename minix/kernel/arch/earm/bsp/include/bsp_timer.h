#ifndef _BSP_TIMER_H_
#define _BSP_TIMER_H_

#ifndef __ASSEMBLY__

void bsp_timer_init(unsigned freq);
void bsp_timer_stop(void);
int  bsp_register_timer_handler(const irq_handler_t handler);
void bsp_timer_int_handler(void);
uint32_t bsp_timer_tsc_per_ms(void);

#endif /* __ASSEMBLY__ */

#endif /* _BSP_TIMER_H_ */
