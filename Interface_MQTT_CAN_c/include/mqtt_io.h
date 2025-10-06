#ifndef MQTT_IO_H
#define MQTT_IO_H

#include "table.h"
#include <mosquitto.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct mqtt_ctx_s {
  struct mosquitto *cli;
  const table_t    *table;
  const char       *host;
  int               port;
  int               qos_pub;   // 1
} mqtt_ctx_t;

// Initialise, connecte, démarre le loop threadé
bool mqtt_init(mqtt_ctx_t *ctx, const table_t *t, const char *host, int port, int qos_pub);

// Subscribe à tous les topics de la table
bool mqtt_subscribe_all(mqtt_ctx_t *ctx);

// Publier un JSON (string) sur topic (QoS ctx->qos_pub)
bool mqtt_publish_json(mqtt_ctx_t *ctx, const char *topic, const char *json_str);

void mqtt_cleanup(mqtt_ctx_t *ctx);

// Callback appelée par CAN RX quand une trame arrive et doit être publiée
bool mqtt_on_can_message(mqtt_ctx_t *ctx, const entry_t *entry, const uint8_t data[8]);

// Callback interne MQTT → CAN (déclarée ici pour que can_io puisse la référencer si besoin)
void mqtt_set_user_data(mqtt_ctx_t *ctx, void *user);

#endif
