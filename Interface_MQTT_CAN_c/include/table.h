#ifndef TABLE_H
#define TABLE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Déclarations liées aux enums/dictionnaires ──────────────────────────── */

struct enum_kv_s {
  const char        *key;    /* ex: "ON" */
  int                value;  /* ex: 1    */
  struct enum_kv_s  *next;   /* liste chaînée, NULL si fin */
};
/* Via types.h: typedef struct enum_kv_s enum_kv_t; */

/* ── Typage des champs des payloads ──────────────────────────────────────── */
typedef enum {
  FT_INT   = 0,   /* 1 octet non signé (0..255) */
  FT_BOOL  = 1,   /* 1 octet: 0/1                */
  FT_HEX   = 2,   /* string "#RRGGBB" → 3 octets */
  FT_INT16 = 3,   /* 2 octets big-endian signés  */
  FT_ENUM  = 4    /* 1 octet via dictionnaire    */
} field_type_t;

/* ── Spécification d’un champ ────────────────────────────────────────────── */
struct field_spec_s {
  const char   *name;        /* clé JSON (ex: "intensity")        */
  field_type_t  type;        /* FT_INT, FT_BOOL, ...               */
  enum_kv_t    *enum_list;   /* pour FT_ENUM, sinon NULL           */
};
/* Via types.h: typedef struct field_spec_s field_spec_t; */

/* ── Une entrée de la table (un topic ⇄ un CAN ID + champs) ─────────────── */
struct entry_s {
  const char    *topic;       /* ex: "led/config"                          */
  uint32_t       can_id;      /* ex: 0x1310                                */
  size_t         field_count; /* nb d'éléments dans 'fields'               */
  field_spec_t  *fields;      /* tableau [field_count]                     */
};
/* Via types.h: typedef struct entry_s entry_t; */

/* ── Table complète ──────────────────────────────────────────────────────── */
struct table_s {
  size_t    entry_count;   /* nb d'entries dans 'entries'                 */
  entry_t  *entries;       /* tableau [entry_count]                       */
};
/* Via types.h: typedef struct table_s table_t; */

/* ── API table ───────────────────────────────────────────────────────────── */

/* Charge la table depuis un JSON (conversion.json).
   Retourne true si OK. En cas d’échec, la table est laissée dans un état sûr. */
bool        table_load(table_t *t, const char *json_path);

/* Libère toute la mémoire allouée par table_load. */
void        table_free(table_t *t);

/* Recherche par topic exact (strcmp). Retourne NULL si absent. */
const entry_t* table_find_by_topic(const table_t *t, const char *topic);

/* Recherche par identifiant CAN. Retourne NULL si absent. */
const entry_t* table_find_by_canid(const table_t *t, uint32_t can_id);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* TABLE_H */
