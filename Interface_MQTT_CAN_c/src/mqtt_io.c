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
#include <errno.h>

typedef struct user_bundle_s {
  const table_t *table;
  struct can_ctx_s *can;
  mqtt_ctx_t *mqtt;
} user_bundle_t;

static int ends_with(const char *s, const char *suf){
  size_t a=strlen(s), b=strlen(suf);
  return (a>=b) && (strcmp(s+(a-b),suf)==0);
}
static void topic_base_from_input(const char *in, char *base, size_t n){
  if(!in||!base||!n) return;
  if(ends_with(in,"/state")){ base[0]='\0'; return; } /* ignoré */
  if(ends_with(in,"/cmd")){
    size_t L=strlen(in);
    size_t nb=(L>=4)?(L-4):0;
    if(nb>=n) nb=n-1;
    memcpy(base,in,nb); base[nb]='\0'; return;
  }
  snprintf(base,n,"%s",in);
}

/* MQTT callbacks */
static void on_connect(struct mosquitto *m, void *ud, int rc){
  (void)m; (void)ud;
  if(rc==0) LOGI("MQTT connecté");
  else      LOGW("MQTT connect rc=%d", rc);
}
static void on_disconnect(struct mosquitto *m, void *ud, int rc){
  (void)m; (void)ud; LOGW("MQTT déconnecté rc=%d", rc);
}

/* MQTT -> CAN */
static void on_message(struct mosquitto *m, void *ud, const struct mosquitto_message *msg){
  (void)m;
  if(!ud || !msg || !msg->topic) return;

  user_bundle_t *ub = (user_bundle_t*)ud;
  if(!ub->table || !ub->can) return;

  char base[256]; topic_base_from_input(msg->topic, base, sizeof(base));
  if(base[0]=='\0') return; /* /state ignoré */

  const entry_t *e = table_find_by_topic(ub->table, base);
  if(!e){ LOGW("Topic inconnu: %s", msg->topic); return; }

  cJSON *in = NULL;
  if(msg->payload && msg->payloadlen>0){
    char *buf = (char*)malloc((size_t)msg->payloadlen+1);
    if(!buf) return;
    memcpy(buf, msg->payload, (size_t)msg->payloadlen);
    buf[msg->payloadlen]='\0';
    in = cJSON_Parse(buf);
    free(buf);
  }
  if(!in){ LOGW("Payload JSON invalide sur %s", msg->topic); return; }

  uint8_t out8[8]={0};
  bool ok = pack_payload(out8, e, in);
  cJSON_Delete(in);
  if(!ok){ LOGE("Pack échoué pour topic %s", base); return; }

  if(!can_send(ub->can, e->can_id, out8)){
    LOGE("Envoi CAN échoué id=0x%X", e->can_id);
    return;
  }
  LOGI("MQTT->CAN OK topic=%s id=0x%X", base, e->can_id);
}

/* API */
bool mqtt_init(mqtt_ctx_t *ctx, const char *host, int port, int keepalive){
  if(!ctx) return false;
  memset(ctx,0,sizeof(*ctx));
  ctx->qos_pub=1; ctx->qos_sub=1;

  mosquitto_lib_init();
  ctx->mosq = mosquitto_new(NULL, true, NULL);
  if(!ctx->mosq){ LOGE("mosquitto_new"); return false; }

  /* Forcer MQTT v5 pour pouvoir utiliser no_local */
  mosquitto_int_option(ctx->mosq, MOSQ_OPT_PROTOCOL_VERSION, MQTT_PROTOCOL_V5);

  mosquitto_connect_callback_set(ctx->mosq, on_connect);
  mosquitto_disconnect_callback_set(ctx->mosq, on_disconnect);
  mosquitto_message_callback_set(ctx->mosq, on_message);

  if(mosquitto_connect(ctx->mosq, host?host:"localhost", port>0?port:1883, keepalive>0?keepalive:60) != MOSQ_ERR_SUCCESS){
    LOGE("mosquitto_connect");
    mosquitto_destroy(ctx->mosq); ctx->mosq=NULL;
    mosquitto_lib_cleanup();
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



/* Pompe non-bloquante; à appeler dans my_loop */
bool mqtt_poll(mqtt_ctx_t *ctx){
  if(!ctx||!ctx->mosq) return false;
  int rc = mosquitto_loop(ctx->mosq, 0/*timeout_ms*/, 1/*max_packets*/);
  if(rc != MOSQ_ERR_SUCCESS && rc != MOSQ_ERR_NO_CONN){
    LOGW("mosquitto_loop rc=%d", rc);
    return false;
  }
  return true;
}

bool mqtt_publish_json(mqtt_ctx_t *ctx, const char *topic, const char *json_str){
  if(!ctx||!ctx->mosq||!topic||!json_str) return false;
  int rc = mosquitto_publish(ctx->mosq, NULL, topic, (int)strlen(json_str), json_str, ctx->qos_pub, false);
  if(rc != MOSQ_ERR_SUCCESS){
    LOGE("publish '%s' rc=%d", topic, rc);
    return false;
  }
  return true;
}

void mqtt_set_qos(mqtt_ctx_t *ctx, int qos_sub, int qos_pub){
  if(!ctx) return;
  if(qos_sub >= 0 && qos_sub <= 2) ctx->qos_sub = qos_sub;
  if(qos_pub  >= 0 && qos_pub  <= 2) ctx->qos_pub  = qos_pub;
}

bool mqtt_handle_can_message(mqtt_ctx_t *ctx, const entry_t *e, const uint8_t data[8]){
  if(!ctx || !e) return false;
  cJSON *obj = unpack_payload(data, e);
  if(!obj){ LOGE("Unpack échoué id=0x%X", e->can_id); return false; }
  char *out = cJSON_PrintUnformatted(obj);
  cJSON_Delete(obj);
  if(!out){ LOGE("cJSON_PrintUnformatted"); return false; }

  bool ok = mqtt_publish_json(ctx, e->topic, out); /* publier sur le topic de base */
  free(out);
  if(ok) LOGI("CAN->MQTT OK id=0x%X topic=%s", e->can_id, e->topic);
  else   LOGE("CAN->MQTT publish échoué topic=%s", e->topic);
  return ok;
}

void mqtt_set_user_data(mqtt_ctx_t *ctx, void *userdata){
  if(!ctx||!ctx->mosq) return;
  mosquitto_user_data_set(ctx->mosq, userdata);
}

void mqtt_cleanup(mqtt_ctx_t *ctx){
  if(!ctx) return;
  if(ctx->mosq){
    mosquitto_disconnect(ctx->mosq);
    mosquitto_destroy(ctx->mosq);
    ctx->mosq=NULL;
  }
  mosquitto_lib_cleanup();
}
