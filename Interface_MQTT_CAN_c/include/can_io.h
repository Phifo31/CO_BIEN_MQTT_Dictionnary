#ifndef CAN_IO_H
#define CAN_IO_H


/* Contexte SocketCAN simple */
typedef struct can_ctx_s {
  int fd;
} can_ctx_t;

/* Init interface (ex: "can0" ou "vcan0"). Non-bloquant. */
bool can_init(can_ctx_t *c, const char *ifname);

/* Send 8 octets sur un CAN ID standard */
bool can_send(can_ctx_t *c, uint32_t can_id, const uint8_t data[8]);

/* Pompe non-bloquante: lit au plus N trames et publie vers MQTT (via table) */
void can_poll(can_ctx_t *c, const table_t *t, mqtt_ctx_t *m, int max_frames);

int can_poll_burst(can_ctx_t *ctx, int max_frames, int timeout_ms);


void can_cleanup(can_ctx_t *c);

#endif

// End of file
