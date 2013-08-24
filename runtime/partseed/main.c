#include <stdint.h>
#include "klee/klee.h"
#include "klee/klee_partseed.h"

#define DECL_HOOK(x)	\
static int __in_##x = 0;\
static void post_##x(void* x)\
{\
klee_print_expr("stop tracking", x);\
klee_partseed_end((psid_t)x);\
__in_##x = 0;\
}	\
void __hookpre_##x(void* r) {\
	psid_t	psid = klee_partseed_begin(#x);\
	__in_##x++;\
	if (__in_##x != 1) return;\
	klee_print_expr("tracking "#x, psid);	\
	klee_hook_return(1, &post_##x, psid);\
}

DECL_HOOK(fgets)
DECL_HOOK(__tzfile_read)
//DECL_HOOK(getpwuid)
DECL_HOOK(__getpwnam_r)
DECL_HOOK(__getpwuid_r)
DECL_HOOK(__gethostbyname_r)
DECL_HOOK(gethostbyname)
//DECL_HOOK(__GI___sysconf)
DECL_HOOK(__GI___getmntent_r)
DECL_HOOK(_nc_setupterm)
//DECL_HOOK(__GI_fgets_unlocked)
DECL_HOOK(__GI_perror)
DECL_HOOK(fgetc)
DECL_HOOK(__dlopen_check)
DECL_HOOK(getservbyname)
DECL_HOOK(getservbyname_r)
DECL_HOOK(__getservbyname_r)
DECL_HOOK(__GI_getaddrinfo)
