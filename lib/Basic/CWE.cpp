#include <string.h>

struct CWE_ent
{
	const char	*ce_klee_msg;
	int		ce_cwe_id;
};

static struct CWE_ent cwe_tab[] = 
{
{ "Tried to divide by zero!", 369},
{ "bad memory access!", 125},	/* XXX out of bounds read */
{ "Loading from free const pointer", 416}, /* use after free */
{ "Storing to free const pointer", 416}, /* use after free */
{ "Loading from free sym pointer", 416}, /* use after free */
{ "Storing to free sym pointer", 416}, /* use after free */
{ "Freeing freed pointer!", 415}, /* double free */
{ "non-const free address", 763},
{ "free invalid address", 763},
{ "free of alloca", 763},
{ "free of global", 763},
{ NULL, 0}
};

namespace klee
{
/* translates error messages into CWE codes */
int CWE_xlate(const char* err_str)
{
	int	i = 0;
	while (cwe_tab[i].ce_klee_msg != NULL) {
		if (strstr(err_str, cwe_tab[i].ce_klee_msg) == NULL)
			continue;

		return cwe_tab[i].ce_cwe_id;
	}

	return -1;
}
}