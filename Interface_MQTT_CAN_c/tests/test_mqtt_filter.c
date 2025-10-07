#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <mosquitto.h>
#include <string.h>

#include "mqtt_io.h"
#include "table.h"
#include "types.h"
#include "pack.h"
#include "can_io.h"

/* Stub CAN: capture l'appel à can_send() */
static uint32_t g_last_can_id = 0xFFFFFFFF;
bool can_send(can_ctx_t *ctx, uint32_t can_id, const uint8_t data[8]){
  (void)ctx; (void)data; g_last_can_id = can_id; return true;
}

/* Getter du callback (défini dans mqtt_io.c sous UNIT_TEST) */
void mqtt__get_on_message_cb(void (**cb)(struct mosquitto*, void*, const struct mosquitto_message*));

static void fake_msg(const char *topic, const char *payload, struct mosquitto_message *out){
  memset(out, 0, sizeof(*out)); out->topic=(char*)topic; out->payload=(void*)payload; out->payloadlen=payload?(int)strlen(payload):0;
}

static void test_mqtt_filter_paths(void **state){
  (void)state;
  table_t t={0}; assert_true(table_load(&t, "tests/data/conv_ok.json"));
  const entry_t *e = table_find_by_topic(&t, "led/config");
  if(!e){ table_free(&t); skip(); return; }      // auto-skip si l’entrée n’existe pas

  mqtt_ctx_t m={0};
  m.mosq = mosquitto_new(NULL, true, NULL); assert_non_null(m.mosq);

  void (*on_msg)(struct mosquitto*, void*, const struct mosquitto_message*) = NULL;
  mqtt__get_on_message_cb(&on_msg); assert_non_null(on_msg);

  can_ctx_t c={0};
  struct { const table_t *table; can_ctx_t *can; mqtt_ctx_t *mqtt; } ub = { .table=&t, .can=&c, .mqtt=&m };
  mosquitto_user_data_set(m.mosq, &ub);

  const char *good = "{\"group_id\":1,\"intensity\":128,\"color\":\"#00FDFF\",\"mode\":\"ON\",\"interval\":10}";
  struct mosquitto_message msg;

  // base -> traité
  g_last_can_id = 0xFFFFFFFF; fake_msg("led/config", good, &msg);
  on_msg(m.mosq, mosquitto_userdata(m.mosq), &msg);
  assert_int_equal(g_last_can_id, e->can_id);

  // base/cmd -> traité
  g_last_can_id = 0xFFFFFFFF; fake_msg("led/config/cmd", good, &msg);
  on_msg(m.mosq, mosquitto_userdata(m.mosq), &msg);
  assert_int_equal(g_last_can_id, e->can_id);

  // base/state -> ignoré
  g_last_can_id = 0xFFFFFFFF; fake_msg("led/config/state", good, &msg);
  on_msg(m.mosq, mosquitto_userdata(m.mosq), &msg);
  assert_int_equal(g_last_can_id, 0xFFFFFFFF);

  mosquitto_destroy(m.mosq);
  table_free(&t);
}

int main(void){
  const struct CMUnitTest tests[] = {
    cmocka_unit_test(test_mqtt_filter_paths),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
