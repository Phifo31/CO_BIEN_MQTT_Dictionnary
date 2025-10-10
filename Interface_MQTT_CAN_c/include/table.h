#ifndef TABLE_H
#define TABLE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* Types de champs */
typedef enum {
  FT_INT,      /* 0..255 */
  FT_BOOL,     /* false/true -> 0/1 */
  FT_HEX,      /* "#RRGGBB" -> 3 octets */
  FT_INT16,    /* 0..65535  (big-endian sur le CAN) */
  FT_ENUM      /* string -> code (uint8) via dictionnaire */
} field_type_t;

/* Dictionnaire enum (clé -> code) */
typedef struct enum_kv_s {
  char *key;
  int   value;
  struct enum_kv_s *next;
} enum_kv_t;

/* Spéc d’un champ */
typedef struct field_spec_s {
  char         *name;       /* ex: "intensity" */
  field_type_t  type;
  enum_kv_t    *enum_list;  /* FT_ENUM: liste chaînée; sinon NULL */
} field_spec_t;

/* Entrée de conversion (un topic MQTT <-> un CAN ID) */
typedef struct entry_s {
  char     *topic;            /* "led/config" */
  uint32_t  can_id;           /* 0x51E … */
  field_spec_t *fields;       /* tableau */
  size_t    field_count;
} entry_t;

/* Table chargée (index par topic & par can_id) */
typedef struct table_s {
  entry_t *entries;
  size_t   count;

  /* petits index à la volée */
  /* (on reste simple: recherche linéaire; optim possible plus tard) */
} table_t;

/* API table */
bool   table_load(table_t *t, const char *json_path);
void   table_free(table_t *t);
const entry_t* table_find_by_topic(const table_t *t, const char *topic);
const entry_t* table_find_by_canid(const table_t *t, uint32_t can_id);

#endif
