#ifndef PACK_H
#define PACK_H

#include "types.h"
#include <stdint.h>
#include <stdbool.h>
#include <cjson/cJSON.h> // alias vers third_party/cJSON/cJSON.h

// empaquette -> buffer[8], retourne true si OK
bool pack_payload(uint8_t out8[8], const entry_t *entry, cJSON *json_in);

// dépile -> objet JSON prêt à publier
// (retourne un cJSON* à libérer par l'appelant)
cJSON* unpack_payload(const uint8_t in8[8], const entry_t *entry);

// helpers
bool parse_hex_rgb(const char *s, uint8_t rgb[3]);
bool enum_str_to_code(const field_spec_t *fs, const char *s, uint8_t *code);
const char* enum_code_to_str(const field_spec_t *fs, uint8_t code);

#endif
