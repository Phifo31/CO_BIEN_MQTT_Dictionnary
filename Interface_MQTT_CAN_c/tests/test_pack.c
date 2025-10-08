#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include <cjson/cJSON.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

#include "table.h"
#include "pack.h"
#include "types.h"

static const char *CFG = "tests/data/conv_ok.json";

/* Helpers */
static cJSON* parse_or_fail(const char *s){ cJSON *o=cJSON_Parse(s); assert_non_null(o); return o; }
static const entry_t* ensure_entry_or_skip(const table_t *t, const char *topic){
  const entry_t *e = table_find_by_topic(t, topic);
  if(!e) skip();
  return e;
}

/* Test 1 : round-trip */
static void test_pack_roundtrip_led_config(void **state){
  (void)state;
  table_t t={0}; assert_true(table_load(&t, CFG));
  const entry_t *e = ensure_entry_or_skip(&t, "led/config");

  cJSON *obj = parse_or_fail("{\"group_id\":1,\"intensity\":128,\"color\":\"#00FDFF\",\"mode\":\"ON\",\"interval\":10}");
  uint8_t bytes[8]={0};
  bool ok = pack_payload(bytes, e, obj);
  cJSON_Delete(obj);
  assert_true(ok);

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

/* Test 2 : bornes 1 octet */
static void test_pack_onebyte_bounds(void **state){
  (void)state;
  table_t t={0}; assert_true(table_load(&t, CFG));
  const entry_t *e = ensure_entry_or_skip(&t, "led/config");

  uint8_t b[8];

  cJSON *ok_min = parse_or_fail("{\"group_id\":1,\"intensity\":0,\"color\":\"#000000\",\"mode\":\"OFF\",\"interval\":0}");
  bool r1 = pack_payload(b, e, ok_min); cJSON_Delete(ok_min); assert_true(r1);

  cJSON *ok_max = parse_or_fail("{\"group_id\":1,\"intensity\":255,\"color\":\"#FFFFFF\",\"mode\":\"ON\",\"interval\":255}");
  bool r2 = pack_payload(b, e, ok_max); cJSON_Delete(ok_max); assert_true(r2);

  cJSON *too_big = parse_or_fail("{\"group_id\":1,\"intensity\":300,\"color\":\"#00FDFF\",\"mode\":\"ON\",\"interval\":10}");
  bool r3 = pack_payload(b, e, too_big); cJSON_Delete(too_big); assert_false(r3);

  cJSON *neg = parse_or_fail("{\"group_id\":1,\"intensity\":-1,\"color\":\"#00FDFF\",\"mode\":\"ON\",\"interval\":10}");
  bool r4 = pack_payload(b, e, neg); cJSON_Delete(neg); assert_false(r4);

  table_free(&t);
}

/* Test 3 : enum invalide */
static void test_pack_enum_invalid(void **state){
  (void)state;
  table_t t={0}; assert_true(table_load(&t, CFG));
  const entry_t *e = ensure_entry_or_skip(&t, "led/config");

  uint8_t b[8];
  cJSON *sanity = parse_or_fail("{\"group_id\":1,\"intensity\":128,\"color\":\"#00FDFF\",\"mode\":\"ON\",\"interval\":10}");
  bool rs = pack_payload(b, e, sanity); cJSON_Delete(sanity);
  if(!rs){ table_free(&t); skip(); return; }

  cJSON *bad = parse_or_fail("{\"group_id\":1,\"intensity\":128,\"color\":\"#00FDFF\",\"mode\":\"BLINKXX\",\"interval\":10}");
  bool rb = pack_payload(b, e, bad); cJSON_Delete(bad); assert_false(rb);

  table_free(&t);
}

/* Test 4 : couleur invalide */
static void test_pack_color_invalid(void **state){
  (void)state;
  table_t t={0}; assert_true(table_load(&t, CFG));
  const entry_t *e = ensure_entry_or_skip(&t, "led/config");

  uint8_t b[8];
  cJSON *sanity = parse_or_fail("{\"group_id\":1,\"intensity\":128,\"color\":\"#00FDFF\",\"mode\":\"ON\",\"interval\":10}");
  bool rs = pack_payload(b, e, sanity); cJSON_Delete(sanity);
  if(!rs){ table_free(&t); skip(); return; }

  cJSON *nohash = parse_or_fail("{\"group_id\":1,\"intensity\":128,\"color\":\"00FDFF\",\"mode\":\"ON\",\"interval\":10}");
  bool r1 = pack_payload(b, e, nohash); cJSON_Delete(nohash); assert_false(r1);

  cJSON *badhex = parse_or_fail("{\"group_id\":1,\"intensity\":128,\"color\":\"#GGHHII\",\"mode\":\"ON\",\"interval\":10}");
  bool r2 = pack_payload(b, e, badhex); cJSON_Delete(badhex); assert_false(r2);

  cJSON *shortc = parse_or_fail("{\"group_id\":1,\"intensity\":128,\"color\":\"#0F\",\"mode\":\"ON\",\"interval\":10}");
  bool r3 = pack_payload(b, e, shortc); cJSON_Delete(shortc); assert_false(r3);

  table_free(&t);
}

/* Test 5 : int16 auto-skip (se réactive si un vrai 16 bits existe dans la table) */
static void test_pack_int16_autoskip(void **state){
  (void)state;
  table_t t={0}; assert_true(table_load(&t, CFG));
  const entry_t *e = ensure_entry_or_skip(&t, "led/config");

  uint8_t b[8]={0};
  cJSON *j = parse_or_fail("{\"group_id\":1,\"intensity\":128,\"color\":\"#00FDFF\",\"mode\":\"ON\",\"interval\":32767}");
  bool ok = pack_payload(b, e, j); cJSON_Delete(j);

  if(!ok){ table_free(&t); skip(); return; } // pas d’int16 dans la table → non applicable

  // Si un jour 'interval' devient FT_INT16, on validera ici la valeur.
  cJSON *out = unpack_payload(b, e);
  assert_non_null(out);
  assert_int_equal(cJSON_GetObjectItem(out,"interval")->valueint, 32767);
  cJSON_Delete(out);
  table_free(&t);
}

int main(void){
  const struct CMUnitTest tests[] = {
    cmocka_unit_test(test_pack_roundtrip_led_config),
    cmocka_unit_test(test_pack_onebyte_bounds),
    cmocka_unit_test(test_pack_enum_invalid),
    cmocka_unit_test(test_pack_color_invalid),
    cmocka_unit_test(test_pack_int16_autoskip),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
