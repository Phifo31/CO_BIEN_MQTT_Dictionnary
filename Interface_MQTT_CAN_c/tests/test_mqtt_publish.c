#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <cjson/cJSON.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

#include "types.h"
#include "table.h"
#include "pack.h"
#include "mqtt_io.h"

/* ---- Stub CAN (pas d'envoi réel côté test) ---- */
bool can_send(can_ctx_t *ctx, uint32_t can_id, const uint8_t data[8]) {
  (void)ctx; (void)can_id; (void)data; return true;
}

/* ---- Mock publish MQTT (injecté via le hook) ---- */
static char g_topic[256];
static char g_payload[1024];

static bool mock_publish(mqtt_ctx_t *ctx, const char *topic, const char *json_str) {
  (void)ctx;
  snprintf(g_topic, sizeof(g_topic), "%s", topic ? topic : "");
  snprintf(g_payload, sizeof(g_payload), "%s", json_str ? json_str : "");
  return true;  // Simule un publish OK
}

static const char *CFG = "tests/data/conv_ok.json";

/* Test : CAN->MQTT publie bien sur le topic de base (sans /state), payload JSON cohérent */
static void test_mqtt_publish_base_topic(void **state) {
  (void)state;

  table_t t = (table_t){0};
  assert_true(table_load(&t, CFG));

  const entry_t *e = table_find_by_topic(&t, "led/config");
  if (!e) { table_free(&t); skip(); return; }  // auto-skip si cette entrée n’existe pas

  /* Hook mock avant l'appel testé */
  mqtt_publish_hook = mock_publish;

  /* Trame 8 octets équivalente à:
     group_id=1, intensity=128, color=#00FDFF, mode=ON, interval=10 */
  uint8_t b[8] = { 0x01, 0x80, 0x00, 0xFD, 0xFF, 0x01, 0x0A, 0x00 };

  mqtt_ctx_t m = (mqtt_ctx_t){0};  // pas de broker réel nécessaire
  bool ok = mqtt_on_can_message(&m, e, b);
  assert_true(ok);

  /* Vérifie que l’on publie sur le topic de base (plus de /state) */
  assert_string_equal(g_topic, "led/config");

  /* Vérifie le JSON publié */
  cJSON *out = cJSON_Parse(g_payload);
  assert_non_null(out);
  assert_int_equal(cJSON_GetObjectItem(out, "group_id")->valueint, 1);
  assert_int_equal(cJSON_GetObjectItem(out, "intensity")->valueint, 128);
  assert_string_equal(cJSON_GetObjectItem(out, "color")->valuestring, "#00FDFF");
  assert_string_equal(cJSON_GetObjectItem(out, "mode")->valuestring, "ON");
  assert_int_equal(cJSON_GetObjectItem(out, "interval")->valueint, 10);
  cJSON_Delete(out);

  table_free(&t);

  /* Reset du hook pour ne pas polluer d’autres tests */
  mqtt_publish_hook = NULL;
}

int main(void) {
  const struct CMUnitTest tests[] = {
    cmocka_unit_test(test_mqtt_publish_base_topic),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
