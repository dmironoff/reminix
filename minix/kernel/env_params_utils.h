//
// Created by dmironov on 19.03.2026.
//

#ifndef REMINIX_ENV_PARAMS_UTILS_H
#define REMINIX_ENV_PARAMS_UTILS_H

char *params_get_value(const char *params,	const char *name);
int params_has_var(const char *buffer, const char *name);
int params_unset_var(char *buffer, const char *name);
int params_set_value(char *buffer, const char *name, const char *value);

#endif //REMINIX_ENV_PARAMS_UTILS_H
