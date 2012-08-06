#include <assert.h>
#include "klee/klee.h"

static const char *assert_prefix = "ASSERTION FAIL: ";

void klee_assert_fail(void* a, void* b, unsigned c, void* d)
{
	unsigned	i, n;
	char		msg[256];

	for (i = 0; assert_prefix[i]; i++)
		msg[i] = assert_prefix[i];

	for (n = 0; ((char*)a)[n] && i < 256; i++, n++)
		msg[i] = ((char*)a)[n];

	msg[i] = '\0';

	klee_uerror(msg, "assert.err");
}

void __assert_fail (__const char *__assertion, __const char *__file,
                           unsigned int __line, __const char *__function)
{ klee_assert_fail((void*)__assertion, NULL, 0, NULL); }

void __assert_rtn(void* a, void* b, void* c, void* d)
{ klee_assert_fail(a, b, (unsigned)c, d); }

void _assert(void* a, void* b, void* c)
{ klee_assert_fail(a, b, (unsigned)c, NULL); }

void abort(void) { klee_abort(); }
void klee_abort(void) { klee_uerror("abort failure", "abort.err"); }
