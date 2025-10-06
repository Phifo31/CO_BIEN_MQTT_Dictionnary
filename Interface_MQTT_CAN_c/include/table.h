#ifndef TABLE_H
#define TABLE_H

#include "types.h"
#include <cjson/cJSON.h>
#include <stdbool.h>

bool table_load(table_t *t, const char *path_json);
void table_free(table_t *t);

// lookups (O(1))
const entry_t* table_find_by_topic(const table_t *t, const char *topic);
const entry_t* table_find_by_id(const table_t *t, uint32_t can_id);

#endif
