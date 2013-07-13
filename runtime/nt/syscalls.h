#ifndef SYSCALLS_H
#define SYSCALLS_H

#include <valgrind/libvex_guest_x86.h>

#define GET_SYSNR(x)	((VexGuestX86State*)x)->guest_EAX
#define GET_SYSRET(x)	((VexGuestX86State*)x)->guest_EAX
#define GET_ARGN(x,n)	((uint32_t*)((uintptr_t)(((VexGuestX86State*)x)->guest_EDX)))[n]
#define GET_ARGN_PTR(x,n) ((void*)((uintptr_t)GET_ARGN(x,n)))

#define GET_ARG0(x)	GET_ARGN(x,0)
#define GET_ARG1(x)	GET_ARGN(x,1)
#define GET_ARG2(x)	GET_ARGN(x,2)
#define GET_ARG3(x)	GET_ARGN(x,3)
#define GET_ARG4(x)	GET_ARGN(x,4)
#define GET_ARG5(x)	GET_ARGN(x,5)
#define GET_ARG6(x)	GET_ARGN(x,6)

#define GET_ARG0_PTR(x)	GET_ARGN_PTR(x,0)
#define GET_ARG1_PTR(x)	GET_ARGN_PTR(x,1)
#define GET_ARG2_PTR(x)	GET_ARGN_PTR(x,2)
#define GET_ARG3_PTR(x)	GET_ARGN_PTR(x,3)
#define GET_ARG4_PTR(x)	GET_ARGN_PTR(x,4)
#define GET_ARG5_PTR(x)	GET_ARGN_PTR(x,5)
#define GET_ARG6_PTR(x)	GET_ARGN_PTR(x,6)

// clang says 362, snapshots say 361. UGH.
//#define ARCH_SZ		sizeof(VexGuestX86State)
#define ARCH_SZ		361	/* UGH!!! */
#define GET_EDX(x)	((VexGuestX86State*)x)->guest_EDX
#define GET_ECX(x)	((VexGuestX86State*)x)->guest_ECX
#define GET_ESP(x)	((VexGuestX86State*)x)->guest_ESP
#define GET_SC_IP(x)	((VexGuestX86State*)x)->guest_IP_AT_SYSCALL
#define GE_SYSCALL	5

struct sc_pkt
{
	int	pure_sys_nr;	/* syscall number given to us by guest */
	int	sys_nr;		/* syscall we translate it to for host */
	void	*regfile;	/* guest's incoming register file */
	int	flags;
};
#define sc_clear(x)	(x)->flags = 0
#define sc_is_32bit(x)	((x)->flags & 1)
#define sc_set_32bit(x)	(x)->flags |= 1


void make_sym(void* _addr, uint64_t len, const char* name);
uint64_t concretize_u64(uint64_t s);

#endif

