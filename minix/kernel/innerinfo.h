//
// Created by dmironov on 28.03.2026.
//

#ifndef REMINIX_INNERINFO_H
#define REMINIX_INNERINFO_H

typedef struct {
#ifdef __arm__
    vir_bytes                      fdt_addr;
    vir_bytes                      kernel_pt; // Виртуальный адрес отдельной таблицы страниц ядра для регистра ttbr1
#endif

    vir_bytes                       user_pt_base; // Виртуальный базовый адрес для физических таблиц страниц

} inner_kernel_info_t;

#endif //REMINIX_INNERINFO_H
