#include "bridge_app.h"
#include "table.h"
#include "mqtt_io.h"
#include "can_io.h"
#include "log.h"
#include <stdlib.h>
#include <mosquitto.h>
#include <unistd.h>   
#include <stddef.h>

#include <signal.h>
#include <string.h>


// --- paramètres "fixes" (fais simple)
static const char *CFG_PATH  = (argc > 1) ? argv[1] : "config/conversion.json" ;
#include "bridge_app.h"
#include <stddef.h>

int main(int argc, char **argv){
  const char *cfg_path = (argc > 1) ? argv[1] : "config/conversion.json";
  if (!my_setup(cfg_path)) return 1;
  while (my_loop()) {}
  my_shutdown();
  return 0;
}


static const char *IFNAME    = "can0";        // ou "vcan0" en test logiciel
static const char *MQTT_HOST = "localhost";
static const int   MQTT_PORT = 1883;

static volatile int g_running = 1;
static table_t   g_table;
static mqtt_ctx_t g_mqtt;
static can_ctx_t  g_can;

static void on_sig(int s){ (void)s; g_running = 0; }

bool my_setup(void) {
  signal(SIGINT,  on_sig);
  signal(SIGTERM, on_sig);

  memset(&g_table, 0, sizeof(g_table));
  memset(&g_mqtt,  0, sizeof(g_mqtt));
  memset(&g_can,   0, sizeof(g_can));

  if (!table_load(&g_table, CFG_PATH)) {
    LOGE("Echec chargement table: %s", CFG_PATH);
    return false;
  }

  if (!mqtt_init(&g_mqtt, MQTT_HOST, MQTT_PORT, 60))
    return false;
  mqtt_set_qos(&g_mqtt, 1, 1);

  // Lier la callback MQTT -> CAN avec userdata (table+can+mqtt)
  struct { const table_t *t; can_ctx_t *c; mqtt_ctx_t *m; } *ub =
    (void*)malloc(sizeof(*ub));
  if (!ub) return false;
  ub->t = &g_table; ub->c = &g_can; ub->m = &g_mqtt;
  mqtt_set_user_data(&g_mqtt, ub);

 if (!mqtt_subscribe_all(&g_mqtt)) return false;
 if (!can_init(&g_can, IFNAME)) return false;

  LOGI("Setup OK (cfg=%s, if=%s, mqtt=%s:%d)", CFG_PATH, IFNAME, MQTT_HOST, MQTT_PORT);
  return true;
}

bool my_loop(void)
{
  if (!g_running) return false;

  /* 1) Traite MQTT disponible (jusqu'à 100 paquets par tick, non-bloquant) */
  if (g_mqtt.mosq)
  mosquitto_loop(g_mqtt.mosq, 0, 100);

  /* 2) Draine le CAN disponible (non-bloquant) */
  can_poll(&g_can, &g_table, &g_mqtt, 8);

  /* 3) Petite sieste pour ne pas monopoliser le CPU si tout est vide */
  sleep(1000); // 1 ms

  return true;
}


void my_shutdown(void) {
  // libérer le userdata qu’on a alloué
  void *ud = g_mqtt.mosq ? mosquitto_userdata(g_mqtt.mosq) : NULL;
  
  if (ud) free(ud);

  can_cleanup(&g_can);
  mqtt_cleanup(&g_mqtt);
  table_free(&g_table);
  LOGI("Shutdown OK");
}
