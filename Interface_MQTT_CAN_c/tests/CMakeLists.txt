# tests/CMakeLists.txt
cmake_minimum_required(VERSION 3.15)

find_package(PkgConfig REQUIRED)
pkg_check_modules(CMOCKA REQUIRED cmocka)
include_directories(${CMAKE_SOURCE_DIR}/include ${CMOCKA_INCLUDE_DIRS})

# ===== Options de build par test =====
option(ENABLE_TEST_TABLE        "Build test_table"        ON)
option(ENABLE_TEST_PACK         "Build test_pack"         ON)
option(ENABLE_TEST_MQTT_FILTER  "Build test_mqtt_filter"  ON)
option(ENABLE_TEST_MQTT_PUBLISH "Build test_mqtt_publish" ON)

# ===== Option CTest (désactivée par défaut) =====
option(REGISTER_CTESTS "Register tests with CTest" OFF)

# ---- test_table ----
if(ENABLE_TEST_TABLE)
  add_executable(test_table
    test_table.c
    ${CMAKE_SOURCE_DIR}/src/table.c
  )
  target_link_libraries(test_table ${CMOCKA_LIBRARIES} cjson)
  if(REGISTER_CTESTS)
    add_test(NAME test_table COMMAND test_table)
    set_tests_properties(test_table PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  endif()
endif()

# ---- test_pack ----
if(ENABLE_TEST_PACK)
  add_executable(test_pack
    test_pack.c
    ${CMAKE_SOURCE_DIR}/src/table.c
    ${CMAKE_SOURCE_DIR}/src/pack.c
  )
  target_link_libraries(test_pack ${CMOCKA_LIBRARIES} cjson)
  if(REGISTER_CTESTS)
    add_test(NAME test_pack COMMAND test_pack)
    set_tests_properties(test_pack PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  endif()
endif()

# ---- test_mqtt_filter ----
if(ENABLE_TEST_MQTT_FILTER)
  add_executable(test_mqtt_filter
    test_mqtt_filter.c
    ${CMAKE_SOURCE_DIR}/src/mqtt_io.c
    ${CMAKE_SOURCE_DIR}/src/table.c
    ${CMAKE_SOURCE_DIR}/src/pack.c
  )
  target_link_libraries(test_mqtt_filter ${CMOCKA_LIBRARIES} cjson mosquitto)
  target_compile_definitions(test_mqtt_filter PRIVATE UNIT_TEST=1)
  if(REGISTER_CTESTS)
    add_test(NAME test_mqtt_filter COMMAND test_mqtt_filter)
    set_tests_properties(test_mqtt_filter PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  endif()
endif()

# ---- test_mqtt_publish (avec HOOK, sans --wrap) ----
if(ENABLE_TEST_MQTT_PUBLISH)
  add_executable(test_mqtt_publish
    test_mqtt_publish.c
    ${CMAKE_SOURCE_DIR}/src/mqtt_io.c
    ${CMAKE_SOURCE_DIR}/src/table.c
    ${CMAKE_SOURCE_DIR}/src/pack.c
  )
  target_link_libraries(test_mqtt_publish ${CMOCKA_LIBRARIES} cjson mosquitto)
  target_compile_definitions(test_mqtt_publish PRIVATE UNIT_TEST=1)
  # NOTE: plus de target_link_options(... --wrap=...) ; on injecte mqtt_publish_hook côté test
  if(REGISTER_CTESTS)
    add_test(NAME test_mqtt_publish COMMAND test_mqtt_publish)
    set_tests_properties(test_mqtt_publish PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  endif()
endif()

# ---- Runner (toujours compilé) ----
add_executable(test_runner test_runner.c)

# S’assurer que les binaires existent avant le runner (facilite le 1er lancement)
if(ENABLE_TEST_TABLE)
  add_dependencies(test_runner test_table)
endif()
if(ENABLE_TEST_PACK)
  add_dependencies(test_runner test_pack)
endif()
if(ENABLE_TEST_MQTT_FILTER)
  add_dependencies(test_runner test_mqtt_filter)
endif()
if(ENABLE_TEST_MQTT_PUBLISH)
  add_dependencies(test_runner test_mqtt_publish)
endif()
