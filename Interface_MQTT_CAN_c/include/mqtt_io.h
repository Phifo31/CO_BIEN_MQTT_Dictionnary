#ifndef MQTT_IO_H
#define MQTT_IO_H

#include <stdbool.h>
#include <stdint.h>

struct mosquitto;

/* Contexte MQTT : stocke le pointeur Mosquitto */
typedef struct mqtt_ctx_s {
  struct mosquitto *mosq;
} mqtt_ctx_t;

/* Fwd decl pour éviter les includes circulaires */
typedef struct table_s table_t;
typedef struct entry_s entry_t;
typedef struct can_ctx_s can_ctx_t;

/* API */
bool mqtt_init(mqtt_ctx_t *ctx, const table_t *t, const char *host, int port, int keepalive);
bool mqtt_subscribe_all(mqtt_ctx_t *ctx);
bool mqtt_publish_json(mqtt_ctx_t *ctx, const char *topic, const char *json_str);

/* Appelé quand une trame CAN arrive → publie sur MQTT.
   Retourne true si la publication a réussi. */
bool mqtt_on_can_message(mqtt_ctx_t *ctx, const entry_t *entry, const uint8_t data[8]);

/* Stocke un pointeur user (ex: bundle {table, can, mqtt}) dans le client */
void mqtt_set_user_data(mqtt_ctx_t *ctx, void *user);

/* Nettoyage */
void mqtt_cleanup(mqtt_ctx_t *ctx);

#endif
