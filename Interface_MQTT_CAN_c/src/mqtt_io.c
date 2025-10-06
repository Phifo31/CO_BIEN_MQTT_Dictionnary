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
#include <stdint.h>

/* ================= Helpers ================= */

static int ends_with(const char *s, const char *suffix) {
  size_t ls = strlen(s), lt = strlen(suffix);
  return (ls >= lt) && (strcmp(s + (ls - lt), suffix) == 0);
}

/* Déduit le "topic de base" accepté côté entrée:
   - "<base>/state"  -> ignoré (base = "")
   - "<base>/cmd"    -> retourne <base>
   - "<base>"        -> retourne <base> (compat ancien format)
*/
static void topic_base_from_input(const char *input, char *base, size_t base_sz) {
  if (!input || !base || base_sz == 0) return;
  base[0] = '\0';

  if (ends_with(input, "/state")) {
    /* On n'ingère jamais /state côté MQTT->CAN */
    return;
  }
  if (ends_with(input, "/cmd")) {
    size_t L = strlen(input), S = 4; /* "/cmd" */
    size_t nb = (L >= S) ? (L - S) : 0;
    if (nb >= base_sz) nb = base_sz - 1;
    memcpy(base, input, nb);
    base[nb] = '\0';
    return;
  }
  /* Sinon, on considère que c'est le topic de base (ancien format) */
  snprintf(base, base_sz, "%s", input);
}

/* Construit "<base>/state" (pour CAN->MQTT) */
static void topic_state_from_base(const char *base, char *state, size_t state_sz) {
  const char *suf = "/state";
  if (!base || !state || state_sz == 0) return;
  if (snprintf(state, state_sz, "%s%s", base, suf) >= (int)state_sz) {
    state[state_sz - 1] = '\0';
  }
}

/* ====== userdata passé aux callbacks (même layout que dans main.c) ====== */
typedef struct user_bundle_s {
  const table_t *table;
  can_ctx_t     *can;
  mqtt_ctx_t    *mqtt; /* optionnel */
} user_bundle_t;

/* ================= Callbacks Mosquitto ================= */

static void on_connect(struct mosquitto *mosq, void *userdata, int rc) {
  (void)mosq; (void)userdata;
  if (rc == 0) LOGI("MQTT connecté");
  else         LOGW("MQTT connect rc=%d", rc);
}

static void on_disconnect(struct mosquitto *mosq, void *userdata, int rc) {
  (void)mosq; (void)userdata;
  LOGW("MQTT déconnecté rc=%d", rc);
}

/* Réception MQTT → CAN : accepte "<base>" ET "<base>/cmd", ignore "<base>/state" */
static void on_message(struct mosquitto *mosq, void *userdata, const struct mosquitto_message *msg) {
  (void)mosq;
  if (!userdata || !msg || !msg->topic) return;

  user_bundle_t *ub = (user_bundle_t*)userdata;
  if (!ub->table || !ub->can) return;

  char base_topic[256];
  topic_base_from_input(msg->topic, base_topic, sizeof(base_topic));
  if (base_topic[0] == '\0') {
    /* /state (ou invalide) -> ignoré pour éviter tout écho */
    return;
  }

  const entry_t *e = table_find_by_topic(ub->table, base_topic);
  if (!e) {
    LOGW("Topic inconnu (mqtt->can): %s (base=%s)", msg->topic, base_topic);
    return;
  }

  /* Parse payload JSON */
  cJSON *in = NULL;
  if (msg->payload && msg->payloadlen > 0) {
    char *buf = (char*)malloc((size_t)msg->payloadlen + 1);
    if (!buf) { LOGE("malloc payload"); return; }
    memcpy(buf, msg->payload, (size_t)msg->payloadlen);
    buf[msg->payloadlen] = '\0';
    in = cJSON_Parse(buf);
    free(buf);
  }
  if (!in) { LOGW("Payload JSON invalide sur %s", msg->topic); return; }

  /* Pack -> 8 octets */
  uint8_t out8[8] = {0};
  bool ok_pack = pack_payload(out8, e, in);
  cJSON_Delete(in);
  if (!ok_pack) {
    LOGE("Pack échoué pour topic %s (base=%s)", msg->topic, base_topic);
    return;
  }

  /* Envoi CAN */
  if (!can_send(ub->can, e->can_id, out8)) {
    LOGE("Envoi CAN échoué (id=0x%X, topic=%s)", e->can_id, base_topic);
    return;
  }
  LOGI("MQTT->CAN OK: topic=%s id=0x%X", base_topic, e->can_id);
}

