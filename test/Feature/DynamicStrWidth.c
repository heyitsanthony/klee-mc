/* OH THE HUMANITY */
// RUN: %llvmgcc %s -emit-llvm -DSYMSTR_LEN=12 -O0 -c -o %t1.bc
// RUN: %klee %t1.bc >%t2.log 2>%t2.errlog
// RUN: test ! -f klee-last/test000010.user.err
// RUN: klee-stats klee-last >stats10
// RUN: cp klee-last/info run10
// RUN: cp klee-last/messages.txt mess10


/* XXX: grep "total queries = 2" klee-last/info */

#include <string.h>
#include <assert.h>

#define CONCRETE_LEN	10
//#define SYMSTR_LEN	11

const char concrete_str[CONCRETE_LEN] = {"123456789"};

int main(void)
{
	char		sym_str[SYMSTR_LEN];
	unsigned int	i;

	klee_make_symbolic(sym_str, SYMSTR_LEN, "derp");
	assert (klee_is_symbolic(sym_str[0]) && 
		"Permit sym less than sym size!");
	for (i = 0; sym_str[i] == concrete_str[i]; i++) {
		if (sym_str[i] == '\0' || concrete_str[i] == '\0')
			break;
	}

	if (i == CONCRETE_LEN-1) printf("Success.\n");
	else printf("Fail.\n");

	return 0;
}