#include "bridge_app.h"
#include <stddef.h>

int main(void){
  if (!my_setup()) return 1;
  while(my_loop()) { /* loop */ }
  my_shutdown();
  return 0;
}
