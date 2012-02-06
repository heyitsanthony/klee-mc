#include <klee/klee.h>
#include <unistd.h>

#define IS_IN_KLEE()	(ksys_is_sym(0) == 0)

static void guard_s(const char* s)
{
	unsigned	sym_extent_sz, k, real_len;

	if (!IS_IN_KLEE())
		return;

	if (s == NULL)
		return;

	/* don't bother with purely symbolic strings */
	if (ksys_is_sym(s))
		return;

	/* skip over all starting symbolics */
	sym_extent_sz = ksys_sym_range_bytes(s, ~0);

	/* concrete; don't do extra work */
	if (sym_extent_sz == 0)
		return;

	/* we permit three strings to pass
	 * 1. empty string
	 * 2. non-terminated string
	 * 3. large terminated string
	 */

	k = sym_extent_sz;
	while (s[k++]);
	real_len = k;

	/* 1. empty string */
	if (s[0] == 0)
		return;

	/* prep for a large string */
	for (k = 0; k < sym_extent_sz-1; k++) {
		if (s[k] == 0) {
			ksys_silent_exit(0);
		}
		ksys_assume(s[k] != 0);
	}

	/* 2. non-terminated string */
	if (s[sym_extent_sz-1] != 0)
		return;

	// implied by the above ^^^
	/* 3. large, terminated string */
	// ksys_assume(s[sym_extent_sz-1] == 0);
}

static void guard_n(const char* s, size_t n)
{
	int		rc;
	unsigned	k = 0;

	if (!IS_IN_KLEE())
		return;

	if (s == NULL)
		return;

	if (!ksys_is_sym(n))
		return;

	/* probe for crashes */
	for (k = 16; k < 30; k++) {
		if (n >= (1 << k)) {
			char c = ((volatile const char*)s)[n-1];
			asm("" ::: "memory");
			break;
		}
	}

	if (n >= (1 << 12)) {
		ksys_silent_exit(0);
	}

	/* 0-length might be confusing to some bad code */
	if (n == 0)
		return;

	for (k = 1; k < 12; k++) {
		if (n <= (1 << k)) {
			klee_assume(n == klee_get_value(n));
			return;
		}
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

void* mempcpy(void* dest, const void* src, size_t n)
{ return ((char*)memcpy(dest, src, n)) + n; }

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

	i = 0;
	while (s[i]) i++;

	return i;
}

// TODO:
// strstr
// strspn
#if 0
int strcmp(const char* s1, const char* s2)
{
	int	n, k;
	guard_s(s1);
	guard_s(s2);

	k = 0;
	while (s1[k] && s2[k] && ((s1[k] - s2[k]) == 0))
		k++;

	return s1[k] - s2[k];
}

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
			return &((char*)s)[k];
	return NULL;
}

char* strchr(const char* s, int c)
{
	unsigned	k;
	guard_s(s);

	for (k = 0; s[k] && s[k] != c; k++); 

	if (s[k] == 0)
		return NULL;

	return (char*)&s[k];
}
