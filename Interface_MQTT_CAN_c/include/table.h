#ifndef TABLE_H
#define TABLE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Paires enum "clé → valeur" (liste chaînée) */
struct enum_kv_s {
  char               *key;     /* alloué (ex: strdup), libéré dans table_free */
  int                 value;
  struct enum_kv_s   *next;
};
typedef struct enum_kv_s enum_kv_t;

/* Types de champ */
typedef enum {
  FT_INT   = 0,   /* 1 octet (0..255) */
  FT_BOOL  = 1,   /* 0/1 */
  FT_HEX   = 2,   /* "#RRGGBB" → 3 octets */
  FT_INT16 = 3,   /* 2 octets big-endian signé */
  FT_ENUM  = 4    /* 1 octet via dictionnaire */
} field_type_t;

/* Spéc d’un champ */
struct field_spec_s {
  char        *name;       /* alloué, libéré dans table_free */
  field_type_t type;
  enum_kv_t   *enum_list;  /* pour FT_ENUM sinon NULL (chaîne allouée/libérée) */
};
typedef struct field_spec_s field_spec_t;

/* Une entrée de table = un topic + un CAN ID + une liste de champs */
struct entry_s {
  char        *topic;        /* alloué, libéré dans table_free */
  uint32_t     can_id;
  size_t       field_count;
  field_spec_t *fields;      /* tableau alloué, libéré dans table_free */
};
typedef struct entry_s entry_t;

/* Table complète */
struct table_s {
  size_t   entry_count;
  entry_t *entries;          /* tableau alloué, libéré dans table_free */
};
typedef struct table_s table_t;

/* API */
bool         table_load(table_t *t, const char *json_path);
void         table_free(table_t *t);
const entry_t* table_find_by_topic(const table_t *t, const char *topic);
const entry_t* table_find_by_canid(const table_t *t, uint32_t can_id);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* TABLE_H */
