#define _LARGEFILE64_SOURCE

#include <string.h>

#ifdef GUEST_WINXP
#include "win32k_xp.h"
#include "ntapi.xp.h"
#elif defined(GUEST_WIN7)
#include "win32k_win7.h"
#include "ntapi.win7.h"
#else
#error wut
#endif

#include "ntkey.h"
#include <klee/klee.h>
#include "fnid.h"

#include "syscalls.h"
#include "breadcrumb.h"

//#define USE_SYS_FAILURE

static int last_sc = 0;

/* PLATFORM DATA */
/* XXX I think this should use klee registers but whatever */
char		plat_pbi[24];
uint32_t	plat_cookie;
uint32_t	plat_version;

#define is_xp()	((plat_version & 0xffff)== 0x0105)

#include "mem.h"
//#include <wtypes.h>

void* sc_new_regs(void* r)
{
	void	*ret = kmc_sc_regs(r);
	SC_BREADCRUMB_FL_OR(BC_FL_SC_NEWREGS);
	return ret;
}

void sc_ret_v(void* regfile, uint64_t v1)
{
//	klee_assume_eq(GET_SYSRET_S(regfile), (ARCH_SIGN_CAST)v1);
	GET_SYSRET(regfile) = v1;
}

void sc_ret_v_new(void* regfile, uint64_t v1)
{
	klee_assume_eq(GET_SYSRET(regfile), v1);
	GET_SYSRET(regfile) = v1;
}

#define SYM_YIELD_SIZE (16*1024)
uint64_t concretize_u64(uint64_t s)
{
	uint64_t sc = klee_get_value(s);
	klee_assume_eq(sc, s);
	return sc;
}

void make_sym(void* _addr, uint64_t len, const char* name)
{
	uint64_t addr;
	klee_check_memory_access(_addr, 1);

	addr = concretize_u64((uint64_t)_addr);
	if (len > SYM_YIELD_SIZE)
		klee_yield();

	if (len == 0)
		return;

	len = concretize_u64(len);
	kmc_make_range_symbolic(addr, len, name);
	sc_breadcrumb_add_ptr((void*)addr, len);

}
#define SC_MAX_LOOP	10
#define SC_MAX_CALL	30
struct loop_info {
	int	sc;
	int	consec;
	int	total;
};
#define INIT_LOOP_INFO(x) { .sc = x, .consec = 0, .total = 0}

static void loop_protect(struct loop_info* li)
{
	if (last_sc != li->sc)
		li->consec = 0;

	li->total++;
	li->consec++;
	if (li->consec < SC_MAX_LOOP && li->total < SC_MAX_CALL)
		return;

	klee_uerror("Called same syscall too many times", "loop.err");
}

