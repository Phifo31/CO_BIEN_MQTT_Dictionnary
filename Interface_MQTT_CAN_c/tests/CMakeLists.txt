# Framework de test
find_package(PkgConfig REQUIRED)
pkg_check_modules(CMOCKA REQUIRED cmocka)
include_directories(${CMOCKA_INCLUDE_DIRS})

# Pour réutiliser les modules testés
include_directories(${CMAKE_SOURCE_DIR}/include)

# ----- test_table -----
add_executable(test_table tests/test_table.c ${CMAKE_SOURCE_DIR}/src/table.c)
target_link_libraries(test_table ${CMOCKA_LIBRARIES} ${CJSON_LIB})
add_test(NAME test_table COMMAND test_table)

# ----- test_pack -----
add_executable(test_pack tests/test_pack.c
  ${CMAKE_SOURCE_DIR}/src/table.c
  ${CMAKE_SOURCE_DIR}/src/pack.c
)
target_link_libraries(test_pack ${CMOCKA_LIBRARIES} ${CJSON_LIB})
add_test(NAME test_pack COMMAND test_pack)

# ----- test_mqtt_filter -----
# On veut tester la logique de filtrage MQTT -> CAN sans broker et sans CAN réel.
# On recompile mqtt_io.c mais on FOURNIT un faux can_send() pour capter l'appel.
add_executable(test_mqtt_filter
  tests/test_mqtt_filter.c
  ${CMAKE_SOURCE_DIR}/src/mqtt_io.c
  ${CMAKE_SOURCE_DIR}/src/table.c
  ${CMAKE_SOURCE_DIR}/src/pack.c
)
target_link_libraries(test_mqtt_filter ${CMOCKA_LIBRARIES} ${CJSON_LIB} ${MOSQUITTO_LIB})
# On force une macro UNIT_TEST pour exposer des helpers si besoin (facultatif)
target_compile_definitions(test_mqtt_filter PRIVATE UNIT_TEST=1)
add_test(NAME test_mqtt_filter COMMAND test_mqtt_filter)

# ----- test_mqtt_publish -----
# On veut vérifier que mqtt_on_can_message publie sur <base>/state.
# On FOURNIT un faux mqtt_publish_json() (spy).
add_executable(test_mqtt_publish
  tests/test_mqtt_publish.c
  ${CMAKE_SOURCE_DIR}/src/mqtt_io.c
  ${CMAKE_SOURCE_DIR}/src/table.c
  ${CMAKE_SOURCE_DIR}/src/pack.c
)
target_link_libraries(test_mqtt_publish ${CMOCKA_LIBRARIES} ${CJSON_LIB} ${MOSQUITTO_LIB})
target_compile_definitions(test_mqtt_publish PRIVATE UNIT_TEST=1)
add_test(NAME test_mqtt_publish COMMAND test_mqtt_publish)
