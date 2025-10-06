#ifndef TYPES_H
#define TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ---------- Types de champs ---------- */
typedef enum {
  FT_INT,      // 1 octet non signé
  FT_BOOL,     // 1 octet (0/1)
  FT_HEX,      // #RRGGBB -> 3 octets
  FT_INT16,    // 2 octets, big-endian
  FT_ENUM      // dict string <-> code (1 octet)
} field_type_t;

typedef struct enum_kv_s {
  char *key;                 // "ON"
  int   value;               // 1
  struct enum_kv_s *next;
} enum_kv_t;

typedef struct field_spec_s {
  char        *name;         // "mode"
  field_type_t type;
  enum_kv_t   *enum_list;    // si FT_ENUM
} field_spec_t;

typedef struct entry_s {
  char     *topic;           // "led/config"
  uint32_t  can_id;          // 0x1310
  field_spec_t *fields;
  size_t        field_count;
  struct entry_s *next;
} entry_t;

/* ---------- Index UTHASH (forward decl) ---------- */
struct topic_map_s;
struct id_map_s;

/* ---------- Table (indices typés, pas void*) ---------- */
typedef struct table_s {
  struct topic_map_s *topic_index;   // index par topic
  struct id_map_s    *id_index;      // index par ID
} table_t;

#endif
