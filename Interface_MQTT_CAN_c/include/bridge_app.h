#pragma once
#include <stdbool.h>

bool my_setup(void);   // charge table, init CAN/MQTT, s'abonne
bool my_loop(void);    // pompe MQTT & CAN en non-bloquant; false => arrêter
void my_shutdown(void);// cleanup
