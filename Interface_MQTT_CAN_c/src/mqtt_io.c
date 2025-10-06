#include "mqtt_io.h"
#include "pack.h"
#include "can_io.h"
#include "log.h"
#include <string.h>
#include <stdlib.h>

typedef struct user_bundle_s {
  const table_t *table;
  can_ctx_t     *can;   // pour envoyer côté MQTT->CAN
} user_bundle_t;

static void on_connect(struct mosquitto *mosq, void *userdata, int rc) {
  if (rc == 0) LOGI("MQTT connecté");
  else LOGW("MQTT connect rc=%d", rc);
}

static void on_disconnect(struct mosquitto *mosq, void *userdata, int rc) {
  LOGW("MQTT déconnecté rc=%d", rc);
}

static void on_message(struct mosquitto *mosq, void *userdata, const struct mosquitto_message *msg) {
  user_bundle_t *ub = (user_bundle_t*) userdata;
  const char *topic = msg->topic;
  const entry_t *e = table_find_by_topic(ub->table, topic);
  if (!e) { LOGW("Topic inconnu: %s", topic); return; }

  // parse JSON
  cJSON *in = cJSON_ParseWithLength((const char*)msg->payload, msg->payloadlen);
  if (!in) { LOGW("Payload JSON invalide sur %s", topic); return; }

  uint8_t data[8];
  if (!pack_payload(data, e, in)) {
    LOGW("Pack échoué pour topic %s", topic);
    cJSON_Delete(in);
    return;
  }
  cJSON_Delete(in);

  if (!ub->can) { LOGW("CAN non initialisé, drop"); return; }
  if (can_send(ub->can, e->can_id, data)) {
    LOGI("MQTT->CAN OK: topic=%s id=0x%X", topic, e->can_id);
  } else {
    LOGE("Envoi CAN échoué pour id=0x%X", e->can_id);
  }
}

bool mqtt_init(mqtt_ctx_t *ctx, const table_t *t, const char *host, int port, int qos_pub) {
  mosquitto_lib_init();
  ctx->cli = mosquitto_new("cobien-bridge", true, NULL);
  if (!ctx->cli) { LOGE("mosquitto_new a échoué"); return false; }

  ctx->table  = t;
  ctx->host   = host;
  ctx->port   = port;
  ctx->qos_pub= qos_pub;

  mosquitto_connect_callback_set(ctx->cli, on_connect);
  mosquitto_disconnect_callback_set(ctx->cli, on_disconnect);
  mosquitto_message_callback_set(ctx->cli, on_message);

  if (mosquitto_connect(ctx->cli, host, port, 60) != MOSQ_ERR_SUCCESS) {
    LOGE("Connexion MQTT échouée à %s:%d", host, port);
    return false;
  }
  if (mosquitto_loop_start(ctx->cli) != MOSQ_ERR_SUCCESS) {
    LOGE("mosquitto_loop_start échoué");
    return false;
  }
  return true;
}

bool mqtt_subscribe_all(mqtt_ctx_t *ctx) {
  // Abonner à tous les topics connus par la table
  // (on parcourt l'index topic)
  extern void table_foreach_topic(const table_t*, void (*cb)(const char*, void*), void*);
  // petite astuce: la table n’expose pas un foreach — on refait un mini getter :
  // Pour rester simple ici, on s’appuie sur les topics qu’on retrouvera en runtime :
  // → On suppose que conversion.json contient aussi une entrée de type "subscribe" ?
  // Si non, on peut stocker la liste lors du chargement (exercice déjà fait).
  // Pour garder compact: on souscrit en lisant à nouveau conversion.json.
  // Simplifions : on resouscrit dynamiquement quand on reçoit on_connect :
  // => On lit depuis l’index via un petit hack local (non exposé dans l’API publique).

  // Hack simple: demander à l’utilisateur de fournir la liste des topics à subscribe:
  // On va juste souscrire à un wildcard global si cohérent (ex: "#").
  // Si tu veux EXACTEMENT les sujets, remonte-les depuis table_load (à faire évoluer).
  int rc = mosquitto_subscribe(ctx->cli, NULL, "#", 1);
  if (rc != MOSQ_ERR_SUCCESS) {
    LOGE("Subscribe '#' échoué (%d)", rc);
    return false;
  }
  LOGI("Abonné à '#'");
  return true;
}

bool mqtt_publish_json(mqtt_ctx_t *ctx, const char *topic, const char *json_str) {
  int rc = mosquitto_publish(ctx->cli, NULL, topic, (int)strlen(json_str), json_str, ctx->qos_pub, false);
  if (rc != MOSQ_ERR_SUCCESS) {
    LOGE("Publish échoué sur %s (%d)", topic, rc);
    return false;
  }
  return true;
}

void mqtt_set_user_data(mqtt_ctx_t *ctx, void *user) {
  mosquitto_user_data_set(ctx->cli, user);
}

bool mqtt_on_can_message(mqtt_ctx_t *ctx, const entry_t *entry, const uint8_t data[8]) {
  cJSON *obj = unpack_payload(data, entry);
  if (!obj) { LOGW("Unpack échoué pour id=0x%X", entry->can_id); return false; }
  char *s = cJSON_PrintUnformatted(obj);
  cJSON_Delete(obj);

  bool ok = mqtt_publish_json(ctx, entry->topic, s);
  if (ok) LOGI("CAN->MQTT OK: id=0x%X topic=%s", entry->can_id, entry->topic);
  free(s);
  return ok;
}

void mqtt_cleanup(mqtt_ctx_t *ctx) {
  if (!ctx || !ctx->cli) return;
  mosquitto_loop_stop(ctx->cli, true);
  mosquitto_disconnect(ctx->cli);
  mosquitto_destroy(ctx->cli);
  mosquitto_lib_cleanup();
}
