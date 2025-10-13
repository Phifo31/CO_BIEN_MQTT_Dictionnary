#include "bridge_app.h"
#include "table.h"
#include "mqtt_io.h"
#include "can_io.h"
#include "log.h"

#include <mosquitto.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>   // nanosleep

// Défauts (peuvent être override par la ligne de commande via main)
static const char *CFG_PATH_DEF = "config/conversion.json";
static const char *IFNAME       = "can0";        // "vcan0" en test
static const char *MQTT_HOST    = "localhost";
static const int   MQTT_PORT    = 1883;

static volatile int g_running = 1;
static table_t    g_table;
static mqtt_ctx_t g_mqtt;
static can_ctx_t  g_can;

static void on_sig(int s){ (void)s; g_running = 0; }

static bool try_load(table_t *t, const char *p){
  if (p && table_load(t, p)) { LOGI("Table chargée depuis: %s", p); return true; }
  LOGW("Echec chargement table: %s", p ? p : "(null)");
  return false;
}

bool my_setup(const char *cfg_path_opt) {
  signal(SIGINT,  on_sig);
  signal(SIGTERM, on_sig);

  memset(&g_table, 0, sizeof(g_table));
  memset(&g_mqtt,  0, sizeof(g_mqtt));
  memset(&g_can,   0, sizeof(g_can));

  // 1) Charger la table
  const char *p = (cfg_path_opt && *cfg_path_opt) ? cfg_path_opt : CFG_PATH_DEF;
  if (!try_load(&g_table, p)) {
    // fallback courant quand on lance depuis build/
    if (!cfg_path_opt && strcmp(p, CFG_PATH_DEF)==0) {
      if (!try_load(&g_table, "../config/conversion.json")) return false;
    } else {
      return false;
    }
  }

  // 2) MQTT
  if (!mqtt_init(&g_mqtt, MQTT_HOST, MQTT_PORT, 60)) return false;
  mqtt_set_qos(&g_mqtt, 1, 1);
  if (!mqtt_subscribe_all(&g_mqtt)) return false;

  // 3) CAN
  if (!can_init(&g_can, IFNAME)) return false;

  LOGI("Setup OK (if=%s, mqtt=%s:%d)", IFNAME, MQTT_HOST, MQTT_PORT);
  return true;
}

bool my_loop(void) {
  if (!g_running) return false;

  if (g_mqtt.mosq) mosquitto_loop(g_mqtt.mosq, 0, 100);
  can_poll(&g_can, &g_table, &g_mqtt, 16);

  struct timespec ts = { .tv_sec = 0, .tv_nsec = 1 * 1000 * 1000 }; // ~1 ms
  nanosleep(&ts, NULL);

  return true;
}

void my_shutdown(void) {
  can_cleanup(&g_can);
  mqtt_cleanup(&g_mqtt);
  table_free(&g_table);
  LOGI("Shutdown OK");
}


