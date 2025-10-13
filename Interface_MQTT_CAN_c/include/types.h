#ifndef TYPES_H
#define TYPES_H


#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>


#ifdef __cplusplus
extern "C" {
#endif


/* Types de champ */
typedef enum {
  FT_INT   = 0,  /* 1 octet (0..255) */
  FT_BOOL  = 1,  /* 0/1 */
  FT_HEX   = 2,  /* "#RRGGBB" -> 3 octets */
  FT_INT16 = 3,  /* 2 octets big-endian */
  FT_ENUM  = 4   /* 1 octet via dictionnaire */
} field_type_t;


/* Paires enum "clé -> valeur" (liste chaînée) */
typedef struct enum_kv_s {
  char               *key;   /* alloué, libéré dans table_free */
  int                 value;
  struct enum_kv_s   *next;
} enum_kv_t;


/* Spéc d’un champ */
typedef struct field_spec_s {
  char        *name;       /* alloué, libéré dans table_free */
  field_type_t type;
  enum_kv_t   *enum_list;  /* pour FT_ENUM sinon NULL */
} field_spec_t;


/* Une entrée = topic + CAN ID + liste de champs */
typedef struct entry_s {
  char         *topic;        /* alloué, libéré dans table_free */
  uint32_t      can_id;
  size_t        field_count;
  field_spec_t *fields;       /* tableau alloué, libéré dans table_free */
} entry_t;


/* Table complète */
typedef struct table_s {
  size_t   entry_count;
  entry_t *entries;           /* tableau alloué, libéré dans table_free */
} table_t;


#ifdef __cplusplus
}
#endif
#endif /* TYPES_H */





