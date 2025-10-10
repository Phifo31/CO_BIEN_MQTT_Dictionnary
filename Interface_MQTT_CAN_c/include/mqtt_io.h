#ifndef MQTT_IO_H
#define MQTT_IO_H

#include <stdbool.h>
#include <stdint.h>

struct mosquitto;
struct table_s;
struct entry_s;
struct can_ctx_s;

typedef struct mqtt_ctx_s {
  struct mosquitto *mosq;
  int qos_sub;  /* 0..2 (def 1) */
  int qos_pub;  /* 0..2 (def 1) */
} mqtt_ctx_t;

/* Init MQTT (v5 + no_local), callbacks installées mais pas de thread lancé */
bool mqtt_init(mqtt_ctx_t *ctx, const char *host, int port, int keepalive);

/* Subscription large (‘#’) avec option v5 no_local */
bool mqtt_subscribe_all(mqtt_ctx_t *ctx);

/* Pompe non-bloquante (à appeler dans my_loop) */
bool mqtt_poll(mqtt_ctx_t *ctx);

/* Publier un JSON sur un topic */
bool mqtt_publish_json(mqtt_ctx_t *ctx, const char *topic, const char *json_str);

/* CAN -> MQTT (publie sur le topic de base, sans /state) */
bool mqtt_handle_can_message(mqtt_ctx_t *ctx, const struct entry_s *e, const uint8_t data[8]);

/* User-data: passer {table,can,mqtt} au callback on_message */
void mqtt_set_user_data(mqtt_ctx_t *ctx, void *userdata);

/* Nettoyage */
void mqtt_cleanup(mqtt_ctx_t *ctx);

#endif
