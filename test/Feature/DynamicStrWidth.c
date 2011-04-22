/* OH THE HUMANITY */
// RUN: %llvmgcc %s -emit-llvm -DSYMSTR_LEN=12 -O0 -c -o %t1.bc
// RUN: %klee %t1.bc >%t2.log 2>%t2.errlog
// RUN: %klee -string-prune %t1.bc 
// RUN: test ! -f klee-last/test000010.user.err
// RUN: klee-stats klee-last >stats10
// RUN: cp klee-last/info run10
// RUN: cp klee-last/messages.txt mess10


/* XXX: grep "total queries = 2" klee-last/info */
#include <stdio.h>
#include <assert.h>

#define CONCRETE_LEN	10
//#define SYMSTR_LEN	11

const char *strtab[] = {
	"ggggggggg",
	"aaaaaaaaa",
	"ppppppppp",
	"bbbbbbbbb",
	"ccccccccc",
	"ddddddddd",
	"hhhhhhhhh",
	NULL
};

const char concrete_str[CONCRETE_LEN] = {};

static int our_strcmp(const char* s1, const char* s2)
{
	while (*s1 == *s2) {
		if (*s1 == '\0') break;
		s1++;
		s2++;
	}
	return *s1 - *s2;
}

int main(void)
{
	char		sym_str[SYMSTR_LEN];
	char		**tabptr;

	klee_make_symbolic(sym_str, SYMSTR_LEN, "derp");
	assert (klee_is_symbolic(sym_str[0]) && 
		"Permit sym less than sym size!");

	tabptr = strtab;
	while (*tabptr) {
		if (our_strcmp(sym_str, *tabptr) == 0) break;
		tabptr++;
	}
	
	if (*tabptr) printf("Success.\n");
	else printf("Fail.\n");

	return 0;
}