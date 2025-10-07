#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <string.h>
#include <cjson/cJSON.h>

#include "mqtt_io.h"
#include "table.h"
#include "pack.h"
#include "types.h"

/* ---- SPY MQTT publish ---- */
static char g_pub_topic[256];
static char g_pub_payload[512];

bool mqtt_publish_json(mqtt_ctx_t *ctx, const char *topic, const char *json_str) {
  (void)ctx;
  snprintf(g_pub_topic, sizeof(g_pub_topic), "%s", topic ? topic : "");
  snprintf(g_pub_payload, sizeof(g_pub_payload), "%s", json_str ? json_str : "");
  return true; // simuler succès
}

static void test_mqtt_publish_state_topic(void **state) {
  (void)state;
  table_t t = {0};
  assert_true(table_load(&t, "tests/data/conv_ok.json"));
  const entry_t *e = table_find_by_topic(&t, "led/config");
  assert_non_null(e);

  // Construire 8 octets via pack pour être réaliste
  cJSON *obj = cJSON_Parse("{\"group_id\":1,\"intensity\":128,\"color\":\"#00FDFF\",\"mode\":\"ON\",\"interval\":10}");
  uint8_t bytes[8]={0};
  assert_true(pack_payload(bytes, e, obj));
  cJSON_Delete(obj);

  mqtt_ctx_t m = {0};
  memset(g_pub_topic, 0, sizeof(g_pub_topic));
  memset(g_pub_payload, 0, sizeof(g_pub_payload));

  // Appeler la voie CAN->MQTT
  assert_true(mqtt_on_can_message(&m, e, bytes));

  // On doit publier sur <base>/state
  assert_string_equal(g_pub_topic, "led/config/state");

  // Payload JSON cohérent
  cJSON *out = cJSON_Parse(g_pub_payload);
  assert_non_null(out);
  assert_int_equal(cJSON_GetObjectItem(out,"group_id")->valueint, 1);
  assert_int_equal(cJSON_GetObjectItem(out,"intensity")->valueint, 128);
  assert_string_equal(cJSON_GetObjectItem(out,"color")->valuestring, "#00FDFF");
  assert_string_equal(cJSON_GetObjectItem(out,"mode")->valuestring, "ON");
  assert_int_equal(cJSON_GetObjectItem(out,"interval")->valueint, 10);
  cJSON_Delete(out);

  table_free(&t);
}

int main(void) {
  const struct CMUnitTest tests[] = {
    cmocka_unit_test(test_mqtt_publish_state_topic),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
