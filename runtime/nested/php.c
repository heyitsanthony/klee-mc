#include <stdint.h>
#define GUEST_ARCH_AMD64
#include "../syscall/syscalls.h"
#include "klee/klee.h"

#define DECL_HOOK(x) void __hookpre_##x(void* r)


#define DECL_HOOK_POST(x)			\
static int __in_##x = 0;\
static void post_##x(void* r);			\
DECL_HOOK(x) {					\
/*	__in_##x++;				\
	if (__in_##x != 1) return;		*/\
	klee_hook_return(1, &post_##x, r);	\
}						\
static void post_##x(void* r)


DECL_HOOK(shutdown_executor)
{
	/* XXX need breadcrumb that says this is an OK exit */
	klee_print_expr("[nested] php shutdown detected. bye", GET_ARG0(r));
	kmc_exit(0);
}

#define E_WARNING		2
#define E_CORE_WARNING		32
#define E_USER_WARNING		512
#define E_USER_NOTICE		1024
#define E_STRICT		2048
#define E_DEPRECATED		8192
#define E_USER_DEPRECATED	16384
#define E_NONFATAL		(	\
	E_WARNING | E_CORE_WARNING | E_USER_WARNING | E_USER_NOTICE	\
	| E_STRICT | E_DEPRECATED | E_USER_DEPRECATED)

#define E_ERROR			1

DECL_HOOK(zend_error_noreturn)
{
	/* XXX need breadcrumb that says this is an OK exit */
	/* XXX: ARG0 is an error type ala E_ERROR, E_WARNING */
	int	err_level = GET_ARG0(r);
	klee_print_expr("[nested] php ZEND ERROR! bye", err_level);
	if ((err_level & E_DEPRECATED) != 0) {
		klee_ureport(GET_ARG2_PTR(r), "zend.err");
	} else {
		klee_uerror(GET_ARG2_PTR(r), "zend.err");
		kmc_exit(GET_ARG0(r));
	}
}

DECL_HOOK(php_socket_strerror)
{
	klee_print_expr("[nested] php_socket_strerror! bye", GET_ARG0(r));
	klee_ureport("Socket error", "socket.err");
	kmc_exit(1);
}

#if 0
DECL_HOOK_POST(zend_get_executed_filename)
{ klee_print_expr(GET_RET(r), 123); }


DECL_HOOK_POST(zend_get_executed_lineno)
{ klee_print_expr("lineno", GET_RET(r)); }
#endif