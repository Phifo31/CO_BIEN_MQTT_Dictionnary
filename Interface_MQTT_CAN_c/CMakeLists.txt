cmake_minimum_required(VERSION 3.15)
project(cobien_bridge C)

# Build type & warnings
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_compile_options(-Wall -Wextra -Wpedantic)
# pour nanosleep/usleep & co
add_compile_definitions(_POSIX_C_SOURCE=200809L)

# Sanitizers (Debug/RelWithDebInfo)
option(ENABLE_ASAN  "AddressSanitizer" ON)
option(ENABLE_UBSAN "UndefinedBehaviorSanitizer" ON)
if(CMAKE_BUILD_TYPE MATCHES "Debug|RelWithDebInfo")
  if(ENABLE_ASAN)
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
  endif()
  if(ENABLE_UBSAN)
    add_compile_options(-fsanitize=undefined)
    add_link_options(-fsanitize=undefined)
  endif()
endif()

# Détection via pkg-config
find_package(PkgConfig REQUIRED)
pkg_check_modules(MOSQ  REQUIRED libmosquitto)
pkg_check_modules(CJSON REQUIRED libcjson)

# Binaire
add_executable(cobien-bridge
  src/main.c
  src/bridge_app.c
  src/pack.c
  src/table.c
  src/mqtt_io.c
  src/can_io.c
)

# Include dirs (projet + libs)
target_include_directories(cobien-bridge PRIVATE
  ${CMAKE_SOURCE_DIR}/include
  ${MOSQ_INCLUDE_DIRS}
  ${CJSON_INCLUDE_DIRS}
)

# Link dirs (si pkg-config ne fournit pas des chemins absolus)
if(MOSQ_LIBRARY_DIRS)
  target_link_directories(cobien-bridge PRIVATE ${MOSQ_LIBRARY_DIRS})
endif()
if(CJSON_LIBRARY_DIRS)
  target_link_directories(cobien-bridge PRIVATE ${CJSON_LIBRARY_DIRS})
endif()

# Link libs
target_link_libraries(cobien-bridge
  ${MOSQ_LIBRARIES}
  ${CJSON_LIBRARIES}
  pthread
)

# (Optionnel) Chemin absolu de fallback compilé en dur :
# cmake -DDEFAULT_CFG_PATH="/chemin/vers/config/conversion.json" ...
if(DEFINED DEFAULT_CFG_PATH)
  target_compile_definitions(cobien-bridge PRIVATE DEFAULT_CFG_PATH="${DEFAULT_CFG_PATH}")
endif()

# Tests (désactivés pour l’instant)
# enable_testing()
# add_subdirectory(tests)


