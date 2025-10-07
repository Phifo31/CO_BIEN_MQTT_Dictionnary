#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include "table.h"
#include "types.h"

static const char *OK = "tests/data/conv_ok.json";
static const char *MISS = "tests/data/conv_missing_fields.json";
static const char *NEST = "tests/data/conv_nested.json";

static void test_table_load_ok(void **state) {
  (void)state;
  table_t t = {0};
  assert_true(table_load(&t, OK));
  assert_non_null(table_find_by_topic(&t, "led/config"));
  assert_non_null(table_find_by_id(&t, 0x51E));  // ajuste si besoin
  table_free(&t);
}

static void test_table_missing_fields(void **state) {
  (void)state;
  table_t t = {0};
  assert_false(table_load(&t, MISS));  // doit échouer proprement
  table_free(&t);
}

static void test_table_nested_topic(void **state) {
  (void)state;
  table_t t = {0};
  assert_true(table_load(&t, NEST));
  assert_non_null(table_find_by_topic(&t, "led/config")); // même si enfoui
  table_free(&t);
}

int main(void) {
  const struct CMUnitTest tests[] = {
    cmocka_unit_test(test_table_load_ok),
    cmocka_unit_test(test_table_missing_fields),
    cmocka_unit_test(test_table_nested_topic),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
