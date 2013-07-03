#include <stddef.h>
#include "klee/klee.h"



/* first argument must refer to an alloca instruction or bitcast ofalloca.
 * second contains a pointer to metadata associated with the pointer;
 * and must be a constant or global value address.
 * If your target collector uses tags, use a null pointer for metadata. */

void llvm_gcroot(void** a0, void* a1)
//{ klee_ureport("llvm_gcroot intrinsic is a stub!", "stub.err"); }
{
	klee_print_expr("llvm_gcroot instr", a0);
	klee_print_expr("llvm_gcroot meta", a1);
}



void* llvm_frameaddress(uint32_t v)
//{ klee_ureport("llvm_frameaddress intrinsic is a stub!", "stub.err"); }
{
	klee_print_expr("llvm_frameaddress", 0);
	return NULL;
}

