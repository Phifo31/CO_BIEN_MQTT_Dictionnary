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

/* ------------------------------------------------------------------ */
/* Hook (injectable en test) : par défaut -> mqtt_publish_json        */
/* ------------------------------------------------------------------ */
mqtt_publish_fn mqtt_publish_hook = NULL;

/* ====== userdata passé aux callbacks (même layout que dans main.c) ====== */
typedef struct user_bundle_s {
  const table_t *table;
  can_ctx_t     *can;
  mqtt_ctx_t    *mqtt; /* optionnel */
} user_bundle_t;

/* Pour compiler même si l’en-tête <mosquitto.h> ne définit pas ces macros */
#ifndef MQTT_SUB_OPT_NO_LOCAL
#define MQTT_SUB_OPT_NO_LOCAL 0x04
#endif

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

/* Réception MQTT → CAN : on utilise le topic tel quel (plus de /cmd ni /state) */
static void on_message(struct mosquitto *mosq, void *userdata, const struct mosquitto_message *msg) {
  (void)mosq;
  if (!userdata || !msg || !msg->topic) return;

  user_bundle_t *ub = (user_bundle_t*)userdata;
  if (!ub->table || !ub->can) return;

  const entry_t *e = table_find_by_topic(ub->table, msg->topic);
  if (!e) {
    LOGW("Topic inconnu (mqtt->can): %s", msg->topic);
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
    LOGE("Pack échoué pour topic %s", msg->topic);
    return;
  }

  /* Envoi CAN */
  if (!can_send(ub->can, e->can_id, out8)) {
    LOGE("Envoi CAN échoué (id=0x%X, topic=%s)", e->can_id, e->topic);
    return;
  }
  LOGI("MQTT->CAN OK: topic=%s id=0x%X", e->topic, e->can_id);
}

/* ================= Implémentation API ================= */

bool mqtt_init(mqtt_ctx_t *ctx, const table_t *t, const char *host, int port, int keepalive) {
  (void)t;
  if (!ctx) return false;
  memset(ctx, 0, sizeof(*ctx));

  /* QoS par défaut */
  ctx->qos_sub = 1;
  ctx->qos_pub = 1;
  ctx->sub_opts = 0;

  mosquitto_lib_init();

  ctx->mosq = mosquitto_new(NULL, true, NULL);
  if (!ctx->mosq) {
    LOGE("mosquitto_new");
    return false;
  }

  /* Basculer en protocole MQTT v5 (prérequis pour "no-local") */
  mosquitto_int_option(ctx->mosq, MOSQ_OPT_PROTOCOL_VERSION, MQTT_PROTOCOL_V5);

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

  /* Initialise le hook si non fourni (prod = vraie publish) */
  if (!mqtt_publish_hook) mqtt_publish_hook = mqtt_publish_json;

  return true;
}

/* S’abonne à tous les topics de la table avec l’option MQTT v5 "no-local".
   On récupère la table via le user_data (user_bundle_t) déjà posé par main(). */
bool mqtt_subscribe_all(mqtt_ctx_t *ctx) {
  if (!ctx || !ctx->mosq) return false;

  const table_t *table = NULL;
  void *ud = mosquitto_userdata(ctx->mosq);
  if (ud) {
    user_bundle_t *ub = (user_bundle_t*)ud;
    table = ub->table;
  }

  if (!table || table->count == 0) {
    /* Fallback: on s’abonne à tout, mais sans garantie no-local topic par topic */
    int rc = mosquitto_subscribe(ctx->mosq, NULL, "#", ctx->qos_sub);
    if (rc != MOSQ_ERR_SUCCESS) {
      LOGE("Subscribe '#' rc=%d", rc);
      return false;
    }
    ctx->sub_opts = 0;
    return true;
  }

  /* Abonnement par topic exact avec NO_LOCAL */
  unsigned char opts = MQTT_SUB_OPT_NO_LOCAL;
  for (size_t i = 0; i < table->count; ++i) {
    const char *topic = table->entries[i].topic;
    if (!topic || !*topic) continue;

    int rc = mosquitto_subscribe_v5(ctx->mosq, NULL, topic, ctx->qos_sub, opts, NULL);
    if (rc != MOSQ_ERR_SUCCESS) {
      LOGE("Subscribe v5 '%s' rc=%d", topic, rc);
      return false;
    }
  }
  ctx->sub_opts = opts;
  LOGI("Abonnements v5 'no-local' appliqués sur %zu topic(s).", table->count);
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

/* Appelé par le backend CAN: trame reçue → publier sur le topic de la table (même topic) */
bool mqtt_on_can_message(mqtt_ctx_t *ctx, const entry_t *e, const uint8_t data[8]) {
  if (!ctx || !e) return false;

  cJSON *obj = unpack_payload(data, e);
  if (!obj) { LOGE("Unpack échoué id=0x%X", e->can_id); return false; }

  char *out = cJSON_PrintUnformatted(obj);
  cJSON_Delete(obj);
  if (!out) { LOGE("cJSON_PrintUnformatted"); return false; }

  const char *topic = e->topic;

  /* Utilise le hook si présent (tests), sinon la vraie fonction */
  mqtt_publish_fn fn = mqtt_publish_hook ? mqtt_publish_hook : mqtt_publish_json;
  bool ok = fn(ctx, topic, out);
  free(out);

  if (ok) LOGI("CAN->MQTT OK: id=0x%X topic=%s", e->can_id, topic);
  else    LOGE("CAN->MQTT publish échoué topic=%s", topic);

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

#ifdef UNIT_TEST
void mqtt__get_on_message_cb(void (**cb)(struct mosquitto*, void*, const struct mosquitto_message*)) {
  *cb = on_message;
}
#endif
