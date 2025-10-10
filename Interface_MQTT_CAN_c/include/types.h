#ifndef TYPES_H
#define TYPES_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Forward declarations (pas de définitions ici) ───────────────────────── */

/* Table & conversion */
struct enum_kv_s;      typedef struct enum_kv_s      enum_kv_t;
struct field_spec_s;   typedef struct field_spec_s   field_spec_t;
struct entry_s;        typedef struct entry_s        entry_t;
struct table_s;        typedef struct table_s        table_t;

/* Contexte CAN / MQTT (définis ailleurs) */
struct can_ctx_s;      typedef struct can_ctx_s      can_ctx_t;
struct mqtt_ctx_s;     typedef struct mqtt_ctx_s     mqtt_ctx_t;

/* Remarque:
   - Les utilisateurs qui ont besoin de field_type_t, field_spec_t, entry_t,
     table_t, etc. doivent inclure "table.h" (qui contient les définitions).
   - "types.h" ne sert qu’à briser les inclusions circulaires. */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* TYPES_H */
