/* 
 * Stupidest implementations I can think of.
 * Hopefully won't stump the checker.
 */
#include <unistd.h>

int  memcmp(const void* l, const void* r, size_t n)
{
	const char	*x = l, *y = r;
	unsigned	i;

	for (i = 0; i < n; i++)
		if (x[i] - y[i])
			return x[i] - y[i];
	
	return 0;
}

void* memcpy(void* d, const void* s, size_t n)
{
	unsigned	i;

	for (i = 0; i < n; i++)
		((char*)d)[i] = ((const char*)s)[i];
	
	return d;
}

void* mempcpy(void* dest, const void* src, size_t n)
{ return ((char*)memcpy(dest, src, n)) + n; }

void *memset(void *s, int c, size_t n)
{
	unsigned	i;

	for (i = 0; i < n; i++)
		((char*)s)[i] = c;

	return s;
}

size_t strlen(const char* s)
{
	size_t i;

	i = 0;
	while (s[i]) i++;

	return i;
}

char* strcpy(char* dest, const char* src)
{
	char*	old_dest = dest;

	while (*src) {
		*dest = *src;
		dest++;
		src++;
	}
	*dest = '\0';

	return old_dest;
}

int strcmp(const char* s1, const char* s2)
{
	int	n, k;

	k = 0;
	while (((s1[k] - s2[k]) == 0) && s1[k])
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

	for (k = 0; k < n; k++)
		if (((const char*)s)[k] == (char)c)
			return &((char*)s)[k];
	return NULL;
}

char* strchr(const char* s, int c)
{
	unsigned	k;

	for (k = 0; s[k] != c && s[k]; k++); 

	if (s[k] == 0)
		return NULL;

	return (char*)&s[k];
}

/* stupid implementation from internet-- whatever */
void* memmove(void* _dest, const void* _src, size_t n)
{
	char		*dest = _dest;
	const char	*src = _src;

	if (dest == src)
		return;

	/* Check for destructive overlap.  */
	if (src < dest && dest < src + n) {
		/* Destructive overlap ... have to copy backwards.  */
		src += n;
		dest += n;
		while (n-- > 0)
			*--dest = *--src;
	} else {
		/* Do an ascending copy.  */
		while (n-- > 0) *dest++ = *src++;
	}
}

// TODO:
// strstr
// strspn
