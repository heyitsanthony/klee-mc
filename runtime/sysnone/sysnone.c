#include "klee/klee.h"

void* sc_enter(void* x) { klee_uerror("System call", "sysnone.err"); }