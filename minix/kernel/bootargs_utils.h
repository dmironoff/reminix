//
// Created by dmironov on 19.03.2026.
//

#ifndef REMINIX_BOOTARGS_UTILS_H
#define REMINIX_BOOTARGS_UTILS_H

#include <sys/types.h>

int bootargs_get_value(char *bootargs, char *var_name, char *value, u32_t value_max_len);
int bootargs_to_params(const char *bootargs, char *params);

#endif //REMINIX_BOOTARGS_UTILS_H
