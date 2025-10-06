#ifndef MQTT_IO_H
#define MQTT_IO_H

#include <stdbool.h>
#include <stdint.h>

struct mosquitto;

/* Contexte MQTT */
typedef struct mqtt_ctx_s {
  struct mosquitto *mosq;
  int qos_sub;  /* 0..2, défaut 1 */
  int qos_pub;  /* 0..2, défaut 1 */
} mqtt_ctx_t;

/* Fwd decl pour éviter les includes circulaires */
typedef struct table_s table_t;
typedef struct entry_s entry_t;
typedef struct can_ctx_s can_ctx_t;

/* API */
bool mqtt_init(mqtt_ctx_t *ctx, const table_t *t, const char *host, int port, int keepalive);
bool mqtt_subscribe_all(mqtt_ctx_t *ctx);
bool mqtt_publish_json(mqtt_ctx_t *ctx, const char *topic, const char *json_str);

/* Trame CAN reçue -> publier sur MQTT (<base>/state). Retourne true si publish OK. */
bool mqtt_on_can_message(mqtt_ctx_t *ctx, const entry_t *entry, const uint8_t data[8]);

/* User data (ex: bundle {table, can, mqtt}) pour la callback on_message */
void mqtt_set_user_data(mqtt_ctx_t *ctx, void *user);

/* Régler les QoS (0/1/2). Valeurs invalides ignorées. */
void mqtt_set_qos(mqtt_ctx_t *ctx, int qos_sub, int qos_pub);

/* Nettoyage */
void mqtt_cleanup(mqtt_ctx_t *ctx);

#endif
