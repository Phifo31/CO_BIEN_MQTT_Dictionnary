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

/* ========= Paramètres simples (change-les au besoin) ========= */
static const char *MQTT_HOST = "localhost";
static const int   MQTT_PORT = 1883;
/* Utilise "can0" pour le bus réel, "vcan0" si tu testes sur bus virtuel */
static const char *IFNAME    = "can0";

/* ========= États globaux minimaux ========= */
static volatile int g_run = 1;
static table_t   g_table = {0};
static mqtt_ctx_t g_mqtt = {0};
static can_ctx_t  g_can  = {0};

/* user_data passé à mosquitto (doit matcher la structure attendue dans mqtt_io.c) */
typedef struct user_bundle_s {
  const table_t *table;
  can_ctx_t     *can;
  mqtt_ctx_t    *mqtt;
} user_bundle_t;
static user_bundle_t g_ub = {0};

/* ========= Utils ========= */

static void on_sig(int sig){ (void)sig; g_run = 0; }

/* Cherche le fichier config/conversion.json de façon robuste :
   1) <exe_dir>/../config/conversion.json
   2) ./config/conversion.json (CWD)
   Retourne out si trouvé, sinon NULL. */
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
  // fallback: depuis le dossier courant
  snprintf(out, n, "config/conversion.json");
  FILE *f = fopen(out, "rb");
  if (f) { fclose(f); return out; }
  return NULL;
}

/* ========= API appelée par main.c ========= */

bool my_setup(void){
  signal(SIGINT,  on_sig);
  signal(SIGTERM, on_sig);

  /* 1) Charger la table */
  char cfg_path[PATH_MAX];
  const char *cfg = find_cfg(cfg_path, sizeof(cfg_path));
  if (!cfg) { LOGE("conversion.json introuvable"); return false; }
  if (!table_load(&g_table, cfg)) {
    LOGE("Echec chargement table: %s", cfg);
    return false;
  }

  /* 2) Démarrer MQTT (boucle manuelle → pas de loop_start) */
  if (!mqtt_init(&g_mqtt, MQTT_HOST, MQTT_PORT, /*keepalive*/60)) {
    LOGE("mqtt_init échoué");
    return false;
  }
  /* Si tu as implémenté mqtt_set_qos() dans mqtt_io.c, tu peux le dé-commenter
     mqtt_set_qos(&g_mqtt, 1, 1);
  */

  /* 3) Lier le bundle pour la callback MQTT -> CAN */
  g_ub.table = &g_table;
  g_ub.can   = &g_can;
  g_ub.mqtt  = &g_mqtt;
  mqtt_set_user_data(&g_mqtt, &g_ub);

  /* S'abonner large : on_message (dans mqtt_io.c) filtrera ce qu'il faut */
  if (!mqtt_subscribe_all(&g_mqtt)) {
    LOGE("Subscribe '#' échoué");
    return false;
  }

  /* 4) Ouvrir CAN (socket non bloquant) */
  if (!can_init(&g_can, IFNAME)) {
    LOGE("can_init(%s) échoué", IFNAME);
    return false;
  }

  LOGI("Bridge prêt. MQTT=%s:%d  IF=%s", MQTT_HOST, MQTT_PORT, IFNAME);
  return true;
}

bool my_loop(void){
  if (!g_run) return false;

  /* Pompe MQTT (non-bloquant) */
  if (g_mqtt.mosq) {
    /* timeout_ms=0, max_packets=100 => fait avancer la state-machine MQTT
       sans bloquer la boucle. */
    mosquitto_loop(g_mqtt.mosq, /*timeout_ms*/0, /*max_packets*/100);
  }

  /* Pompe CAN (non-bloquant), draine quelques trames max par tour */
  can_poll(&g_can, &g_table, &g_mqtt, /*max_frames*/32);

  /* Micro-pause pour ne pas brûler le CPU si rien n'arrive */
  usleep(1000); // 1 ms

  return g_run != 0;
}

void my_shutdown(void){
  /* Ordre sympa: CAN puis MQTT, puis table */
  can_cleanup(&g_can);
  mqtt_cleanup(&g_mqtt);
  table_free(&g_table);
}



