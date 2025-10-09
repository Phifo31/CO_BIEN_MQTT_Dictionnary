#include "log.h"
#include "table.h"
#include "mqtt_io.h"
#include "can_io.h"

#include <signal.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* arrêt propre au Ctrl+C */
static volatile int g_running = 1;
static void on_sigint(int sig){ (void)sig; g_running = 0; }

/* Bundle passé aux callbacks MQTT (lu par on_message dans mqtt_io.c) */
typedef struct user_bundle_s {
  const table_t *table;
  can_ctx_t     *can;
  mqtt_ctx_t    *mqtt;  /* optionnel */
} user_bundle_t;

int main(int argc, char **argv) {
  const char *cfg_path = (argc>1) ? argv[1] : "config/conversion.json";
  const char *mqtt_host= "localhost";
  int         mqtt_port= 1883;
  const char *ifname   = "can0";   /* "can0" réel, "vcan0" pour tests virtuels */

  signal(SIGINT,  on_sigint);
  signal(SIGTERM, on_sigint);

  /* 1) Charger la table (conversion.json) */
  table_t table = (table_t){0};
  if (!table_load(&table, cfg_path)) {
    LOGE("Echec chargement table");
    return 1;
  }

  /* 2) MQTT (abonnement v5 avec option no-local géré dans mqtt_io.c) */
  mqtt_ctx_t mqtt = (mqtt_ctx_t){0};
  if (!mqtt_init(&mqtt, &table, mqtt_host, mqtt_port, /*keepalive*/60)) {
    table_free(&table);
    return 1;
  }
  mqtt_set_qos(&mqtt, 1, 1);  /* QoS par défaut 1/1 */

  /* 3) CAN (SocketCAN) */
  can_ctx_t can = (can_ctx_t){0};
  if (!can_init(&can, &table, &mqtt, ifname)) {
    mqtt_cleanup(&mqtt);
    table_free(&table);
    return 1;
  }

  /* 4) Lier les modules pour la callback MQTT -> CAN */
  user_bundle_t ub = { .table = &table, .can = &can, .mqtt = &mqtt };
  mqtt_set_user_data(&mqtt, &ub);

  /* 5) S'abonner (MQTT v5 no-local, topics de la table ou '#' en fallback) */
  if (!mqtt_subscribe_all(&mqtt)) {
    LOGE("Subscribe échoué");
    can_cleanup(&can);
    mqtt_cleanup(&mqtt);
    table_free(&table);
    return 1;
  }

  /* 6) Thread de réception CAN (CAN -> MQTT sur le topic exact de la table) */
  pthread_t th_rx;
  if (pthread_create(&th_rx, NULL, can_rx_loop, &can) != 0) {
    LOGE("pthread_create can_rx_loop");
    can_cleanup(&can);
    mqtt_cleanup(&mqtt);
    table_free(&table);
    return 1;
  }

  LOGI("Démarré. Ctrl+C pour quitter.");
  while (g_running) { sleep(1); }

  LOGI("Arrêt…");
  /* Teardown */
  pthread_cancel(th_rx);            /* simple; sinon, prévoir un flag d'arrêt côté can_rx_loop */
  pthread_join(th_rx, NULL);
  can_cleanup(&can);
  mqtt_cleanup(&mqtt);
  table_free(&table);
  return 0;
}
