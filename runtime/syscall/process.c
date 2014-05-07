#include <sys/syscall.h>
#include <linux/futex.h>
#include "syscalls.h"
#include "klee/klee.h"
#include <sched.h>

static int *set_child_tid = NULL, *clear_child_tid = NULL;
static struct robust_list_head** rlh;
static size_t			rlh_len;
static int			free_pid = 1000;

//   long clone(
//   		unsigned long flags,	/* arg0 */
//   		void *child_stack,	/* arg1 */
// 		void *ptid,		/* arg2 */
// 		void *ctid,		/* arg3 */
//              struct pt_regs *regs	/* arg4 */);
static void do_clone(struct sc_pkt* sc)
{
	void	*new_regs = NULL;
	uint32_t orig_flags = GET_ARG0(sc->regfile), flags;
	void	*child_stack = GET_ARG1_PTR(sc->regfile);
	void	*ptid = GET_ARG2_PTR(sc->regfile);
	void	*ctid = GET_ARG3_PTR(sc->regfile);
	void	*regs = GET_ARG4_PTR(sc->regfile);
	int	child_pid;
	int	exit_sig;

	klee_print_expr("DOING CLONE!!!", 123);
	klee_print_expr("CLONE ARG0", GET_ARG0(sc->regfile));
	klee_print_expr("CLONE ARG1", GET_ARG1(sc->regfile));
	klee_print_expr("CLONE ARG2", GET_ARG2(sc->regfile));
	klee_print_expr("CLONE ARG3", GET_ARG3(sc->regfile));
	klee_print_expr("CLONE ARG4", GET_ARG4(sc->regfile));

	flags = orig_flags;
	exit_sig = flags & CSIGNAL;
	flags &= ~CSIGNAL;

	if (flags & CLONE_CHILD_CLEARTID) {
		/* register exit futex and memory location to clear */
		flags &= ~CLONE_CHILD_CLEARTID;
	}

	if (flags & CLONE_CHILD_SETTID)
		flags &= ~CLONE_CHILD_SETTID;

	new_regs = sc_new_regs(sc->regfile);
	if (flags == 0) {
		klee_ureport("Unhandled clone flags", "clone.err");
		klee_assume_eq(GET_SYSRET(new_regs), -1);
		return;
	}

	if (child_stack == NULL) {
		klee_ureport ("Unhandled new child stack", "clone.err");
		klee_assume_eq(GET_SYSRET(new_regs), -1);
		return;
	}

	if (ptid == NULL) {
		klee_ureport ("Unhandled ptid", "clone.err");
		klee_assume_eq(GET_SYSRET(new_regs), -1);
		return;
	}

	/* failure case */
	if (GET_SYSRET_S(new_regs) == -1) return;

	/* successful parent case */
	child_pid = free_pid++;
	if (GET_SYSRET(new_regs) == child_pid) return;

	if (regs == NULL) {
		klee_ureport("can't jump to child's pt_regs yet", "clone.err");
		klee_assume_eq(GET_SYSRET(new_regs), -1);
	}

	/* child case */
	klee_assume_eq(GET_SYSRET(new_regs), 0);
	if (orig_flags & CLONE_CHILD_CLEARTID) {
		clear_child_tid = ctid;
	}

	if (orig_flags & CLONE_CHILD_SETTID) {
		/* store TID in userlelvel buffer in the child */
		if (ctid == NULL) {
			klee_ureport(
				"no ctid given but CHILD_SETTID set",
				"clone.err");
		} else {
			*((int*)ctid) = child_pid;
		}
		flags &= ~CLONE_CHILD_SETTID;
	}

/* I have no idea what I'm doing. Ugh. */
#if 0
	if (GET_SYSRET(new_regs) == 0) {
		unsigned i = 0;
		jmpptr = GET_PTREGS_IP(GET_ARG5_PTR(sc->regfile));
		for (i = 0; i < 32; i++) {
		klee_print_expr("i", i);
		klee_print_expr("WUT[i]",
			((uint64_t*)GET_ARG5_PTR(sc->regfile))[i]);

		}
		klee_print_expr("CHILD CLONE TO", jmpptr);
	}
#endif
}

void proc_sc(struct sc_pkt* sc)
{
	void	*new_regs = NULL;
	switch (sc->sys_nr) {
	case SYS_clone:
		do_clone(sc);
		break;

	case SYS_set_robust_list:
		rlh = GET_ARG0(sc->regfile);
		rlh_len = GET_ARG1(sc->regfile);
		sc_ret_v(sc->regfile, 0);
		break;
	case SYS_set_tid_address:
		clear_child_tid = GET_ARG0_PTR(sc->regfile);
		sc_ret_v(sc->regfile,
			1000 /* XXX: should be PID of calling process */);
		break;

	case SYS_vfork:
	case SYS_fork:
		klee_print_expr("forking cur free_pid", free_pid);
		new_regs = sc_new_regs(sc->regfile);
		/* this will *always* fork into two states, so no need
		 * to make it symbolic first */
		if (GET_SYSRET(new_regs) == 0) {
			klee_print_expr("fork child", 0);
			break;	/* child */
		}

		/* child's PID, passed to parent */
		klee_print_expr("fork parent", free_pid);
		sc_ret_v_new(new_regs, free_pid++);
		break;
	}
}
