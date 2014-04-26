/* not really a header but easier than having a legit library */
#ifndef UCDRIVER_H
#define UCDRIVER_H

#include <stdint.h>
#include <sys/mman.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <dlfcn.h>
#include <setjmp.h>
#include <signal.h>

struct uc_desc { void *ucd_data, *ucd_base, *ucd_seg; unsigned ucd_pgs, ucd_bytes; };
struct uc_args { uint64_t uca_args[6]; };

typedef long (*ucfptr_t)(long, long, long, long, long, long);

static sigjmp_buf	probe_env;
static void uc_probe_sig(int signum) { siglongjmp(probe_env, 1); }

#ifdef CHK_LINKAGE
extern long CHK_LINKAGE(long,long,long,long,long,long);
#endif

/* figure out whether pointer can be derefed by using sigsetjmp */
static int is_ret_ptr(const void* p)
{
	/* XXX implement this */
	struct sigaction        act, oldact;
	int			ok_ptr = 0;

	memset(&act, 0, sizeof(act));
	act.sa_handler = uc_probe_sig;
	sigaction(SIGSEGV, &act, &oldact);

	if (sigsetjmp(probe_env, 1) == 0) {
		/* probing */
		volatile char c = *((const char*)p);
		(void)c;
		ok_ptr = 1;
	} else {
		/* faulted */
		ok_ptr = 0;
	}

	sigaction(SIGSEGV, &oldact, NULL);
	return ok_ptr;
}

static void uc_run(
	const char* lib_path, const char* func_name,
	struct uc_desc* ucds, struct uc_args* uca)
{
	long		v;
	ucfptr_t	ucf;
	struct uc_desc	*ucd;

	/* map in buffers */
	for (ucd = ucds; ucd->ucd_data; ucd++) {
		void* x = mmap(ucd->ucd_seg, 4096*ucd->ucd_pgs,
			PROT_READ|PROT_WRITE, 
			MAP_ANONYMOUS|MAP_FIXED|MAP_PRIVATE, -1, 0);
		assert (x == ucd->ucd_seg);
		memcpy(ucd->ucd_base, ucd->ucd_data, ucd->ucd_bytes);
	}

	/* get function */
	ucf = dlsym(dlopen(lib_path, RTLD_GLOBAL|RTLD_LAZY), func_name);
	if (ucf == NULL) {
		/* aborts are problematic because they look
		 * like crashes */
		printf("[OOPS] FAILED TO LOAD SYM '%s' from '%s'\n",
			func_name, lib_path);
		return;
	}

/* link with stdlib and make sure dlsym didn't default to already present
 * library... */
#ifdef CHK_LINKAGE
	if (ucf == CHK_LINKAGE) {
		printf("[OOPS] DID NOT LOAD SYM from '%s'\n", lib_path);
		return;
	}
#endif

	/* execute function */
	/* XXX: what about floating point?? */
	v = ucf(uca->uca_args[0], 
		uca->uca_args[1],
		uca->uca_args[2],
		uca->uca_args[3],
		uca->uca_args[4],
		uca->uca_args[5]);

	/* sometimes dump buffers */
	if (getenv("UC_DUMPBUFS")) {
		for (ucd = ucds; ucd->ucd_data; ucd++) {
			int	i;
			printf("%p : ", ucd->ucd_data);
			for (i = 0; i < ucd->ucd_bytes; i++)
				printf("%x ", ((unsigned char*)(ucd->ucd_data))[i]);
			printf("\n");
		}
	}


	if (v != 0 && is_ret_ptr((void*)v)) {
		/* pointer masking because we don't have a writeset */
		printf("%lx\n", 0xdeadbeefcafe);
	} else
		/* dump return value if clearly not pointer.. */
		printf("%lx\n", v);
}

#endif /*UCDRIVER_H */
