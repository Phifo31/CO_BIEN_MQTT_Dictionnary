// src/bridge_app.c
#include "bridge_app.h"
#include "log.h"
#include "table.h"
#include "mqtt_io.h"
#include "can_io.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>     // usleep, readlink
#include <limits.h>     // PATH_MAX
#include <mosquitto.h>  // mosquitto_loop

/* ========= Paramètres simples ========= */
static const char *MQTT_HOST = "localhost";
static const int   MQTT_PORT = 1883;
/* "can0" pour bus réel, "vcan0" si bus virtuel */
static const char *IFNAME    = "can0";

/* ========= États globaux ========= */
static volatile int g_run = 1;
static table_t    g_table = (table_t){0};
static mqtt_ctx_t g_mqtt  = (mqtt_ctx_t){0};
static can_ctx_t  g_can   = (can_ctx_t){0};

typedef struct user_bundle_s {
  const table_t *table;
  can_ctx_t     *can;
  mqtt_ctx_t    *mqtt;
} user_bundle_t;
static user_bundle_t g_ub = {0};

/* ========= Utils ========= */
static void on_sig(int sig){ (void)sig; g_run = 0; }

static const char* find_cfg(char out[], size_t n){
  char exe[PATH_MAX];
  ssize_t m = readlink("/proc/self/exe", exe, sizeof(exe)-1);
  if (m > 0) {
    exe[m] = '\0';
    char *slash = strrchr(exe, '/');
    if (slash) *slash = '\0'; // dirname
    snprintf(out, n, "%s/../config/conversion.json", exe);
    FILE *f = fopen(out, "rb");
    if (f) { fclose(f); return out; }
  }
  snprintf(out, n, "config/conversion.json");
  FILE *f = fopen(out, "rb");
  if (f) { fclose(f); return out; }
  return NULL;
}

static bool file_exists(const char *p){
  if (!p) return false;
  FILE *f = fopen(p, "rb");
  if (!f) return false;
  fclose(f);
  return true;
}

/* ========= API appelée par main.c ========= */

bool my_setup(const char *cfg_path_opt){
  signal(SIGINT,  on_sig);
  signal(SIGTERM, on_sig);

  /* 1) Trouver/charger la table */
  char cfg_path[PATH_MAX];
  const char *cfg = NULL;

  if (cfg_path_opt && file_exists(cfg_path_opt)) {
    cfg = cfg_path_opt;
  } else {
    cfg = find_cfg(cfg_path, sizeof(cfg_path));
  }

  if (!cfg) { LOGE("conversion.json introuvable"); return false; }
  if (!table_load(&g_table, cfg)) {
    LOGE("Echec chargement table: %s", cfg);
    return false;
  }

  /* 2) MQTT (boucle pilotée manuellement dans my_loop) */
  if (!mqtt_init(&g_mqtt, MQTT_HOST, MQTT_PORT, /*keepalive*/60)) {
    LOGE("mqtt_init échoué");
    return false;
  }
  /* Dé-commente si mqtt_set_qos est implémenté dans mqtt_io.c
     mqtt_set_qos(&g_mqtt, 1, 1);
  */

  /* 3) Lier le bundle pour la callback MQTT->CAN */
  g_ub.table = &g_table;
  g_ub.can   = &g_can;
  g_ub.mqtt  = &g_mqtt;
  mqtt_set_user_data(&g_mqtt, &g_ub);

  /* S'abonner large (#) : on_message filtrera côté mqtt_io.c */
  if (!mqtt_subscribe_all(&g_mqtt)) {
    LOGE("Subscribe '#' échoué");
    return false;
  }

  /* 4) CAN non bloquant */
  if (!can_init(&g_can, IFNAME)) {
    LOGE("can_init(%s) échoué", IFNAME);
    return false;
  }

  LOGI("Bridge prêt. MQTT=%s:%d  IF=%s  CFG=%s", MQTT_HOST, MQTT_PORT, IFNAME, cfg);
  return true;
}

bool my_loop(void){
  if (!g_run) return false;

  /* Pompe MQTT (non bloquant) */
  if (g_mqtt.mosq) {
    mosquitto_loop(g_mqtt.mosq, /*timeout_ms*/0, /*max_packets*/100);
  }

  /* Pompe CAN (non bloquant), draine quelques trames max par tour */
  can_poll(&g_can, &g_table, &g_mqtt, /*max_frames*/32);

  /* Micro-pause pour éviter 100% CPU si rien n'arrive */
  usleep(1000); // 1 ms
  return g_run != 0;
}

void my_shutdown(void){
  can_cleanup(&g_can);
  mqtt_cleanup(&g_mqtt);
  table_free(&g_table);
}



