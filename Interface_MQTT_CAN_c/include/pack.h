#ifndef PACK_H
#define PACK_H
#include <stdbool.h>
#include <stdint.h>
#include "table.h"

/* Pack JSON -> 8 octets (selon entry->fields). Retourne false si valeur invalide. */
bool   pack_payload(uint8_t out8[8], const entry_t *entry, cJSON *json_in);
/* Unpack 8 octets -> JSON (clé/val). Retourne NULL si erreur. */
cJSON* unpack_payload(const uint8_t in8[8], const entry_t *entry);

#endif
