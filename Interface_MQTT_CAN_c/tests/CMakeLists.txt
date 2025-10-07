find_package(PkgConfig REQUIRED)
pkg_check_modules(CMOCKA REQUIRED cmocka)
include_directories(${CMAKE_SOURCE_DIR}/include ${CMOCKA_INCLUDE_DIRS})

# --- test_table ---
add_executable(test_table
  test_table.c
  ${CMAKE_SOURCE_DIR}/src/table.c
)
target_link_libraries(test_table ${CMOCKA_LIBRARIES} cjson)
add_test(NAME test_table COMMAND test_table)

# --- test_pack ---
add_executable(test_pack
  test_pack.c
  ${CMAKE_SOURCE_DIR}/src/table.c
  ${CMAKE_SOURCE_DIR}/src/pack.c
)
target_link_libraries(test_pack ${CMOCKA_LIBRARIES} cjson)
add_test(NAME test_pack COMMAND test_pack)

# --- test_mqtt_filter ---
add_executable(test_mqtt_filter
  test_mqtt_filter.c
  ${CMAKE_SOURCE_DIR}/src/mqtt_io.c
  ${CMAKE_SOURCE_DIR}/src/table.c
  ${CMAKE_SOURCE_DIR}/src/pack.c
)
target_link_libraries(test_mqtt_filter ${CMOCKA_LIBRARIES} cjson mosquitto)
target_compile_definitions(test_mqtt_filter PRIVATE UNIT_TEST=1)
add_test(NAME test_mqtt_filter COMMAND test_mqtt_filter)

# --- test_mqtt_publish ---
# On veut espionner mqtt_publish_json sans conflit de symboles:
#   -> on laisse mqtt_io.c tel quel
#   -> on 'wrap' la fonction au link: __wrap_mqtt_publish_json dans le test
add_executable(test_mqtt_publish
  test_mqtt_publish.c
  ${CMAKE_SOURCE_DIR}/src/mqtt_io.c
  ${CMAKE_SOURCE_DIR}/src/table.c
  ${CMAKE_SOURCE_DIR}/src/pack.c
)
target_link_libraries(test_mqtt_publish ${CMOCKA_LIBRARIES} cjson mosquitto)
target_compile_definitions(test_mqtt_publish PRIVATE UNIT_TEST=1)
# wrap du symbole pour éviter "multiple definition"
# target_link_options(test_mqtt_publish PRIVATE -Wl,--wrap=mqtt_publish_json)
add_test(NAME test_mqtt_publish COMMAND test_mqtt_publish)

target_compile_definitions(test_mqtt_publish PRIVATE UNIT_TEST=1 mqtt_publish_json=__mock_mqtt_publish_json)


# --- IMPORTANT: faire tourner les tests depuis la racine (pour que 'tests/data/...' existe) ---
set_tests_properties(
  test_table test_pack test_mqtt_filter test_mqtt_publish
  PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
