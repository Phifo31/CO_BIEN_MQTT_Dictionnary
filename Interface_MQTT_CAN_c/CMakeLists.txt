cmake_minimum_required(VERSION 3.15)
project(cobien_bridge C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra -Wpedantic -O2)

# libmosquitto (sudo apt install libmosquitto-dev)
find_library(MOSQUITTO_LIB mosquitto)
if (NOT MOSQUITTO_LIB)
  message(FATAL_ERROR "libmosquitto introuvable. Installe libmosquitto-dev.")
endif()

# Option backend CAN simulé (FIFOs)
option(FAKE_CAN "Use FIFO fake CAN backend instead of SocketCAN" OFF)

include_directories(${CMAKE_SOURCE_DIR}/include)

add_executable(cobien-bridge
  src/main.c
  src/pack.c
  src/table.c
  src/mqtt_io.c
  src/can_io.c
)

# Passe le define au code si tu fais: cmake -DFAKE_CAN=ON ..
if(FAKE_CAN)
  target_compile_definitions(cobien-bridge PRIVATE FAKE_CAN=1)
endif()

# Liens
# - mosquitto (MQTT)
# - cjson (sudo apt install libcjson-dev)
# - pthread (threads)
target_link_libraries(cobien-bridge
  ${MOSQUITTO_LIB}
  cjson
  pthread
)

# Installation simple
install(TARGETS cobien-bridge RUNTIME DESTINATION bin)
install(FILES config/conversion.json DESTINATION /etc/cobien/)
