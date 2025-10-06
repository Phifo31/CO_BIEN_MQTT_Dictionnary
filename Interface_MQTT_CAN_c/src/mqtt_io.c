#include "mqtt_io.h"
#include "table.h"
#include "pack.h"
#include "can_io.h"
#include "log.h"
#include <mosquitto.h>
#include <cjson/cJSON.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* ========= Helpers simples ========= */

static int ends_with(const char *s, const char *suffix) {
  size_t ls = strlen(s), lt = strlen(suffix);
  return (ls >= lt) && (strcmp(s + (ls - lt), suffix) == 0);
}

static void topic_base_from_cmd(const char *cmd_topic, char *base, size_t base_sz) {
  /* Enlève le suffixe "/cmd" si présent */
  const char *suffix = "/cmd";
  size_t L = strlen(cmd_topic);
  size_t S = strlen(suffix);
  if (L >= S && strcmp(cmd_topic + (L - S), suffix) == 0) {
    size_t nb = L - S;
    if (nb >= base_sz) nb = base_sz - 1;
    memcpy(base, cmd_topic, nb);
    base[nb] = '\0';
  } else {
    /* Pas de suffixe: on considère que c'est déjà un "topic de base" */
    snprintf(base, base_sz, "%s", cmd_topic);
  }
}

static void topic_state_from_base(const char *base, char *state, size_t state_sz) {
  /* Concatène "/state" au topic de base */
  const char *suf = "/state";
  size_t need = strlen(base) + strlen(suf) + 1;
  if (need > state_sz) {
    /* tronque proprement */
    snprintf(state, state_sz, "%s", base);
    return;
  }
  snprintf(state, state_sz, "%s%s", base, suf);
}

/* ========= Contexte utilisateur passé aux callbacks =========
   On réutilise le bundle déjà en place (table + pointeur CAN). */
typedef struct user_bundle_s {
  const table_t *table;
  can_ctx_t     *can;
  mqtt_ctx_t    *mqtt;
} user_bundle_t;

/* ========= Callbacks Mosquitto ========= */

static void on_connect(struct mosquitto *mosq, void *userdata, int rc) {
  (void)mosq; (void)userdata;
  if (rc == 0) LOGI("MQTT connecté");
  else         LOGW("MQTT connect rc=%d", rc);
}

static void on_disconnect(struct mosquitto *mosq, void *userdata, int rc) {
  (void)mosq; (void)userdata;
  LOGW("MQTT déconnecté rc=%d", rc);
}

/* === Réception MQTT → CAN : on ne traite QUE les topics se terminant par "/cmd" === */
static void on_message(struct mosquitto *mosq, void *userdata, const struct mosquitto_message *msg) {
  (void)mosq;
  user_bundle_t *ub = (user_bundle_t*)userdata;
  if (!ub || !ub->table || !ub->can || !msg || !msg->topic) return;

  /* Filtre : uniquement ".../cmd" */
  if (!ends_with(msg->topic, "/cmd")) {
    /* on ignore tout le reste, y compris ".../state" → évite toute boucle */
    return;
  }

  /* Retrouver le topic "de base" (sans /cmd) pour consulter la table */
  char base_topic[256];
  topic_base_from_cmd(msg->topic, base_topic, sizeof(base_topic));

  const entry_t *e = table_find_by_topic(ub->table, base_topic);
  if (!e) {
    LOGW("Topic inconnu (cmd): %s (base=%s)", msg->topic, base_topic);
    return;
  }

  /* Parser le JSON d'entrée */
  cJSON *in = NULL;
  if (msg->payloadlen > 0 && msg->payload) {
    char *buf = (char*)malloc((size_t)msg->payloadlen + 1);
    if (!buf) { LOGE("malloc payload"); return; }
    memcpy(buf, msg->payload, (size_t)msg->payloadlen);
    buf[msg->payloadlen] = 0;
    in = cJSON_Parse(buf);
    free(buf);
  }
  if (!in) { LOGW("Payload JSON invalide sur %s", msg->topic); return; }

  /* Pack → 8 octets */
  uint8_t out8[8] = {0};
  if (!pack_payload(out8, e, in)) {
    LOGE("Pack échoué pour topic %s (base=%s)", msg->topic, base_topic);
    cJSON_Delete(in);
    return;
  }
  cJSON_Delete(in);

  /* Envoi CAN */
  if (!can_send(ub->can, e->can_id, out8)) {
    LOGE("Envoi CAN échoué (id=0x%X, topic=%s)", e->can_id, base_topic);
    return;
  }
  LOGI("MQTT->CAN OK: topic=%s id=0x%X", base_topic, e->can_id);
}

