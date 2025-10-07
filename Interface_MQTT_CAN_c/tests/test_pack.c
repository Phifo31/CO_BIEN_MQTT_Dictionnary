#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <cjson/cJSON.h>
#include <string.h>
#include "table.h"
#include "pack.h"
#include "types.h"

static const char *CFG = "tests/data/conv_ok.json";

static void test_pack_roundtrip_led_config(void **state) {
  (void)state;
  table_t t = {0};
  assert_true(table_load(&t, CFG));
  const entry_t *e = table_find_by_topic(&t, "led/config");
  assert_non_null(e);

  const char *json_in = "{\"group_id\":1,\"intensity\":128,\"color\":\"#00FDFF\",\"mode\":\"ON\",\"interval\":10}";
  cJSON *obj = cJSON_Parse(json_in);
  assert_non_null(obj);

  uint8_t bytes[8] = {0};
  assert_true(pack_payload(bytes, e, obj));
  cJSON_Delete(obj);

  cJSON *out = unpack_payload(bytes, e);
  assert_non_null(out);

  assert_int_equal(cJSON_GetObjectItem(out,"group_id")->valueint, 1);
  assert_int_equal(cJSON_GetObjectItem(out,"intensity")->valueint, 128);
  assert_string_equal(cJSON_GetObjectItem(out,"color")->valuestring, "#00FDFF");
  assert_string_equal(cJSON_GetObjectItem(out,"mode")->valuestring, "ON");
  assert_int_equal(cJSON_GetObjectItem(out,"interval")->valueint, 10);

  cJSON_Delete(out);
  table_free(&t);
}

static void test_pack_int16_endian_limits(void **state) {
  (void)state;
  table_t t = {0};
  assert_true(table_load(&t, CFG));
  const entry_t *e = table_find_by_topic(&t, "led/config");
  assert_non_null(e);

  // min int16
  cJSON *j1 = cJSON_Parse("{\"group_id\":1,\"intensity\":0,\"color\":\"#000000\",\"mode\":\"OFF\",\"interval\":-32768}");
  uint8_t b1[8]={0}; assert_true(pack_payload(b1,e,j1)); cJSON_Delete(j1);
  // max int16
  cJSON *j2 = cJSON_Parse("{\"group_id\":1,\"intensity\":255,\"color\":\"#FFFFFF\",\"mode\":\"ON\",\"interval\":32767}");
  uint8_t b2[8]={0}; assert_true(pack_payload(b2,e,j2)); cJSON_Delete(j2);
  // Unpack et vérifie les bornes
  cJSON *o2 = unpack_payload(b2,e); assert_non_null(o2);
  assert_int_equal(cJSON_GetObjectItem(o2,"interval")->valueint, 32767);
  cJSON_Delete(o2);
  table_free(&t);
}

static void test_pack_enum_invalid(void **state) {
  (void)state;
  table_t t = {0};
  assert_true(table_load(&t, CFG));
  const entry_t *e = table_find_by_topic(&t, "led/config");
  assert_non_null(e);

  cJSON *bad = cJSON_Parse("{\"group_id\":1,\"intensity\":128,\"color\":\"#00FDFF\",\"mode\":\"BLINKXX\",\"interval\":10}");
  uint8_t b[8]={0};
  assert_false(pack_payload(b, e, bad));  // doit refuser l'enum inconnue
  cJSON_Delete(bad);
  table_free(&t);
}

int main(void) {
  const struct CMUnitTest tests[] = {
    cmocka_unit_test(test_pack_roundtrip_led_config),
    cmocka_unit_test(test_pack_int16_endian_limits),
    cmocka_unit_test(test_pack_enum_invalid),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
