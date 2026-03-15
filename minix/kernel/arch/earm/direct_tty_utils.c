/*
 * Этот файл, как по мне так очень дикий костыль,
 * но в данной архитектуре это единственный способ выводить куда-то информацию
 * о процессе инициализации ядра до того как будет запущен драйвер tty
 *
 * Нам совершенно не нужно не читать tty, ни очищать экран
 * Данные функции здесь заглушки
 *
 * Мы используем только вывод данных в серийный порт для архитектуры ARM
 */


#include "kernel/kernel.h"
#include "direct_utils.h"
#include "bsp_serial.h"
#include "glo.h"

void direct_cls(void)
{
    /* Do nothing */
}

void direct_print_char(char c)
{
	if(c == '\n')
        bsp_ser_putc('\r');

    bsp_ser_putc(c);
}

void direct_print(const char *str)
{
	while (*str) {
		direct_print_char(*str);
		str++;
	}
}

int direct_read_char(unsigned char *ch)
{
	return 0;
}
