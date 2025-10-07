#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <cjson/cJSON.h>
#include <string.h>
#include <stdio.h>   // snprintf

#include "mqtt_io.h"
#include "table.h"
#include "pack.h"
#include "types.h"
#include "can_io.h"

/* ---- STUB CAN: éviter l'undefined reference dans mqtt_io.c (on ne l'utilise pas ici) ---- */
bool can_send(can_ctx_t *ctx, uint32_t can_id, const uint8_t data[8]) {
  (void)ctx; (void)can_id; (void)data;
  return true;
}

/* ---- WRAP publish: le linker redirige mqtt_publish_json -> __wrap_mqtt_publish_json ---- */
static char g_topic[256];
static char g_payload[1024];

bool __wrap_mqtt_publish_json(mqtt_ctx_t *ctx, const char *topic, const char *json_str){
  (void)ctx;
  snprintf(g_topic, sizeof(g_topic), "%s", topic ? topic : "");
  snprintf(g_payload, sizeof(g_payload), "%s", json_str ? json_str : "");
  return true; // simule succès
}

static const char *CFG = "tests/data/conv_ok.json";

static void test_mqtt_publish_state_topic(void **state){
  (void)state;
  table_t t={0}; assert_true(table_load(&t, CFG));
  const entry_t *e = table_find_by_topic(&t, "led/config"); assert_non_null(e);

  cJSON *obj = cJSON_Parse("{\"group_id\":1,\"intensity\":128,\"color\":\"#00FDFF\",\"mode\":\"ON\",\"interval\":10}");
  uint8_t b[8]={0}; assert_non_null(obj);
  assert_true(pack_payload(b, e, obj)); cJSON_Delete(obj);

  mqtt_ctx_t m={0};
  memset(g_topic,0,sizeof(g_topic));
  memset(g_payload,0,sizeof(g_payload));

  assert_true(mqtt_on_can_message(&m, e, b));
  assert_string_equal(g_topic, "led/config/state");

  cJSON *out = cJSON_Parse(g_payload); assert_non_null(out);
  assert_int_equal(cJSON_GetObjectItem(out,"group_id")->valueint, 1);
  assert_int_equal(cJSON_GetObjectItem(out,"intensity")->valueint, 128);
  assert_string_equal(cJSON_GetObjectItem(out,"color")->valuestring, "#00FDFF");
  assert_string_equal(cJSON_GetObjectItem(out,"mode")->valuestring, "ON");
  assert_int_equal(cJSON_GetObjectItem(out,"interval")->valueint, 10);
  cJSON_Delete(out);

  table_free(&t);
}

int main(void){
  const struct CMUnitTest tests[] = {
    cmocka_unit_test(test_mqtt_publish_state_topic),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