/* ================= Implémentation API ================= */

bool mqtt_init(mqtt_ctx_t *ctx, const table_t *t, const char *host, int port, int keepalive) {
  (void)t;
  if (!ctx) return false;
  memset(ctx, 0, sizeof(*ctx));

  /* QoS par défaut */
  ctx->qos_sub = 1;
  ctx->qos_pub = 1;

  mosquitto_lib_init();

  ctx->mosq = mosquitto_new(NULL, true, NULL);
  if (!ctx->mosq) {
    LOGE("mosquitto_new");
    return false;
  }

  mosquitto_connect_callback_set(ctx->mosq, on_connect);
  mosquitto_disconnect_callback_set(ctx->mosq, on_disconnect);
  mosquitto_message_callback_set(ctx->mosq, on_message);

  if (mosquitto_connect(ctx->mosq,
                        host ? host : "localhost",
                        port > 0 ? port : 1883,
                        keepalive > 0 ? keepalive : 60) != MOSQ_ERR_SUCCESS) {
    LOGE("mosquitto_connect");
    mosquitto_destroy(ctx->mosq);
    ctx->mosq = NULL;
    return false;
  }

  if (mosquitto_loop_start(ctx->mosq) != MOSQ_ERR_SUCCESS) {
    LOGE("mosquitto_loop_start");
    mosquitto_destroy(ctx->mosq);
    ctx->mosq = NULL;
    return false;
  }
  return true;
}

bool mqtt_subscribe_all(mqtt_ctx_t *ctx) {
  if (!ctx || !ctx->mosq) return false;
  int rc = mosquitto_subscribe(ctx->mosq, NULL, "#", ctx->qos_sub);
  if (rc != MOSQ_ERR_SUCCESS) {
    LOGE("Subscribe '#' rc=%d", rc);
    return false;
  }
  return true;
}

bool mqtt_publish_json(mqtt_ctx_t *ctx, const char *topic, const char *json_str) {
  if (!ctx || !ctx->mosq || !topic || !json_str) return false;
  int rc = mosquitto_publish(ctx->mosq, NULL, topic,
                             (int)strlen(json_str), json_str,
                             ctx->qos_pub, false);
  if (rc != MOSQ_ERR_SUCCESS) {
    LOGE("publish '%s' rc=%d", topic, rc);
    return false;
  }
  return true;
}

/* Appelé par le backend CAN: trame reçue → publier sur "<base>/state" */
bool mqtt_on_can_message(mqtt_ctx_t *ctx, const entry_t *e, const uint8_t data[8]) {
  if (!ctx || !e) return false;

  cJSON *obj = unpack_payload(data, e);
  if (!obj) { LOGE("Unpack échoué id=0x%X", e->can_id); return false; }

  char *out = cJSON_PrintUnformatted(obj);
  cJSON_Delete(obj);
  if (!out) { LOGE("cJSON_PrintUnformatted"); return false; }

  char topic_state[256];
  topic_state_from_base(e->topic, topic_state, sizeof(topic_state));

  bool ok = mqtt_publish_json(ctx, topic_state, out);
  free(out);

  if (ok) LOGI("CAN->MQTT OK: id=0x%X topic=%s", e->can_id, topic_state);
  else    LOGE("CAN->MQTT publish échoué topic=%s", topic_state);

  return ok;
}

/* Stocker un pointeur arbitraire (ex: user_bundle_t) dans le client */
void mqtt_set_user_data(mqtt_ctx_t *ctx, void *user) {
  if (!ctx || !ctx->mosq) return;
  mosquitto_user_data_set(ctx->mosq, user);
}

/* Configurer les QoS (0/1/2). Valeurs invalides ignorées. */
void mqtt_set_qos(mqtt_ctx_t *ctx, int qos_sub, int qos_pub) {
  if (!ctx) return;
  if (qos_sub >= 0 && qos_sub <= 2) ctx->qos_sub = qos_sub;
  if (qos_pub  >= 0 && qos_pub  <= 2) ctx->qos_pub = qos_pub;
}

void mqtt_cleanup(mqtt_ctx_t *ctx) {
  if (!ctx) return;
  if (ctx->mosq) {
    /* ne pas free le user data ici : possédé par l'appelant (main) */
    mosquitto_loop_stop(ctx->mosq, true);
    mosquitto_disconnect(ctx->mosq);
    mosquitto_destroy(ctx->mosq);
    ctx->mosq = NULL;
  }
  mosquitto_lib_cleanup();
}
