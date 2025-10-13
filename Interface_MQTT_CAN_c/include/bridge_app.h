#pragma once
#include <stdbool.h>

bool my_setup(const char *cfg_path_opt);  // <- accepte un chemin optionnel
bool my_loop(void);
void my_shutdown(void);
