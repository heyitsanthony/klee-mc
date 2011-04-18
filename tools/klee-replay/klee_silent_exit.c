#include <stdlib.h>

void klee_silent_exit(int status) {
  exit(status);
}
