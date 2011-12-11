#ifndef KLEE_SYSCALLS_H
#define KLEE_SYSCALLS_H

#include <valgrind/libvex_guest_amd64.h>
#include <valgrind/libvex_guest_arm.h>

#include <stdint.h>

#ifdef GUEST_ARCH_AMD64
#define GET_SYSRET(x)	((VexGuestAMD64State*)x)->guest_RAX
#define GET_ARG0(x)	((VexGuestAMD64State*)x)->guest_RDI
#define GET_ARG1(x)	((VexGuestAMD64State*)x)->guest_RSI
#define GET_ARG2(x)	((VexGuestAMD64State*)x)->guest_RDX
#define GET_ARG3(x)	((VexGuestAMD64State*)x)->guest_R10
#define GET_ARG4(x)	((VexGuestAMD64State*)x)->guest_R8
#define GET_ARG5(x)	((VexGuestAMD64State*)x)->guest_R9
#define GET_SYSNR(x)	((VexGuestAMD64State*)x)->guest_RAX
#define ARCH_SIGN_CAST	intptr_t
#elif GUEST_ARCH_ARM
#define GET_SYSRET(x)	((VexGuestARMState*)x)->guest_R0
/* XXX: There is some silliness when passing 64-bit parameters.
 * Should be fine though.. */
#define GET_ARG0(x)	((VexGuestARMState*)x)->guest_R0
#define GET_ARG1(x)	((VexGuestARMState*)x)->guest_R1
#define GET_ARG2(x)	((VexGuestARMState*)x)->guest_R2
#define GET_ARG3(x)	((VexGuestARMState*)x)->guest_R3
#define GET_ARG4(x)	((VexGuestARMState*)x)->guest_R4
#define GET_ARG5(x)	((VexGuestARMState*)x)->guest_R5
#define GET_SYSNR(x)	((VexGuestARMState*)x)->guest_R7	/* ARM EABI */
#define ARM_SYS_mmap2	0x1000
#define ARCH_SIGN_CAST	signed
#else
#error UNKNOWN GUEST ARCH
#endif

#define GET_SYSRET_S(x)	((ARCH_SIGN_CAST)GET_SYSRET(x))
#define GET_ARG0_S(x)	((ARCH_SIGN_CAST)GET_ARG0(x))
#define GET_ARG1_S(x)	((ARCH_SIGN_CAST)GET_ARG1(x))
#define GET_ARG2_S(x)	((ARCH_SIGN_CAST)GET_ARG2(x))
#define GET_ARG3_S(x)	((ARCH_SIGN_CAST)GET_ARG3(x))
#define GET_ARG4_S(x)	((ARCH_SIGN_CAST)GET_ARG4(x))

#define GET_ARG0_PTR(x)	(void*)((uintptr_t)GET_ARG0(x))
#define GET_ARG1_PTR(x)	(void*)((uintptr_t)GET_ARG1(x))
#define GET_ARG2_PTR(x)	(void*)((uintptr_t)GET_ARG2(x))
#define GET_ARG3_PTR(x)	(void*)((uintptr_t)GET_ARG3(x))
#define GET_ARG4_PTR(x)	(void*)((uintptr_t)GET_ARG4(x))
#define GET_ARG5_PTR(x)	(void*)((uintptr_t)GET_ARG5(x))


int syscall_xlate(unsigned int sys_nr);

void* sc_new_regs(void* regfile);
void sc_ret_range(void* regfile, int64_t lo, int64_t hi);
uint64_t concretize_u64(uint64_t s);
void sc_ret_v(void* regfile, uint64_t v1);
void make_sym_by_arg(
	void *regfile, uint64_t arg_num, uint64_t len, const char* name);
void make_sym(uint64_t addr, uint64_t len, const char* name);

#endif
