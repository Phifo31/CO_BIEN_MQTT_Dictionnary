cmake_minimum_required(VERSION 3.15)
project(cobien_bridge C)

# Build type & warnings
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra -Wpedantic)

# Sanitizers activables en Debug/RelWithDebInfo
option(ENABLE_ASAN  "AddressSanitizer" ON)
option(ENABLE_UBSAN "UndefinedBehaviorSanitizer" ON)
if(CMAKE_BUILD_TYPE MATCHES "Debug|RelWithDebInfo")
  if(ENABLE_ASAN)
    add_compile_options(-fsanitize=address)
    add_link_options(-fsanitize=address)
  endif()
  if(ENABLE_UBSAN)
    add_compile_options(-fsanitize=undefined)
    add_link_options(-fsanitize=undefined)
  endif()
endif()

# Includes
include_directories(${CMAKE_SOURCE_DIR}/include)

# Trouver libmosquitto
find_library(MOSQUITTO_LIB mosquitto REQUIRED)
# Trouver cJSON (paquet libcjson-dev OU install source /usr/local)
find_library(CJSON_LIB cjson REQUIRED)

# Binaire principal
add_executable(cobien-bridge
  src/main.c
  src/pack.c
  src/table.c
  src/mqtt_io.c
  src/can_io.c
)
target_link_libraries(cobien-bridge
  ${MOSQUITTO_LIB}
  ${CJSON_LIB}
  pthread
)

enable_testing()
add_subdirectory(tests)
