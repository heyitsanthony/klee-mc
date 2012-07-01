// RUN: gcc %s  -DFP_ADD -O0 -o %t1-add
// RUN: klee-mc -pipe-solver -branch-hint=false -max-stp-time=5 -stop-after-n-tests=10 -use-softfp - ./%t1-add 2>%t1.err >%t1.out
//
// RUN: ls klee-last | not grep ".err"
// RUN: not grep "oncret" klee-last/warnings.txt
// RUN: ls klee-last | grep 10.ktest.gz
//
// RUN: gcc %s -DFP_DIV -O0 -o %t1-div
// RUN: klee-mc -pipe-solver -branch-hint=false -max-stp-time=5 -stop-after-n-tests=10 -use-softfp - ./%t1-div 2>%t1-div.err >%t1-div.out
//
// RUN: ls klee-last | not grep ".err"
// RUN: not grep "oncret" klee-last/warnings.txt
// RUN: ls klee-last | grep 10.ktest.gz
//
// RUN: gcc %s -DFP_MUL -O0 -o %t1-mul
// RUN: klee-mc -pipe-solver -branch-hint=false -max-stp-time=5 -stop-after-n-tests=10 -use-softfp - ./%t1-mul 2>%t1-mul.err >%t1-mul.out
//
// RUN: ls klee-last | not grep ".err"
// RUN: not grep "oncret" klee-last/warnings.txt
// RUN: ls klee-last | grep 10.ktest.gz
//
// RUN: gcc %s -DFP_SUB -O0 -o %t1-sub
// RUN: klee-mc -pipe-solver -branch-hint=false -max-stp-time=5 -stop-after-n-tests=10 -use-softfp - ./%t1-sub 2>%t1-sub.err >%t1-sub.out
//
// RUN: ls klee-last | not grep ".err"
// RUN: not grep "oncret" klee-last/warnings.txt
// RUN: ls klee-last | grep 10.ktest.gz
//
// RUN: gcc %s  -DFP_CMP_EQ -DFP_ADD -O0 -o %t1-addeq
// RUN: klee-mc -pipe-solver -branch-hint=false -max-stp-time=5 -stop-after-n-tests=10 -use-softfp - ./%t1-addeq 2>%t1-addeq.err >%t1-addeq.out
//
// RUN: ls klee-last | not grep ".err"
// RUN: not grep "oncret" klee-last/warnings.txt
// RUN: ls klee-last | grep 10.ktest.gz
//
// RUN: gcc %s  -DINT_CMP_EQ -DFP_ADD -O0 -o %t1-addinteq
// RUN: klee-mc -pipe-solver -branch-hint=false -max-stp-time=5 -stop-after-n-tests=10 -use-softfp - ./%t1-addinteq 2>%t1-addinteq.err >%t1-addinteq.out
//
// RUN: ls klee-last | not grep ".err"
// RUN: not grep "oncret" klee-last/warnings.txt
// RUN: ls klee-last | grep 10.ktest.gz
//
// RUN: gcc %s  -DFP_CMP_LE -DFP_ADD -O0 -o %t1-addle
// RUN: klee-mc -pipe-solver -branch-hint=false -max-stp-time=5 -stop-after-n-tests=10 -use-softfp - ./%t1-addle 2>%t1-addle.err >%t1-addle.out
//
// RUN: ls klee-last | not grep ".err"
// RUN: not grep "oncret" klee-last/warnings.txt
// RUN: ls klee-last | grep 10.ktest.gz


#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>

int main(void)
{
	int	r;
	double	d, x;
	float	f;

	r = read(0, &d, sizeof(double));
	if (r <= 0) return -1;

	r = read(0, &f, sizeof(float));
	if (r <= 0) return -1;

#if defined(FP_ADD)
	x = d + f;
#elif defined(FP_SUB)
	x = d - f;
#elif defined(FP_MUL)
	x = d * f;
#elif defined(FP_DIV)
	x = d / f;
#else
#error wut
#endif

#if defined(FP_CMP_EQ)
	if (x == 0.0) {
#elif defined(FP_CMP_LE)
	if (x <= 0.0) {
#elif defined(INT_CMP_EQ)
	if ((int)x == 0) {
#else
	if (x < 0.0) {
#endif
		write(1, "LTZ\n", 4);
	} else {
		write(1, "GEZ\n", 4);
	}

	return 0;
}