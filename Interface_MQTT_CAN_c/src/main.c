#include "log.h"
#include "table.h"
#include "mqtt_io.h"
#include "can_io.h"

#include <signal.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static volatile int g_running = 1;
static void on_sigint(int sig){ (void)sig; g_running = 0; }

int main(int argc, char **argv) {
  const char *cfg_path = (argc>1) ? argv[1] : "config/conversion.json";
  const char *mqtt_host= "localhost";
  int         mqtt_port= 1883;
  const char *ifname   = "can0";  // change en "can0" si bus réel

  signal(SIGINT,  on_sigint);
  signal(SIGTERM, on_sigint);

  table_t table = {0};
  if (!table_load(&table, cfg_path)) {
    LOGE("Echec chargement table");
    return 1;
  }

  mqtt_ctx_t mqtt = {0};
  if (!mqtt_init(&mqtt, &table, mqtt_host, mqtt_port, /*QoS*/1)) return 1;

  // associer userdata: table+can pour la callback on_message
  can_ctx_t can = {0};
  if (!can_init(&can, &table, &mqtt, ifname)) return 1;

  typedef struct { const table_t *table; can_ctx_t *can; } user_bundle_t;
  user_bundle_t ub = { .table = &table, .can = &can };
  mqtt_set_user_data(&mqtt, &ub);

  // s'abonner
  mqtt_subscribe_all(&mqtt); // ici wildcard '#', sinon itérer les topics exacts

  // thread RX CAN
  pthread_t th_rx;
  pthread_create(&th_rx, NULL, can_rx_loop, &can);

  LOGI("Démarré. Ctrl+C pour quitter.");
  while (g_running) { sleep(1); }

  LOGI("Arrêt…");
  // teardown
  pthread_cancel(th_rx); // ou meilleur: utiliser un flag + read timeouts
  pthread_join(th_rx, NULL);
  can_cleanup(&can);
  mqtt_cleanup(&mqtt);
  table_free(&table);
  return 0;
}
