#ifndef PACK_H
#define PACK_H

#include <stdint.h>
#include <stdbool.h>
#include "types.h"

/* cJSON: on peut soit inclure l'en-tête, soit forward-declarer.
   Je choisis l'inclusion pour éviter les soucis de type. */
#include <cjson/cJSON.h>

/* Packe un objet JSON vers 8 octets CAN selon l'entry. */
bool   pack_payload(uint8_t out8[8], const entry_t *entry, cJSON *json_in);

/* Dépacke 8 octets CAN vers un objet JSON (à libérer avec cJSON_Delete). */
cJSON* unpack_payload(const uint8_t in8[8], const entry_t *entry);

#endif /* PACK_H */
