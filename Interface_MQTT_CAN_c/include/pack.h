#ifndef PACK_H
#define PACK_H

#include <stdint.h>
#include <stdbool.h>

/* IMPORTANT: on a besoin des définitions complètes de entry_t/field_spec_t */
#include "table.h"

/* cJSON */
#include <cjson/cJSON.h>

/* Packe un objet JSON vers 8 octets CAN selon l'entry. */
bool   pack_payload(uint8_t out8[8], const entry_t *entry, cJSON *json_in);

/* Dépacke 8 octets CAN vers un objet JSON (à libérer avec cJSON_Delete). */
cJSON* unpack_payload(const uint8_t in8[8], const entry_t *entry);

#endif /* PACK_H */
