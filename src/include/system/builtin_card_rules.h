#ifndef BUILTIN_CARD_RULES_H
#define BUILTIN_CARD_RULES_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int builtin_card_priority_for_operator(const char *operator_id);
int builtin_card_profile_supported(const char *oemname, const char *devtype,
                                   const char *devsubtype);
int builtin_card_parse_real_name_status(const char *json, int *status);
int builtin_card_build_priority_query(int priority, char *out, size_t out_size);

#ifdef __cplusplus
}
#endif

#endif
