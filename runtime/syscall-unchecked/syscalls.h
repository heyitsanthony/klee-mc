#ifndef KLEE_SYSCALLS_H
#define KLEE_SYSCALLS_H

#include <stdint.h>

#define ARCH_SYS_MMAP2		0x1000

#ifdef GUEST_ARCH_AMD64
#include <valgrind/libvex_guest_amd64.h>
#define GET_SYSRET(x)	((VexGuestAMD64State*)x)->guest_RAX
#define GET_RET(x)	((VexGuestAMD64State*)x)->guest_RAX
#define GET_ARG0(x)	((VexGuestAMD64State*)x)->guest_RDI
#define GET_ARG1(x)	((VexGuestAMD64State*)x)->guest_RSI
#define GET_ARG2(x)	((VexGuestAMD64State*)x)->guest_RDX
#define GET_ARG3(x)	((VexGuestAMD64State*)x)->guest_R10
#define GET_ARG4(x)	((VexGuestAMD64State*)x)->guest_R8
#define GET_ARG5(x)	((VexGuestAMD64State*)x)->guest_R9
#define GET_STACK(x)	((VexGuestAMD64State*)x)->guest_RSP
#define GET_SYSNR(x)	((VexGuestAMD64State*)x)->guest_RAX
#define GET_FPRET(x)	*((double*)(&((VexGuestAMD64State*)(x))->guest_YMM0))
#define GET_PTREGS_IP(x)	(((uint64_t*)x)[16])
#define ARCH_SIGN_CAST	int64_t
#elif GUEST_ARCH_ARM
#include <valgrind/libvex_guest_arm.h>
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
#define GET_STACK(x)	((VexGuestARMState*)x)->guest_R13
#define GET_PTREGS_IP(x)	(((uint32_t*)x)[-1000])		/* XXX */
#define ARM_SYS_mmap2	ARCH_SYS_MMAP2
#define ARCH_SIGN_CAST	signed
#elif GUEST_ARCH_X86
#include <valgrind/libvex_guest_x86.h>
// %eax for syscall_number %ebx, %ecx, %edx, %esi, %edi, %ebp
#define GET_SYSRET(x)	((VexGuestX86State*)x)->guest_EAX
#define GET_ARG0(x)	((VexGuestX86State*)x)->guest_EBX
#define GET_ARG1(x)	((VexGuestX86State*)x)->guest_ECX
#define GET_ARG2(x)	((VexGuestX86State*)x)->guest_EDX
#define GET_ARG3(x)	((VexGuestX86State*)x)->guest_ESI
#define GET_ARG4(x)	((VexGuestX86State*)x)->guest_EDI
#define GET_ARG5(x)	((VexGuestX86State*)x)->guest_EBP
#define GET_SYSNR(x)	((VexGuestX86State*)x)->guest_EAX
#define GET_STACK(x)	((VexGuestX86State*)x)->guest_ESP
#define GET_PTREGS_IP(x)	(((uint32_t*)x)[-1000])
#define ARCH_SIGN_CAST	int32_t
#define X86_SYS_mmap2	ARCH_SYS_MMAP2
#else
#error UNKNOWN GUEST ARCH
#endif


#define ARCH_SYS_DEFAULT_LE0	0x999
#define ARCH_SYS_DEFAULT_EQ0	0x998
#define ARCH_SYS_UNSUPP		0x997
#define ARCH_SYS_SOCKETCALL	0x996
#define ARCH_SYS_WAITPID	0x995

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

#define sc_clear(x)	(x)->flags = 0
#define sc_is_32bit(x)	((x)->flags & 1)
#define sc_set_32bit(x)	(x)->flags |= 1

struct sc_pkt
{
	int	pure_sys_nr;	/* syscall number given to us by guest */
	int	sys_nr;		/* syscall we translate it to for host */
	void	*regfile;	/* guest's incoming register file */
	int	flags;
};


void syscall_xlate(struct sc_pkt* sc);

void* sc_new_regs(void* regfile);
void sc_ret_range(void* regfile, int64_t lo, int64_t hi);
uint64_t concretize_u64(uint64_t s);
void* concretize_ptr(void* ptr);
void sc_ret_v(void* regfile, uint64_t v1);
void sc_ret_or(void* regfile, uint64_t v1, uint64_t v2);
void sc_ret_v_new(void* regfile, uint64_t v1);
void make_sym_by_arg(
	void *regfile, uint64_t arg_num, uint64_t len, const char* name);
void make_sym(uint64_t addr, uint64_t len, const char* name);

/* ... */
void* kmc_sc_regs(void*);
void kmc_sc_bad(unsigned int);
void kmc_free_run(uint64_t addr, uint64_t num_bytes);
void kmc_exit(uint64_t);
void kmc_make_range_symbolic(uint64_t, uint64_t, const char*);
void* kmc_alloc_aligned(uint64_t, const char* name);

#endif
