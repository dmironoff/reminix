//
// Created by dmironov on 19.03.2026.
//

/*
 * Небольшая библиотека для работы с хранилещем параметров в ядре
 * Конечно, большая часть этого, возможно, нам не потребуется, но пусть будет
 * Всё это написано без libc так как она будет использоваться в unpaged секции
 * А туда мне не хочется тащить лишние зависимости - только всё по сути
 * формат параметров: имя1=значение\0имя2=значение\0\0   - значения разделены \0, а кончаются двумя \0\0
 */

#include "env_params_utils.h"

/*
 * Получение значения из буфера параметров
 */
char *params_get_value(
        const char *params,			/* boot monitor parameters */
        const char *name			/* key to look up */
)
{
/* Get environment value - kernel version of getenv to avoid setting up the
 * usual environment array.
 */
    register const char *namep;
    register char *envp;

    for (envp = (char *) params; *envp != 0;) {
        for (namep = name; *namep != 0 && *namep == *envp; namep++, envp++)
            ;
        if (*namep == '\0' && *envp == '=') return(envp + 1);
        while (*envp++ != 0)
            ;
    }
    return(0);
}

/*
 * Проверка наличия параметра
 *
 */
int params_has_var(const char *buffer, const char *name) {
    char *iter, *namep;

    for (iter = (char *)buffer, namep = (char *)name; *iter != '\0' && *(iter + 1) != '\0'; iter++) {
        if (*iter == *namep) {
            namep++;
            if (*namep == '\0' && *(iter + 1) == '=') {
                return 1;
            } else {
                namep = (char *)name;
            }
            continue;
        }
        namep = (char *)name;
    }

    return 0;
}

/*
 * Удаление параметра
 *
 */
int params_unset_var(char *buffer, const char *name) {
   char *iter, *namep;
   int start_offset = 0;
   int end_offset = 0;
   int found = 0;

   for (iter = (char *)buffer, namep = (char *)name; *iter != '\0' && *(iter + 1) != '\0'; iter++) {
       if (!found) {
           if (*iter == *namep) {
               namep++;
               if (*namep == '\0' && *(iter + 1) == '=') {
                   found = 1; // Мы нашли переменную, теперь нам нужно найти конец значения
                   end_offset = start_offset;
               } else {
                   namep = (char *) name;
               }
               continue;
           }
           namep = (char *) name;
           start_offset++;
       } else {
           if (*iter == '\0')  {
               break;
           }
           end_offset++;
       }
   }

   if (!found) {
       return 0;
   }

   for (iter = buffer + start_offset;
            *(iter + end_offset) != '\0' && *(iter + end_offset + 1) != '\0';
            iter++, start_offset++, end_offset++) {
       *(buffer + start_offset) = *(buffer + end_offset);
   }
    *(buffer + start_offset + 1) = '\0';
    *(buffer + start_offset + 2) = '\0';

    return 1;
}

/*
 * Внесение или изменение значения в буфере параметров
 */
int params_set_value(char *buffer, const char *name, const char *value) {
    char *iter;
    int params_len;

    params_unset_var(buffer, name);

    for (iter = (char *)buffer; *iter !='\0' && *(iter + 1) != '\0'; iter++, params_len++);

    for (iter = (char *)name; *iter != '\0'; iter++) {
        *(buffer + params_len) = *iter;
        params_len++;
    }

    *(buffer + params_len) = '=';
    params_len++;

    for (iter = (char *)value; *iter != '\0'; iter++) {
        *(buffer + params_len) = *iter;
        params_len++;
    }

    return 1;
}
