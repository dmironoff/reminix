//
// Created by dmironov on 19.03.2026.
//

#include "bootargs_utils.h"
#include "env_params_utils.h"

#define BOOTARG_ENTRY_LEN       25

/*
 * Получение значения переменной из параметров командной строки по имени
 */
int bootargs_get_value(char *bootargs, char *var_name, char *value, u32_t value_max_len) {
    char *iter, *varp;
    int var_len = 0;
    int bootargs_len = 0;
    int match_len = 0;
    int value_len = 0;

    if (bootargs == 0 || var_name == 0 || value == 0) {
        return 0;
    }

    for(iter = bootargs; *iter != '\0'; iter++, bootargs_len++);
    for(iter = var_name; *iter != '\0'; iter++, var_len++);

    if (bootargs_len == 0 || var_len == 0) {
        return 0;
    }

    for(iter = bootargs, varp = var_name; *iter != '\0' && match_len < var_len; iter++) {
        if (*varp == *iter) {
            varp++;
            match_len++;
            continue;
        }
        match_len = 0;
        varp = var_name;
    }

    if (match_len == var_len) {
        iter++;
        if (*iter == '=' ) {
            while(*iter != '\0' && *iter != ' ' && value_len  + 1< value_max_len) {
                *value++ = *iter++;
                value_len++;
            }
            *value='\0';
        }
        return 1;
    }

    return 0;
}

/*
 * Готовая функция по экспорту параметров командной строки в переменные окружения
 */
int bootargs_to_params(const char *bootargs, char *params) {
    char *iter;
    char variable[BOOTARG_ENTRY_LEN];
    char value[BOOTARG_ENTRY_LEN];
    int variable_len = 0;
    int value_len = 0;
    int imported_values = 0;


    for (iter = (char *)bootargs; *iter != '\0'; iter++) {
        if (*iter != ' ') {
           if (variable_len && !value_len) {
               variable[variable_len] = *iter;
               variable_len++;
               if (*(iter + 1) == '=') {
                   variable[variable_len] = '\0';
                   iter += 2;
                   value[value_len] = *iter;
                   value_len++;
               } else if (*(iter + 1) == '\0') {
                   variable[variable_len] = '\0';
               }  else if (*(iter + 1) == ' ') {
                   variable[variable_len] = '\0';
               }
           } else if (value_len) {
               value[value_len] = *iter;
               value_len++;
               if (*(iter + 1) == '\0') {
                   value[value_len] = '\0';
               } else if (*(iter + 1) == ' ') {
                   value[value_len] = '\0';
               }
           } else {
               variable[variable_len] = *iter;
               variable_len++;
           }
        } else {
            if (variable_len && value_len) {
                params_set_value(params, variable, value);
                imported_values++;
            } else if (variable_len) {
                params_set_value(params, variable, "1");
                imported_values++;
            }
            variable_len = 0;
            value_len = 0;
        }
    }

    if (variable_len && value_len) {
        params_set_value(params, variable, value);
        imported_values++;
    } else if (variable_len) {
        params_set_value(params, variable, "1");
        imported_values++;
    }

    return imported_values;
}