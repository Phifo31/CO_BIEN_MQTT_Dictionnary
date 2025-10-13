#include "bridge_app.h"
#include <stddef.h>

int main(int argc, char **argv){
  const char *cfg = (argc > 1) ? argv[1] : NULL;
  if (!my_setup(cfg)) return 1;
  while (my_loop()) {}
  my_shutdown();
  return 0;
}



