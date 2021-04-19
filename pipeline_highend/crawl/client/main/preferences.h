#ifndef FOO_DOT_H    /* This is an "include guard" */
#define FOO_DOT_H

#include "esp_system.h"
#include <stdio.h>
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"

bool pref_begin(const char *name, bool readOnly, const char *partition_label = NULL);
size_t pref_putUInt(const char *key, uint32_t value);
uint32_t pref_getUInt(const char *key, const uint32_t defaultValue);

#endif /* FOO_DOT_H */