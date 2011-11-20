#ifndef KLEE_SYSCALLS_H
#define KLEE_SYSCALLS_H

#include <valgrind/libvex_guest_amd64.h>
#include <stdint.h>

#define GET_RAX(x)	((VexGuestAMD64State*)x)->guest_RAX
#define GET_ARG0(x)	((VexGuestAMD64State*)x)->guest_RDI
#define GET_ARG1(x)	((VexGuestAMD64State*)x)->guest_RSI
#define GET_ARG2(x)	((VexGuestAMD64State*)x)->guest_RDX
#define GET_ARG3(x)	((VexGuestAMD64State*)x)->guest_R10
#define GET_ARG4(x)	((VexGuestAMD64State*)x)->guest_R8
#define GET_ARG5(x)	((VexGuestAMD64State*)x)->guest_R9
#define GET_SYSNR(x)	GET_RAX(x)

void* sc_new_regs(void* regfile);
void sc_ret_range(void* regfile, int64_t lo, int64_t hi);
uint64_t concretize_u64(uint64_t s);
void sc_ret_v(void* regfile, uint64_t v1);
void make_sym_by_arg(
	void *regfile, uint64_t arg_num, uint64_t len, const char* name);
void make_sym(uint64_t addr, uint64_t len, const char* name);

#endif
