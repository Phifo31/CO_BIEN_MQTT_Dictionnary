#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <mosquitto.h>
#include <cjson/cJSON.h>
#include <string.h>

#include "mqtt_io.h"
#include "table.h"
#include "types.h"
#include "pack.h"
#include "can_io.h"

/* ---- STUB CAN : capture l'appel à can_send sans toucher au réseau ---- */
static uint32_t g_last_can_id = 0;
static uint8_t  g_last_can_data[8];
bool can_send(can_ctx_t *ctx, uint32_t can_id, const uint8_t data[8]) {
  (void)ctx;
  g_last_can_id = can_id;
  memcpy(g_last_can_data, data, 8);
  return true;
}

/* Petit helper pour pousser un faux message Mosquitto dans on_message() */
extern void mosquitto_message_callback_set(struct mosquitto *, void (*)(struct mosquitto *, void *, const struct mosquitto_message *));
extern struct mosquitto_message; /* on utilise la struct réelle, mais sans broker */
extern void *mosquitto_userdata(struct mosquitto *); /* pour récupérer le user bundle */

typedef struct user_bundle_s {
  const table_t *table;
  can_ctx_t     *can;
  mqtt_ctx_t    *mqtt;
} user_bundle_t;

/* Déclaration des symboles internes de mqtt_io.c */
static void (*g_on_message)(struct mosquitto*, void*, const struct mosquitto_message*) = NULL;

/* Construit un faux message mosquitto_message */
static void fake_msg(const char *topic, const char *payload, struct mosquitto_message *out) {
  memset(out, 0, sizeof(*out));
  out->topic = (char*)topic;
  out->payload = (void*)payload;
  out->payloadlen = payload ? (int)strlen(payload) : 0;
}

/* Test: base et base/cmd => DOIT appeler can_send ; base/state => IGNORÉ */
static void test_mqtt_filter_paths(void **state) {
  (void)state;
  table_t t = {0};
  assert_true(table_load(&t, "tests/data/conv_ok.json"));
  const entry_t *e = table_find_by_topic(&t, "led/config");
  assert_non_null(e);

  mqtt_ctx_t m = {0};
  /* on n'appelle pas mqtt_init pour éviter le réseau ; on a juste besoin du callback */
  m.mosq = mosquitto_new(NULL, true, NULL);
  assert_non_null(m.mosq);

  /* Reproduire ce que fait mqtt_init : set callback on_message */
  extern void mqtt__get_on_message_cb(void (**cb)(struct mosquitto*, void*, const struct mosquitto_message*)); /* exposé via UNIT_TEST */
  mqtt__get_on_message_cb(&g_on_message);
  assert_non_null(g_on_message);

  can_ctx_t c = {0};
  user_bundle_t ub = { .table=&t, .can=&c, .mqtt=&m };
  mosquitto_user_data_set(m.mosq, &ub);

  const char *good_json = "{\"group_id\":1,\"intensity\":128,\"color\":\"#00FDFF\",\"mode\":\"ON\",\"interval\":10}";

  // 1) topic = base  -> traité
  struct mosquitto_message msg1; fake_msg("led/config", good_json, &msg1);
  g_last_can_id = 0;
  g_on_message(m.mosq, mosquitto_userdata(m.mosq), &msg1);
  assert_int_equal(g_last_can_id, e->can_id);

  // 2) topic = base/cmd -> traité
  struct mosquitto_message msg2; fake_msg("led/config/cmd", good_json, &msg2);
  g_last_can_id = 0;
  g_on_message(m.mosq, mosquitto_userdata(m.mosq), &msg2);
  assert_int_equal(g_last_can_id, e->can_id);

  // 3) topic = base/state -> ignoré
  struct mosquitto_message msg3; fake_msg("led/config/state", good_json, &msg3);
  g_last_can_id = 0xFFFFFFFF;
  g_on_message(m.mosq, mosquitto_userdata(m.mosq), &msg3);
  assert_int_equal(g_last_can_id, 0xFFFFFFFF);

  mosquitto_destroy(m.mosq);
  table_free(&t);
}

int main(void) {
  const struct CMUnitTest tests[] = {
    cmocka_unit_test(test_mqtt_filter_paths),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