#define BEGIN_PROTECT_CASE(x) case Nt##x: { \
	static struct loop_info x##_c = INIT_LOOP_INFO(Nt##x); \
	loop_protect(&x##_c);

#define END_PROTECT_CASE break; }

#define DEFAULT_SC(x)	case x: new_regs = sc_new_regs(regfile); break;
#define DEFAULT_SC_PROTECT(x)	\
	case Nt##x:  {		\
		static struct loop_info x##_c = INIT_LOOP_INFO(Nt##x); \
		loop_protect(&x##_c);\
		new_regs = sc_new_regs(regfile);	\
		break;	\
	}


#define VOID_SC(x)	case x: break;

#define DEFAULT_HANDLE_SC_N(x,n)	\
	case Nt##x:				\
	new_regs = sc_new_regs(regfile);	\
	if (GET_SYSRET(new_regs) != 0) break;	\
	make_sym(GET_ARGN_PTR(regfile, n), 4, "Nt" #x);	\
	break;

#define DEFAULT_HANDLE_SC(x)	DEFAULT_HANDLE_SC_N(x,0)

#define WRITE_N_TO_ARG(a,n,y) case Nt##a:	\
	WRITE_N_TO_ARG_BOTTOM(a,n,y)
#define WRITE_N_TO_ARG_BOTTOM(a,n,y)	\
		new_regs = sc_new_regs(regfile);	\
		if (GET_ARGN_PTR(regfile, y) != NULL)	\
			make_sym(GET_ARGN_PTR(regfile, y), n, "Nt" #a);	\
		break;

#define WRITE_N_TO_ARG_PROTECT(a,n,y)	\
	BEGIN_PROTECT_CASE(a)		\
	WRITE_N_TO_ARG_BOTTOM(a,n,y)	\
	END_PROTECT_CASE

#define UNIMPL_SC(n) case n:	\
if (is_sym_sc)	\
	klee_uerror(	\
		"Unimplemented symbolically resolved syscall "#n,	\
		"symsc.err");	\
else	\
	klee_uerror("Unimplemented call "#n, "sc.err");\
break;

static int is_sym_sc;

void* sc_enter(void* regfile, void* jmpptr)
{
	struct sc_pkt		sc;
	void			*new_regs = NULL;

	sc_clear(&sc);
	sc.regfile = regfile;
	sc.pure_sys_nr = GET_SYSNR(regfile);

	if (((char*)regfile)[ARCH_SZ-1] == GE_SYSCALL) {
		GET_EDX(regfile) += 8;
		jmpptr = (void*)(uintptr_t)(GET_SC_IP(regfile) + 2);
		GET_IP(regfile) = (uint32_t)jmpptr;
	}

	if (klee_is_symbolic(sc.pure_sys_nr)) {
		klee_warning_once("Resolving symbolic syscall nr");
		klee_print_expr("Symbolic syscall", sc.pure_sys_nr);
		klee_uerror(
			"Symbolically resolved syscall! No way!",
			"symsc.err");
		is_sym_sc = 1;
		sc.pure_sys_nr = klee_fork_all_n(sc.pure_sys_nr, 4);
	} else
		is_sym_sc = 0;

	sc.sys_nr = sc.pure_sys_nr;
	sc_breadcrumb_reset();

	klee_print_expr("cur call", sc.pure_sys_nr);
	switch (sc.sys_nr) {
	/* nt */
	case NtAcceptConnectPort:
		new_regs = sc_new_regs(regfile);
		if (GET_ARG0_PTR(regfile))
			make_sym(
				GET_ARG0_PTR(regfile),
				4,
				"NtAcceptConnectPort.h");
		if (GET_ARG4_PTR(regfile))
//			LPC_SECTION_OWNER_MEMORY
			klee_uerror("NtACP SECTION_OWNER_MEMORY", "sc.err");

		if (GET_ARG5_PTR(regfile))
			klee_uerror("NtACP SECTION_MEMORY", "sc.err");
//			LPC_SECTION_MEMORY
		break;

	DEFAULT_SC(NtCompleteConnectPort)


	DEFAULT_HANDLE_SC(CreateEvent)
	DEFAULT_HANDLE_SC(CreateSection)

	DEFAULT_SC(NtReleaseKeyedEvent)
	DEFAULT_HANDLE_SC(OpenSection)

	WRITE_N_TO_ARG(OpenEvent, 4, 0)
	WRITE_N_TO_ARG(OpenProcess, 4, 0)
	WRITE_N_TO_ARG(QuerySystemTime, 8, 1)
	WRITE_N_TO_ARG(OpenProcessToken, 4, 2)
	WRITE_N_TO_ARG(OpenThreadToken, 4, 3)
	WRITE_N_TO_ARG(DuplicateObject, 4, 3)

	DEFAULT_SC(NtSetEventBoostPriority)
	DEFAULT_SC(NtTerminateJobObject)

	case NtQueryEvent:
		new_regs = sc_new_regs(regfile);
		if (GET_ARG1(regfile) != 0) {
			klee_uerror(
				"NtQueryEvent: unknown EVENT_INFORMATION_CLASS",
				"sc.err");
			break;
		}

		if (GET_ARG2_PTR(regfile) == 0) break;
		make_sym(GET_ARG2_PTR(regfile),
			GET_ARG3(regfile),
			"NtQueryEvent.ei");

		if (GET_ARG4_PTR(regfile) == 0) break;

		make_sym(GET_ARG4_PTR(regfile), 4, "NtQueryEvent.retlen");
		klee_assume_ule(
			*((uint32_t*)GET_ARG4_PTR(regfile)),
			GET_ARG3(regfile));

		break;

	case NtQueryPerformanceCounter:
		new_regs = sc_new_regs(regfile);
		if (GET_ARG0_PTR(regfile))
			make_sym(GET_ARG0_PTR(regfile), 8, "NtQPC.pc");
		if (GET_ARG1_PTR(regfile))
			make_sym(GET_ARG1_PTR(regfile), 8, "NtQPC.freq");
		break;


	WRITE_N_TO_ARG(QueryInformationFile, GET_ARG3(regfile), 2)

	/* this should probably have logic to exit if handle==NtCurrentThread() */
	DEFAULT_SC(NtTerminateThread)

	case NtTerminateProcess:
		kmc_exit(GET_ARG1(regfile));
		break;

	case NtRaiseException:
		klee_warning_once("[NT] terminating on raise exception");
		kmc_exit(GET_ARG0(regfile));
		break;

	case NtFindAtom:
		new_regs = sc_new_regs(regfile);
		if (GET_SYSRET(new_regs) == 0 && GET_ARG2_PTR(regfile))
			make_sym(GET_ARG2_PTR(regfile), 2, "NtFindAtom");
		break;

	DEFAULT_SC(NtSetInformationObject);
	DEFAULT_SC(NtSetInformationThread);
	WRITE_N_TO_ARG(SetInformationFile, 8, 1)

	DEFAULT_HANDLE_SC(OpenKey)

	DEFAULT_HANDLE_SC_N(OpenThreadTokenEx, 4)

	WRITE_N_TO_ARG(QueryDefaultUILanguage, 2, 0)
	WRITE_N_TO_ARG(OpenProcessTokenEx, 4, 3)

	/* XXX: ignores io status block in arg3 */
	WRITE_N_TO_ARG(OpenFile, 4, 0)
	/* XXX: ignores io status block */
	WRITE_N_TO_ARG(CreateFile, 4, 0)

	case NtQueryInformationThread:
		new_regs = sc_new_regs(regfile);
		if (GET_ARG2_PTR(regfile))
			make_sym(
				GET_ARG2_PTR(regfile),
				GET_ARG3(regfile),
				"NtQueryInformationThread");

		if (GET_ARG4_PTR(regfile)) {
			make_sym(
				GET_ARG4_PTR(regfile),
				4,
				"NtQueryInformationThread.len");
			klee_assume_ule(
				*((uint32_t*)GET_ARG2_PTR(regfile)),
				GET_ARG3(regfile));
		}


		break;


	case NtNotifyChangeDirectoryFile:
		new_regs = sc_new_regs(regfile);
		if (GET_ARG4_PTR(regfile))
			make_sym(
				GET_ARG4_PTR(regfile),
				8,
				"NtNotifyChangeDirectoryFiles.io");
		if (GET_ARG5_PTR(regfile)) {
			make_sym(
				GET_ARG5_PTR(regfile),
				GET_ARG6(regfile),
				"NtNotifyChangeDirectoryFiles.buf");
		}
		break;

	WRITE_N_TO_ARG(NotifyChangeKey, 7, GET_ARGN(regfile, 8))

	case NtRemoveIoCompletion:
		new_regs = sc_new_regs(regfile);
		if (GET_ARG1_PTR(regfile))
			make_sym(GET_ARG1_PTR(regfile),
				4,
				"NtRemoveIoCompletion.key");

		if (GET_ARG2_PTR(regfile))
			make_sym(GET_ARG2_PTR(regfile),
				4,
				"NtRemoveIoCompletion.val");

		if (GET_ARG3_PTR(regfile))
			make_sym(GET_ARG3_PTR(regfile),
				8,
				"NtRemoveIoCompletion.io");
		break;

	WRITE_N_TO_ARG(CreateMutant, 4, 0)

	DEFAULT_SC(NtQueueApcThread)


	case NtQueryDirectoryFile:
		new_regs = sc_new_regs(regfile);
		if (GET_ARG4_PTR(regfile))
			make_sym(
				GET_ARG4_PTR(regfile),
				8,
				"NtQueryDirectoryFile.io");
		if (GET_ARG5_PTR(regfile))
			make_sym(
				GET_ARG5_PTR(regfile),
				GET_ARG6(regfile),
				"NtQueryDirectoryFile.info");
		break;

	BEGIN_PROTECT_CASE(ReplyWaitReceivePort)
		new_regs = sc_new_regs(regfile);
		if (GET_ARG1_PTR(regfile))
			make_sym(
				GET_ARG1_PTR(regfile),
				4,
				"NtReplyWaitReceivePort.h");
		if (GET_ARG3_PTR(regfile))
			make_sym(
				GET_ARG3_PTR(regfile),
				0x18,
				"NtReplyWaitReceivePort.lpc");
	END_PROTECT_CASE

	WRITE_N_TO_ARG_PROTECT(ReplyWaitReplyPort , 0x18, 1)

	BEGIN_PROTECT_CASE(ReplyWaitReceivePortEx)
		new_regs = sc_new_regs(regfile);
		if (GET_ARG1_PTR(regfile))
			make_sym(
				GET_ARG1_PTR(regfile),
				4,
				"NtReplyWaitReceivePortEx.h");
		if (GET_ARG3_PTR(regfile))
			make_sym(
				GET_ARG3_PTR(regfile),
				5*4 /* PORT_MESSAGE */,
				"NtReplyWaitReceivePortEx.port");
	END_PROTECT_CASE

	case NtQueryValueKey:
		new_regs = sc_new_regs(regfile);
		if (GET_ARG3_PTR(new_regs) == NULL) break;
		if (GET_SYSRET(new_regs) != 0) break;

		make_sym(GET_ARG3_PTR(regfile),
			GET_ARG4(regfile),
			"NtQueryValueKey.info");
		if (GET_ARG5_PTR(regfile)) {
			make_sym(GET_ARG5_PTR(regfile), 4, "NtQueryValueKey.sz");
			klee_assume_ule(
				*((uint32_t*)GET_ARG5_PTR(regfile)),
				GET_ARG4(regfile));
		}
		break;

	case NtQueryInformationProcess: {
		unsigned out_sz = 0;

		klee_print_expr("[NT] QUERY PROC INFO",  GET_ARG1(regfile));

		new_regs = sc_new_regs(regfile);
//		if (GET_SYSRET(new_regs) != 0) break;
		klee_assume_eq(GET_SYSRET(new_regs), 0);

		klee_print_expr("[NTQIP] Size", GET_ARG3(regfile));

		/* PROCESSBASICINFORMATION */
		switch (GET_ARG1(regfile)) {
		case 0: /* ProcessBasicInformation */

		memcpy(GET_ARG2_PTR(regfile), plat_pbi, sizeof(plat_pbi));
		out_sz = sizeof(plat_pbi);
		klee_print_expr("pbi[0]", ((uint32_t*)plat_pbi)[0]);
		klee_print_expr("pbi[1]", ((uint32_t*)plat_pbi)[1]);
		klee_print_expr("pbi[2]", ((uint32_t*)plat_pbi)[2]);
		klee_print_expr("pbi[3]", ((uint32_t*)plat_pbi)[3]);
		klee_print_expr("pbi[4]", ((uint32_t*)plat_pbi)[4]);
		break;
		case 36: /* ProcessCookie */
			*((uint32_t*)GET_ARG2_PTR(regfile)) = plat_cookie;
			out_sz = 4;
			break;

		default:
			klee_print_expr(
				"[NT] Weird ProcessInfoClass",
				GET_ARG1(regfile));

			klee_uerror(
				"Weird NtQueryInformationProcess",
				"sc.err");
			break;
		}


//		make_sym(
//			GET_ARG2_PTR(regfile),
//			GET_ARG3(regfile),
//			"NtQueryInformationProcess");
//
		/* result length */
		if (GET_ARG4_PTR(regfile) && out_sz) {
			*((uint32_t*)GET_ARG4_PTR(regfile)) = out_sz;
#if 0
			make_sym(GET_ARG4_PTR(regfile), 4,
				"NtQueryInformationProcess.len");
			klee_assume_ule(
				*((uint32_t*)GET_ARG4_PTR(regfile)),
				GET_ARG3(regfile));
#endif
		}

		break;
	}

	case NtQueryInformationToken:
		new_regs = sc_new_regs(regfile);
		if (GET_SYSRET(new_regs) != 0) break;

		make_sym(
			GET_ARG2_PTR(regfile),
			GET_ARG3(regfile),
			"NtQueryInformationToken");

		/* result length */
		make_sym(GET_ARG4_PTR(regfile), 4, "NtQueryInformationToken.len");
		klee_assume_ule(
			*((uint32_t*)GET_ARG4_PTR(regfile)),
			GET_ARG3(regfile));
		break;

	case NtQueryKey:
		new_regs = sc_new_regs(regfile);
		if (GET_SYSRET(new_regs) != 0) break;

		make_sym(
			GET_ARG2_PTR(regfile),
			GET_ARG3(regfile),
			"NtQueryKey");

		switch (GET_ARG1(regfile)) {
		case KeyNameInformation: {
			KEY_NAME_INFORMATION	*kni;
			kni = GET_ARG2_PTR(regfile);
			klee_assume_ule(
				kni->NameLength,
				GET_ARG3(regfile) - 4); /* room for namelen */
			break;
		}
		default:
			klee_print_expr(
				"[NT] QueryKey WTF",
				GET_ARG1(regfile));
			klee_uerror("Unknown QueryKey class", "sc.err");
		}

		/* result length */
		make_sym(GET_ARG4_PTR(regfile), 4, "NtQueryKey.len");
		klee_assume_ule(
			*((uint32_t*)GET_ARG4_PTR(regfile)),
			GET_ARG3(regfile));
		break;

	case NtCreateKey:
		new_regs = sc_new_regs(regfile);
		if (GET_SYSRET(new_regs) != 0) break;

		make_sym(GET_ARG0_PTR(regfile), 4, "NtCreateKey");
		if (GET_ARG6_PTR(regfile)) {
			make_sym(
				GET_ARG6_PTR(regfile),
				4,
				"NtCreateKey.disp");
		}
		break;

	DEFAULT_SC(NtDeleteAtom)

	WRITE_N_TO_ARG(QueryDefaultLocale, 4, 1)
	WRITE_N_TO_ARG(SetEvent, 2, 1)
	WRITE_N_TO_ARG(ReleaseMutant, 4, 1)
	WRITE_N_TO_ARG(QueryAttributesFile, 0x24, 1)
	WRITE_N_TO_ARG(CancelTimer, 4, 1)
	WRITE_N_TO_ARG(SetIoCompletion, 8 /*IO_STATUS_BLOCK */, 2)
	WRITE_N_TO_ARG(CancelIoFile, 8 /* IO_STATUS_BLOCK */, 1)
	WRITE_N_TO_ARG(ResumeThread, 4, 1)

	WRITE_N_TO_ARG(SetTimer, 4, 6)

	DEFAULT_SC(NtQueryDebugFilterState)

	DEFAULT_SC_PROTECT(WaitForMultipleObjects)

	DEFAULT_SC_PROTECT(DelayExecution)
	DEFAULT_SC(NtWaitForSingleObject)
	DEFAULT_SC(NtClose)
	DEFAULT_SC(NtClearEvent)

	WRITE_N_TO_ARG(LockFile, 8, 4)

	case NtQueryVirtualMemory:
	//DEFAULT_SC(NtQueryVirtualMemory)
	//UNIMPL_SC(NtQueryVirtualMemory)
	{
		uint32_t	*result;
		new_regs = sc_new_regs(regfile);

		if (GET_ARG2(regfile) != 0) {
			klee_print_expr("INFOCLASS", GET_ARG2(regfile));
			klee_uerror("NtQueryVirtualMemory class???", "sc.err");
		}

		if (GET_ARG3_PTR(regfile) == NULL) {
			klee_assume_ne(GET_SYSRET(new_regs), 0);
			break;
		}

		if (!nt_qvm(GET_ARG1_PTR(regfile), GET_ARG3_PTR(regfile))) {
			klee_assume_ne(GET_SYSRET(new_regs), 0);
			break;
		}

		result = GET_ARG5_PTR(regfile);
		if (result) *result = sizeof(MEMORY_BASIC_INFORMATION);
		klee_assume_eq(GET_SYSRET(new_regs), 0);
		break;
	}


	/* this is kind of cheap since it will get confused
	 * if the free size exceeds usable memory */
	case NtFreeVirtualMemory: {
		void*		base = 	GET_ARG1_PTR(regfile);
		uint32_t	sz = *((uint32_t*)GET_ARG2_PTR(regfile));

		/* TODO: handle different free types? */

		new_regs = sc_new_regs(regfile);
		base = (void*)concretize_u64((uint64_t)base);
		sz = concretize_u64(sz);
		kmc_free_run((uint64_t)base, sz);
		*((uint32_t*)GET_ARG2_PTR(regfile)) = (sz + 4095) & ~0xfff;

		break;
	}

	case NtAllocateVirtualMemory : {
		void*	ret;
		new_regs = sc_new_regs(regfile);
		ret = nt_avm(
			GET_ARG0_PTR(regfile),
			GET_ARG1(regfile), /* size */
			GET_ARG2(regfile), /* alloc type */
			GET_ARG3(regfile)); /* protection */
		klee_assume_eq(GET_SYSRET(new_regs), ret);
		break;
	}

#define NT_LPC_SZ	20

	WRITE_N_TO_ARG_PROTECT(RequestWaitReplyPort, 0x130, 2)
	WRITE_N_TO_ARG(ReleaseSemaphore, 4, 2)

	DEFAULT_SC(NtSetValueKey)
	case NtFsControlFile:
		new_regs = sc_new_regs(regfile);
		if (GET_ARG4_PTR(regfile))
			make_sym(GET_ARG4_PTR(regfile), 8,
			"NtFsControlFile.io");

		if (GET_ARGN_PTR(regfile, 8))
			make_sym(GET_ARGN_PTR(regfile, 8),
				GET_ARGN(regfile, 9),
				"NtFsControlFile.buf");
		break;

	WRITE_N_TO_ARG(UnlockFile, 8, 1)

	DEFAULT_SC(NtYieldExecution)


	case NtReadFile:
		new_regs = sc_new_regs(regfile);
		if (GET_ARG4_PTR(regfile))
			make_sym(GET_ARG4_PTR(regfile), 8, "NtReadFile.io");

		if (GET_ARG5_PTR(regfile))
			make_sym(GET_ARG5_PTR(regfile),
				GET_ARG6(regfile),
				"NtReadFile.buf");
		break;

	WRITE_N_TO_ARG(WriteFile, 8, 4) /* iobuf */


	case NtQuerySystemInformation:

		new_regs = sc_new_regs(regfile);
		if (GET_ARG1_PTR(regfile) == NULL) break;

		make_sym(GET_ARG1_PTR(regfile),
			GET_ARG2(regfile),
			"NtQuerySystemInfromation.sysinfo");

		if (GET_ARG3_PTR(regfile) == NULL) break;
		make_sym(
			GET_ARG3_PTR(regfile),
			4,
			"NtQuerySystemInformation.len");
		klee_assume_ule(*((uint32_t*)GET_ARG3_PTR(regfile)), GET_ARG2(regfile));
		break;

	case NtDeviceIoControlFile:
	new_regs = sc_new_regs(regfile);
	if (GET_ARG4_PTR(regfile))
		make_sym(GET_ARG4_PTR(regfile), 8, "NtDeviceIoControlFile.io");
	make_sym(
		GET_ARGN_PTR(regfile, 8),
		GET_ARGN(regfile, 9),
		"NtDeviceIoControlFile.outbuf");
	break;

	DEFAULT_SC(NtDeleteKey)

	DEFAULT_HANDLE_SC(OpenMutant)

	case NtQueryVolumeInformationFile:
	new_regs = sc_new_regs(regfile);
	if (GET_ARG1_PTR(regfile))
		make_sym(GET_ARG1_PTR(regfile), 8, "NtQueryVolumeInformationFile.io");
	if (GET_ARG2_PTR(regfile))
		make_sym(
			GET_ARG2_PTR(regfile),
			GET_ARG3(regfile),
			"NtQueryVolumeInformationFile.fsinfo");
	break;

	WRITE_N_TO_ARG(RaiseHardError, 4, 5)

	WRITE_N_TO_ARG(CreateSemaphore, 4, 0)
	DEFAULT_SC(NtDeleteValueKey)

	/********* win32k **************/
	DEFAULT_SC(NtUserSetWindowsHookEx)
	DEFAULT_SC(NtUserRegisterWindowMessage)
	DEFAULT_SC(NtUserCreateWindowEx)
	DEFAULT_SC(NtGdiStretchBlt)
	DEFAULT_SC(NtGdiBitBlt)
	DEFAULT_SC(NtUserOpenDesktop)
	DEFAULT_SC(NtGdiCreateDIBitmapInternal)
	DEFAULT_SC(NtGdiCreateRectRgn)
	DEFAULT_SC(NtGdiSelectFont)
	DEFAULT_SC(NtUserCheckMenuItem)
	DEFAULT_SC(NtUserDeferWindowPos)
	DEFAULT_SC(NtUserVkKeyScanEx)
	DEFAULT_SC(NtUserSetMenu)
	DEFAULT_SC(NtUserSetScrollInfo)
	DEFAULT_SC(NtUserGetWindowDC)
	DEFAULT_SC(NtUserGetForegroundWindow)
	DEFAULT_SC(NtUserGetAncestor)
	DEFAULT_SC(NtUserSetParent)
	DEFAULT_SC(NtUserConvertMemHandle)
	DEFAULT_SC(NtUserSetActiveWindow)
	DEFAULT_SC(NtUserMoveWindow)
	DEFAULT_SC(NtUserGetSystemMenu)
	DEFAULT_SC(NtUserShowWindow)
	DEFAULT_SC(NtGdiHfontCreate)
	DEFAULT_SC(NtGdiExtSelectClipRgn)
	DEFAULT_SC(NtGdiGetRandomRgn)
	WRITE_N_TO_ARG(GdiGetAppClipBox, 16, 1)
	DEFAULT_SC(NtUserCallHwndParamLock)
	DEFAULT_SC(NtUserWindowFromPoint)
	DEFAULT_SC(NtUserAlterWindowStyle)
	DEFAULT_SC(NtUserDestroyCursor)
	DEFAULT_SC(NtUserEmptyClipboard)
	DEFAULT_SC(NtUserModifyUserStartupInfoFlags)
	DEFAULT_SC(NtUserSetProp)
	DEFAULT_SC(NtUserAssociateInputContext)
	DEFAULT_SC(NtUserFindExistingCursorIcon)
	DEFAULT_SC(NtUserGetImeInfoEx)
	DEFAULT_SC(NtUserSetCapture)
	DEFAULT_SC(NtUserGetDCEx)

	/* XXX: does not write to PCLSMENUNAME, but neither does reactos */
	DEFAULT_SC(NtUserUnregisterClass)
	DEFAULT_SC(NtGdiCreateCompatibleDC)

	VOID_SC(NtUserNotifyWinEvent)

	WRITE_N_TO_ARG(UserCopyAcceleratorTable, GET_ARG2(regfile)*5, 1)
	DEFAULT_SC(NtUserPostThreadMessage)
	DEFAULT_SC(NtUserCallHwndLock)
	DEFAULT_SC(NtUserGetDoubleClickTime)
	DEFAULT_SC(NtUserSetWindowPos)
	DEFAULT_SC(NtUserShowCaret)
	WRITE_N_TO_ARG(UserGetCaretPos, 8, 0)
	DEFAULT_SC(NtUserOpenClipboard)
	DEFAULT_SC(NtGdiIntersectClipRect)
	DEFAULT_SC(NtUserDispatchMessage)

	DEFAULT_SC(NtUserFindWindowEx)

	/* short return val */
	DEFAULT_SC(NtUserGetAsyncKeyState)

	DEFAULT_SC(NtUserUpdateInputContext)
	DEFAULT_SC(NtUserThunkedMenuItemInfo)
	VOID_SC(NtUserDestroyMenu)
	DEFAULT_SC(NtUserTrackPopupMenuEx)

	WRITE_N_TO_ARG(GdiGetTextFaceW, GET_ARG1(regfile)*2, 2)
	WRITE_N_TO_ARG(GdiExtGetObjectW, GET_ARG1(regfile)*2, 2)

	WRITE_N_TO_ARG(UserGetKeyboardState, 256, 0)

	DEFAULT_SC(NtGdiFlush)
	DEFAULT_SC(NtGdiSetColorSpace)
	DEFAULT_SC(NtUserCloseClipboard)

	case NtUserCreateLocalMemHandle:
		new_regs = sc_new_regs(regfile);
		make_sym(	GET_ARG1_PTR(regfile),
				GET_ARG2(regfile),
				"NtUserCreateLocalMemHandle");
		if (GET_ARG3_PTR(regfile)) {
			make_sym(GET_ARG3_PTR(regfile),
				4,
				"NtUserCreateLocalMemHandle.pcb");

		}
		break;

	DEFAULT_SC(NtUserSetClipboardData)
	DEFAULT_SC(NtUserRemoveProp)
	DEFAULT_SC(NtUserRedrawWindow)
	DEFAULT_SC(NtUserValidateRect)
	DEFAULT_SC(NtUserSetThreadState)
	DEFAULT_SC(NtUserUnregisterHotKey)

	case NtGdiGetTextExtent:
		new_regs = sc_new_regs(regfile);
		make_sym(	GET_ARG1_PTR(regfile),
				GET_ARG2(regfile),
				"NtGdiGetTextExtent.str");
		make_sym(GET_ARG1_PTR(regfile), 8, "NtGdiGetTextExtent.sz");
		break;

	/* XXX is this an exit call? */
	DEFAULT_SC(NtUserDestroyWindow)

	DEFAULT_SC(NtUserChildWindowFromPointEx)

	DEFAULT_SC(NtUserQueryInputContext)
	DEFAULT_SC(NtUserEnableMenuItem)
	DEFAULT_SC(NtUserKillTimer)

	// XXX this one is tough
	// NtUserSystemParametersInfo:


	DEFAULT_SC(NtUserPostMessage)

	DEFAULT_SC(NtUserWaitMessage)

	// FONTSIGNATURE
	WRITE_N_TO_ARG(GdiGetTextCharsetInfo, (4*4+4*2), 1)

	DEFAULT_SC(NtUserGetProcessWindowStation)

	DEFAULT_SC(NtUserShowScrollBar)


#define MAKE_US_SYM(x,y)						\
		make_sym(					\
			(void*)((uintptr_t)y->Buffer),		\
			y->MaximumLength, 			\
			"Nt" #x ".str");			\
			make_sym(&y->Length, 2, "Nt" #x ".len");\
		klee_assume_ule(y->Length, y->MaximumLength);

	case NtUserGetAtomName: {
		struct UNICODE_STRING	*us = GET_ARG1_PTR(regfile);

		new_regs = sc_new_regs(regfile);
		if (us == NULL) break;
		MAKE_US_SYM(UserGetAtomName, us);
		break;
	}

	DEFAULT_SC(NtGdiCreateSolidBrush)


	BEGIN_PROTECT_CASE(UserGetClassName)
		struct UNICODE_STRING	*us = GET_ARG2_PTR(regfile);

		new_regs = sc_new_regs(regfile);
		if (us == NULL) break;
		MAKE_US_SYM(UserGetClassName, us);
	END_PROTECT_CASE

	DEFAULT_SC(NtUserUnhookWindowsHookEx)

#if 0
int APIENTRY NtUserToUnicodeEx 	( 	UINT  	wVirtKey, 0
UINT  	wScanCode, 1
PBYTE  	pKeyStateUnsafe, 2
LPWSTR  	pwszBuffUnsafe, 3
INT  	cchBuff, 4
UINT  	wFlags,
HKL  	dwhkl 
) 	
#endif
	case NtUserToUnicodeEx:
		if (GET_ARGN_PTR(regfile, 3) == NULL) {
			sc_ret_v(regfile, 0);
			break;
		}

		new_regs = sc_new_regs(regfile);
		if (GET_ARGN_PTR(regfile, 3) != NULL)
			make_sym(
				GET_ARGN_PTR(regfile, 3),
				GET_ARG4(regfile),
				"NtUserToUnicode");
		klee_assume_ule(GET_SYSRET(new_regs),  GET_ARG4(regfile));
		break;


#define W32K_BROADCASTPARM_SZ	(4 + 4 + 4 + 4 + 12)
#define W32K_DOSENDMESSAGE_SZ	(4 + 4 + 4)
	case NtUserMessageCall: {
		void	*p = GET_ARG4_PTR(regfile);

		new_regs = sc_new_regs(regfile);
		if (p == NULL) break;

		switch (GET_ARG5(regfile)) {
		case FNID_BROADCASTSYSTEMMESSAGE:
			make_sym(p, W32K_BROADCASTPARM_SZ,
				"NtUserMessageCall.bcast");
			break;
		case FNID_SENDMESSAGE:
			make_sym(p, 4, "NtUserMessageCall.sndmsg");
			break;

		case FNID_SENDMESSAGEFF:
		case FNID_SENDMESSAGEWTOOPTION:
			make_sym(p, W32K_DOSENDMESSAGE_SZ,
				"NtUserMessageCall.dosndmsg");
			break;

		case FNID_DEFWINDOWPROC:
		case FNID_CALLWNDPROC:
		case FNID_CALLWNDPROCRET:
		case FNID_SCROLLBAR:
		case FNID_DESKTOP:
			make_sym(p, 4, "NtUserMessageCall.lresult");
			break;
		default: break;
		}
		break;
	}

#define W32K_MSG_SZ	(4+4+4+4+4+8)

	WRITE_N_TO_ARG_PROTECT(UserPeekMessage, W32K_MSG_SZ, 0)
	WRITE_N_TO_ARG(UserCallMsgFilter, W32K_MSG_SZ, 0)
	WRITE_N_TO_ARG_PROTECT(UserGetMessage, W32K_MSG_SZ, 0)

	DEFAULT_SC(NtUserGetThreadState)

	WRITE_N_TO_ARG(UserGetClipboardData, 12, 1)

	DEFAULT_SC(NtUserSetTimer)

	case NtUserEnumDisplayMonitors:
		new_regs = sc_new_regs(regfile);
		if (GET_ARG2_PTR(regfile)) {
			make_sym(
				GET_ARG2_PTR(regfile),
				GET_ARG4(regfile),
				"NtUserEnumDisplayMonitors.monList");
		}

		if (GET_ARG3_PTR(regfile)) {
			make_sym(
				GET_ARG2_PTR(regfile),
				GET_ARG4(regfile),
				"NtUserEnumDisplayMonitors.rList");
		}

		break;


	// (HWND hWnd, HACCEL hAccel, LPMSG pUnsafeMessage
	DEFAULT_SC(NtUserTranslateAccelerator)
	case NtUserBuildHwndList: {
		unsigned len = *((uint32_t*)GET_ARG6_PTR(regfile));
		new_regs = sc_new_regs(regfile);
		if (GET_SYSRET(new_regs) != 0) break;
		make_sym(GET_ARG6_PTR(regfile), 4, "NtUserBuildHwndList.sz");
		klee_assume_ule(
			*((uint32_t*)GET_ARG6_PTR(regfile)),
			len / 4);
		if (GET_ARG5_PTR(regfile) == NULL) break;
		make_sym(GET_ARG5_PTR(regfile),	len, "NtUserBuildHwndList");
		break;
	}

	DEFAULT_SC(NtUserGetDC)
	DEFAULT_SC(NtUserHideCaret)
	DEFAULT_SC(NtUserInvalidateRect)
	DEFAULT_SC(NtUserIsClipboardFormatAvailable)
	DEFAULT_SC(NtUserQueryWindow)
	DEFAULT_SC(NtUserSetCursor)
	DEFAULT_SC(NtUserSetWindowFNID)
	DEFAULT_SC(NtUserSetWindowLong)
	DEFAULT_SC(NtUserSetFocus)
	DEFAULT_SC(NtUserTranslateMessage)
	DEFAULT_SC(NtUserValidateTimerCallback)

	DEFAULT_SC(NtGdiDeleteObjectApp)
	VOID_SC(NtGdiEngDeletePath)

	/* as far as I can tell, these just some integer so whatever */
	// SEE http://reactos.org/wiki/Techwiki:Win32k/apfnSimpleCall
	DEFAULT_SC(NtUserCallNoParam)
	DEFAULT_SC(NtUserCallOneParam)
	DEFAULT_SC(NtUserGetKeyState)
	DEFAULT_SC(GreSelectBitmap)
	DEFAULT_SC(NtUserExcludeUpdateRgn)
	DEFAULT_SC(NtGdiDeleteClientObj)
	DEFAULT_SC(NtUserNotifyIMEStatus)
	DEFAULT_SC(NtUserSetImeOwnerWindow)

	default:
		if (is_sym_sc)
			klee_uerror(
				"Unimplemented symbolically resolved syscall",
				"symsc.err");
		else
			klee_uerror("Unimplemented syscall", "sc.err");
	}



	last_sc = sc.sys_nr;

	if (sc_breadcrumb_is_newregs() || klee_is_symbolic(GET_SYSRET(regfile))) {
		/* ret value is stored in ktest regctx */
		sc_breadcrumb_commit(&sc, 0);
	} else {
		/* ret value was written concretely */
		sc_breadcrumb_commit(&sc, GET_SYSRET(regfile));
	}

//already_logged:
	return jmpptr;
}

void* concretize_ptr(void* s)
{
	uint64_t s2 = klee_get_value((uint64_t)s);
	klee_assume_eq((uint64_t)s, s2);
	return (void*)s2;
}