/* ========= API exposée ========= */

bool mqtt_init(mqtt_ctx_t *ctx, const table_t *t, const char *host, int port, int keepalive) {
  if (!ctx) return false;
  memset(ctx, 0, sizeof(*ctx));

  mosquitto_lib_init();
  ctx->mosq = mosquitto_new(NULL, true, NULL);
  if (!ctx->mosq) {
    LOGE("mosquitto_new");
    return false;
  }
  mosquitto_connect_callback_set(ctx->mosq, on_connect);
  mosquitto_disconnect_callback_set(ctx->mosq, on_disconnect);
  mosquitto_message_callback_set(ctx->mosq, on_message);

  /* Connexion */
  if (mosquitto_connect(ctx->mosq, host ? host : "localhost", port > 0 ? port : 1883, keepalive > 0 ? keepalive : 60) != MOSQ_ERR_SUCCESS) {
    LOGE("mosquitto_connect");
    mosquitto_destroy(ctx->mosq);
    ctx->mosq = NULL;
    return false;
  }

  /* Thread interne libmosquitto */
  if (mosquitto_loop_start(ctx->mosq) != MOSQ_ERR_SUCCESS) {
    LOGE("mosquitto_loop_start");
    mosquitto_destroy(ctx->mosq);
    ctx->mosq = NULL;
    return false;
  }
  return true;
}

/* Abonnement : on reste large (wildcard) mais on ne TRAITE QUE les "/cmd" dans on_message().
   Avantage: pas besoin d'énumérer les topics de la table ici, et zéro boucle. */
bool mqtt_subscribe_all(mqtt_ctx_t *ctx) {
  if (!ctx || !ctx->mosq) return false;
  int rc = mosquitto_subscribe(ctx->mosq, NULL, "#", 1);
  if (rc != MOSQ_ERR_SUCCESS) {
    LOGE("Subscribe '#' rc=%d", rc);
    return false;
  }
  return true;
}

/* Publier du JSON sur un topic arbitraire */
bool mqtt_publish_json(mqtt_ctx_t *ctx, const char *topic, const char *json_str) {
  if (!ctx || !ctx->mosq || !topic || !json_str) return false;
  int rc = mosquitto_publish(ctx->mosq, NULL, topic, (int)strlen(json_str), json_str, 1, false);
  if (rc != MOSQ_ERR_SUCCESS) {
    LOGE("publish '%s' rc=%d", topic, rc);
    return false;
  }
  return true;
}

/* Appelé par le backend CAN quand une trame arrive (CAN → MQTT) */
void mqtt_on_can_message(mqtt_ctx_t *ctx, const entry_t *e, const uint8_t data[8]) {
  if (!ctx || !e) return;

  /* Unpack → JSON */
  cJSON *obj = unpack_payload(data, e);
  if (!obj) { LOGE("Unpack échoué id=0x%X", e->can_id); return; }

  char *out = cJSON_PrintUnformatted(obj);
  cJSON_Delete(obj);
  if (!out) { LOGE("cJSON_PrintUnformatted"); return; }

  /* Publier sur "<base>/state" (jamais traité par on_message car on filtre sur '/cmd') */
  char topic_state[256];
  topic_state_from_base(e->topic, topic_state, sizeof(topic_state));

  bool ok = mqtt_publish_json(ctx, topic_state, out);
  free(out);

  if (ok) LOGI("CAN->MQTT OK: id=0x%X topic=%s", e->can_id, topic_state);
  else    LOGE("CAN->MQTT publish échoué topic=%s", topic_state);
}

/* Attache le bundle user (table + can + self) pour les callbacks */
void mqtt_set_user_data(mqtt_ctx_t *ctx, const table_t *t, can_ctx_t *can) {
  if (!ctx || !ctx->mosq) return;
  user_bundle_t *ub = (user_bundle_t*)malloc(sizeof(user_bundle_t));
  ub->table = t;
  ub->can   = can;
  ub->mqtt  = ctx;
  mosquitto_userdata_set(ctx->mosq, ub);
}

/* Nettoyage */
void mqtt_cleanup(mqtt_ctx_t *ctx) {
  if (!ctx) return;
  if (ctx->mosq) {
    /* libérer le userdata si alloué */
    void *ud = mosquitto_userdata_get(ctx->mosq);
    if (ud) free(ud);
    mosquitto_loop_stop(ctx->mosq, true);
    mosquitto_disconnect(ctx->mosq);
    mosquitto_destroy(ctx->mosq);
    ctx->mosq = NULL;
  }
  mosquitto_lib_cleanup();
}
