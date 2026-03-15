//
// Created by dmironov on 12.03.2026.
//

/*
 *
 * В архитектуре arm и внутри их готовых ядер есть
 * промышленный стандарт таймера тиков системы
 * Предполагается использовать его для разделения процессорного времени.
 * Вся прелесть этого таймера в том что он очень быстро общается с ядрами процессора
 * а так же работает от нативных 24MHz, короче быстрая и удобная штука
 * Так как этот таймер поддерживается не всеми процессорами
 * то он вынесен в отдельный каталог так же как и gic
 *
 * Это простая реализация драйвера для Generic Timer
 * Для большей скорости работы я просто написал кучу inline функций
 * Которые могут быть использованы в bsp
 */

#ifndef REMINIX_GENERIC_TIMER_H
#define REMINIX_GENERIC_TIMER_H

#define GENERIC_TIMER_IRQ_NUMBER    30


/*
 * Получение частоты работы таймера
 */
static inline uint32_t get_cntfrq(void) {
    uint32_t val;
    __asm__ __volatile__("mrc p15, 0, %0, c14, c0, 0" : "=r"(val));
    return val;
}

/*
 * Запись нового значения частоты
 * хз зачем, мне 24Mhz нравится
 * Теоритически это типа делается всего один раз при инициализации soc
 */
static inline void set_cntfrq(uint32_t new_frq) {
    __asm__ __volatile__("mcr p15, 0, %0, c14, c0, 0; isb" : : "r"(new_frq));
}

/*
 * Включение таймера
 *
 */

static inline void enable_timer(void) {
    uint32_t current_value = 0;
    __asm__ __volatile__("isb; mrc p15, 0, %0, c14, c2, 1" : "=r"(current_value));
    current_value = current_value | 1;
    __asm__ __volatile__("mcr p15, 0, %0, c14, c2, 1; isb" : : "r"(current_value));
}


/*
 * Выключение таймера
 */

static inline void disable_timer(void) {
    uint32_t current_value = 0;
    __asm__ __volatile__("isb; mrc p15, 0, %0, c14, c2, 1" : "=r"(current_value));
    current_value = current_value & (~((uint32_t) 1));
    __asm__ __volatile__("mcr p15, 0, %0, c14, c2, 1; isb" : : "r"(current_value));
}

/*
 * Демаскировка прерывания таймера
 *
 */

static inline void unmask_timer_irq(void) {
    uint32_t current_value = 0;
    __asm__ __volatile__("isb; mrc p15, 0, %0, c14, c2, 1" : "=r"(current_value));
    current_value = current_value & (~((uint32_t) (1 << 1)));
    __asm__ __volatile__("mcr p15, 0, %0, c14, c2, 1; isb" : : "r"(current_value));
}


/*
 * Маскировка прерывания таймера
 */

static inline void mask_timer_irq(void) {
    uint32_t current_value = 0;
    __asm__ __volatile__("isb; mrc p15, 0, %0, c14, c2, 1" : "=r"(current_value));
    current_value = current_value | ((uint32_t)(1 << 1));
    __asm__ __volatile__("mcr p15, 0, %0, c14, c2, 1; isb" : : "r"(current_value));
}

/*
 * Получаем значение счётчика
 * оно 64 битное, в отличии от всех остальных регистров таймера
 */

static inline uint64_t read_cntpct(void) {
    uint32_t low, high;
    __asm__ __volatile__("isb; mrrc p15, 0, %0, %1, c14" : "=r"(low), "=r"(high));
    return ((uint64_t)high << 32) | low;
}

/*
 * Устанавливаем интервал следующего тика
 * несмотря на то что значение счетчика у нас 64 разряда
 * интервал тика будет 32 разряда
 */

static inline void set_next_tick(uint32_t value) {
    __asm__ __volatile__("mcr p15, 0, %0, c14, c2, 0; isb" : : "r"(value));
}

/*
 * Читаем значение интервала следующего тика
 */

static inline uint32_t read_next_tick(void) {
    uint32_t value = 0;
    __asm__ __volatile__("isb; mrc p15, 0, %0, c14, c2, 0" : "=r"(value));
    return value;
}


#endif //REMINIX_GENERIC_TIMER_H
