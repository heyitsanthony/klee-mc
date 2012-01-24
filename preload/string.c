#include <klee/klee.h>
#include <unistd.h>

#define IS_IN_KLEE()	(ksys_is_sym(0) == 0)

static void guard_s(const char* s)
{
	unsigned	i, k;

	if (!IS_IN_KLEE())
		return;

	/* skip over all starting symbolics */
	for (i = 0; ksys_is_sym(s[i]); i++);

	/* concrete; don't do extra work */
	if (i == 0)
		return;

	/* we permit three strings to pass
	 * 1. empty string
	 * 2. non-terminated string
	 * 3. large terminated string
	 */
	k = i;
	while (s[k++]);

	/* 1. empty string */
	if (s[0] == 0)
		return;

	/* prep for a large string */
	for (k = 0; k < i-1; k++)
		ksys_assume(s[k] != 0);

	/* 2. non-terminated string */
	if (s[i-1] != 0)
		return;

	/* 3. large, terminated string */
	ksys_assume(s[i-1] == 0);
}

static void guard_n(const char* s, size_t n)
{
	int		rc;
	unsigned	k = 0;

	if (!IS_IN_KLEE())
		return;

	if (!ksys_is_sym(n))
		return;

	for (k = 16; k < 30; k++) {
		if (n == (1 << k)) {
			char c = ((volatile const char*)s)[n];
			asm("" ::: "memory");
		}
	}
	if (n >= (1 << 12)) {
		ksys_silent_exit(0);
	}

	if (n == 0)
		return;

	for (k = 1; k < 12; k++) {
		if (n == (1 << k))
			return;
	}

	klee_assume(n == klee_get_value(n));
}

int  memcmp(const void* l, const void* r, size_t n)
{
	const char	*x = l, *y = r;
	unsigned	i;

	guard_n(l, n);
	guard_n(r, n);


	for (i = 0; i < n; i++)
		if (x[i] - y[i])
			return x[i] - y[i];
	
	return 0;
}

void* memcpy(void* d, const void* s, size_t n)
{
	unsigned	i;

	guard_n(d, n);
	guard_n(s, n);


	for (i = 0; i < n; i++)
		((char*)d)[i] = ((const char*)s)[i];
	
	return d;
}

void *memset(void *s, int c, size_t n)
{
	unsigned	i;

	guard_n(s, n);

	for (i = 0; i < n; i++)
		((char*)s)[i] = c;

	return s;
}

size_t strlen(const char* s)
{
	size_t i;

	guard_s(s);

	while (s[i]) i++;

	return i;
}

int strcmp(const char* s1, const char* s2)
{
	int	n, k;
	guard_s(s1);
	guard_s(s2);

	k = 0;
	while (	(n = (s1[k] - s2[k])) == 0
		&& s1[k] && s2[k])
		k++;

	return s1[k] - s2[k];
}

#if 0
size_t strspn(const char* s, const char* accept)
{
	guard_s(s);
	return __GI_strspn(s, accept);
#endif

void *memchr(const void *s, int c, size_t n)
{
	unsigned	k;
	guard_n(s, n);

	for (k = 0; k < n; k++)
		if (((const char*)s)[k] == (char)c)
			return &((const char*)s)[k];
	return NULL;
}
