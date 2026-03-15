/* Physical memory layout */

#ifndef _ARM_MEMORY_H
#define _ARM_MEMORY_H

/*
 * Здесь мы описываем адресацию памяти DDR
 * H3 поддерживает до 2G DDR памяти, но мы пока размапим первый гектар
 * Как по мне так это грязнючий костыль и надо бы перенести это в FDT
 * Но пока мы это оставим именно так что бы наш процессор просто запускался
 *
 */


#define PHYS_MEM_BEGIN 0x40000000
#define PHYS_MEM_END 0x80000000

#endif /* _ARM_MEMORY_H */
