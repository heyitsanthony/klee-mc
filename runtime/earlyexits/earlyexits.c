/* functions that should cause an early exit in the executor */

#define GUEST_ARCH_AMD64
#include <stdint.h>
#include <stdlib.h>
#include "klee/klee.h"
#include "../syscall/syscalls.h"

void __hookpre___assert(void* regs)
{ klee_uerror((const char*)GET_ARG0(regs), "uassert.err"); }

void __hookpre___assert_fail(void* regs)
{ klee_uerror((const char*)GET_ARG0(regs), "uassert.err"); }

void __hookpre___GI___assert_fail(void* regs)
{ klee_uerror((const char*)GET_ARG0(regs), "uassert.err"); }


void __hookpre___GI_exit(void* regs) { exit(GET_ARG0(regs)); }

void __hookpre___stack_chk_fail(void* regs)
{ klee_uerror("???", "stackchk.err"); }

void __hookpre___fortify_fail(void* regs)
{ klee_uerror("???", "fortify.err"); }
