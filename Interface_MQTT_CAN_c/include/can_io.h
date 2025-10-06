#ifndef CAN_IO_H
#define CAN_IO_H

#include "table.h"
#include "mqtt_io.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct can_ctx_s {
  int           fd;         // socket CAN_RAW
  const table_t *table;
  mqtt_ctx_t    *mqtt;      // pour publier côté CAN->MQTT
  char          ifname[16]; // "vcan0" ou "can0"
} can_ctx_t;

bool can_init(can_ctx_t *ctx, const table_t *t, mqtt_ctx_t *mqtt, const char *ifname);

// Envoi d'une trame CAN (8 octets)
bool can_send(can_ctx_t *ctx, uint32_t can_id, const uint8_t data[8]);

// Boucle RX bloquante (thread) : reçoit et publie via MQTT
void* can_rx_loop(void *arg);

void can_cleanup(can_ctx_t *ctx);

#endif
