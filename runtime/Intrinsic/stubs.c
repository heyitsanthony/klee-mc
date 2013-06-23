#include <stddef.h>
#include "klee/klee.h"

void llvm_gcroot(void** a0, void* a1)
{ klee_ureport("llvm_gcroot intrinsic is a stub!", "stub.err"); }

void* llvm_frameaddress(uint32_t v)
{ klee_ureport("llvm_frameaddress intrinsic is a stub!", "stub.err"); }

