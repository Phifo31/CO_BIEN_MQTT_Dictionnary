#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <mosquitto.h>
#include <string.h>
#include <cjson/cJSON.h>

#include "mqtt_io.h"
#include "table.h"
#include "pack.h"
#include "can_io.h"

/* ---- Stub CAN : on capture juste l'ID envoyé ---- */
static uint32_t g_last_can_id = 0xFFFFFFFF;
bool can_send(can_ctx_t *ctx, uint32_t can_id, const uint8_t data[8]) {
  (void)ctx; (void)data; g_last_can_id = can_id; return true;
}

/* Récupérer le callback interne de mqtt_io.c (exposé en UNIT_TEST) */
void mqtt__get_on_message_cb(void (**cb)(struct mosquitto*, void*, const struct mosquitto_message*));

/* Message synthétique */
static void fake_msg(const char *topic, const char *payload, struct mosquitto_message *out) {
  memset(out, 0, sizeof(*out));
  out->topic      = (char*)topic;
  out->payload    = (void*)payload;
  out->payloadlen = payload ? (int)strlen(payload) : 0;
}

/* Construit un JSON valide pour une entry donnée */
static char* build_valid_json_for_entry(const entry_t *e) {
  cJSON *obj = cJSON_CreateObject();
  if (!obj) return NULL;

  for (size_t i = 0; i < e->field_count; ++i) {
    const field_spec_t *fs = &e->fields[i];
    switch (fs->type) {
      case FT_INT:   cJSON_AddNumberToObject(obj, fs->name, 1); break;
      case FT_BOOL:  cJSON_AddBoolToObject  (obj, fs->name, 1); break;
      case FT_HEX:   cJSON_AddStringToObject(obj, fs->name, "#00FDFF"); break;
      case FT_INT16: cJSON_AddNumberToObject(obj, fs->name, 10); break;
      case FT_ENUM: {
        const char *label = NULL;
        if (fs->enum_list) label = fs->enum_list->key;  /* premier label dispo */
        if (!label) label = "ON";
        cJSON_AddStringToObject(obj, fs->name, label);
      } break;
    }
  }
  char *s = cJSON_PrintUnformatted(obj);
  cJSON_Delete(obj);
  return s; /* à free() par l'appelant */
}

static void test_mqtt_filter_all_topics(void **state) {
  (void)state;

  mosquitto_lib_init();

  table_t t = (table_t){0};
  assert_true(table_load(&t, "tests/data/conv_ok.json"));
  if (t.entry_count == 0) { table_free(&t); mosquitto_lib_cleanup(); skip(); return; }

  mqtt_ctx_t m = (mqtt_ctx_t){0};
  m.mosq = mosquitto_new(NULL, true, NULL);
  assert_non_null(m.mosq);

  void (*on_msg)(struct mosquitto*, void*, const struct mosquitto_message*) = NULL;
  mqtt__get_on_message_cb(&on_msg);
  assert_non_null(on_msg);

  can_ctx_t c = (can_ctx_t){0};
  struct { const table_t *table; can_ctx_t *can; mqtt_ctx_t *mqtt; } ub = { .table=&t, .can=&c, .mqtt=&m };
  mosquitto_user_data_set(m.mosq, &ub);

  struct mosquitto_message msg;

  /* Pour chaque topic de la table : topic exact accepté, /cmd et /state ignorés */
  for (size_t i = 0; i < t.entry_count; ++i) {
    const entry_t *e = &t.entries[i];

    /* JSON valide pour ce topic */
    char *good = build_valid_json_for_entry(e);
    assert_non_null(good);

    /* 1) Topic EXACT -> doit appeler can_send (ID = e->can_id) */
    g_last_can_id = 0xFFFFFFFF;
    fake_msg(e->topic, good, &msg);
    on_msg(m.mosq, mosquitto_userdata(m.mosq), &msg);
    assert_int_equal(g_last_can_id, e->can_id);

    /* 2) <topic>/cmd -> ignoré */
    char topic_cmd[256]; snprintf(topic_cmd, sizeof(topic_cmd), "%s/cmd", e->topic);
    g_last_can_id = 0xFFFFFFFF;
    fake_msg(topic_cmd, good, &msg);
    on_msg(m.mosq, mosquitto_userdata(m.mosq), &msg);
    assert_int_equal(g_last_can_id, 0xFFFFFFFF);

    /* 3) <topic>/state -> ignoré */
    char topic_state[256]; snprintf(topic_state, sizeof(topic_state), "%s/state", e->topic);
    g_last_can_id = 0xFFFFFFFF;
    fake_msg(topic_state, good, &msg);
    on_msg(m.mosq, mosquitto_userdata(m.mosq), &msg);
    assert_int_equal(g_last_can_id, 0xFFFFFFFF);

    free(good);
  }

  mosquitto_destroy(m.mosq);
  table_free(&t);
  mosquitto_lib_cleanup();
}

int main(void) {
  const struct CMUnitTest tests[] = {
    cmocka_unit_test(test_mqtt_filter_all_topics),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
