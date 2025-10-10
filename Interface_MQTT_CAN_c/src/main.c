#include "bridge_app.h"
int main(void){
  if(!my_setup()) return 1;
  while(my_loop()) { /* rien */ }
  my_shutdown();
  return 0;
}
