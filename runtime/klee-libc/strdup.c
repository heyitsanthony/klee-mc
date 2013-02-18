#include  <stdio.h>
extern void* malloc(unsigned);
extern char* strcpy(char *dest, const char* src);
extern unsigned strlen(const char* s);


char* __strdup(const char* s)
{
	int	len;
	char	*ret;
	if (s == NULL) return NULL;
	len = strlen(s);
	ret = malloc(len+1);
	strcpy(ret, s);
	return ret;
}