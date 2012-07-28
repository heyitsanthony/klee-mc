// RUN: gcc %s  -DFP_ADD -O0 -o %t1-add
// RUN: ../../../scripts/save_guest.sh ./%t1-add guest-add
// RUN: klee-mc -pipe-solver -branch-hint=false -max-stp-time=5 -stop-after-n-tests=10 -use-softfp -validate-test -guest-type=sshot -  2>%t1.err >%t1.out
//
// RUN: ls klee-last | not grep ".err"
// RUN: not grep "oncret" klee-last/warnings.txt
// RUN: find -L klee-last -name *validate | xargs cat | grep OK 
// RUN: find -L klee-last -name *validate | xargs cat | not grep FAIL

//
// RUN: gcc %s -DFP_DIV -O0 -o %t1-div
// RUN: ../../../scripts/save_guest.sh ./%t1-div guest-div
// RUN: klee-mc -pipe-solver -branch-hint=false -max-stp-time=5 -stop-after-n-tests=10 -use-softfp -validate-test -guest-type=sshot - 2>%t1-div.err >%t1-div.out
//
// RUN: ls klee-last | not grep ".err"
// RUN: not grep "oncret" klee-last/warnings.txt
// RUN: find -L klee-last -name *validate | xargs cat | grep OK 
// RUN: find -L klee-last -name *validate | xargs cat | not grep FAIL
//
// RUN: gcc %s -DFP_MUL -O0 -o %t1-mul
// RUN: ../../../scripts/save_guest.sh ./%t1-mul guest-mul
// RUN: klee-mc -pipe-solver -branch-hint=false -max-stp-time=5 -stop-after-n-tests=10 -use-softfp -validate-test -guest-type=sshot - 2>%t1-mul.err >%t1-mul.out
//
// RUN: ls klee-last | not grep ".err"
// RUN: not grep "oncret" klee-last/warnings.txt
// RUN: find -L klee-last -name *validate | xargs cat | grep OK 
// RUN: find -L klee-last -name *validate | xargs cat | not grep FAIL
//
// RUN: gcc %s -DFP_SUB -O0 -o %t1-sub
// RUN: ../../../scripts/save_guest.sh ./%t1-sub guest-sub
// RUN: klee-mc -pipe-solver -branch-hint=false -max-stp-time=5 -stop-after-n-tests=10 -use-softfp -validate-test -guest-type=sshot - 2>%t1-sub.err >%t1-sub.out
//
// RUN: ls klee-last | not grep ".err"
// RUN: not grep "oncret" klee-last/warnings.txt
// RUN: find -L klee-last -name *validate | xargs cat | grep OK 
// RUN: find -L klee-last -name *validate | xargs cat | not grep FAIL
//
// RUN: gcc %s  -DFP_CMP_EQ -DFP_ADD -O0 -o %t1-addeq
// RUN: ../../../scripts/save_guest.sh ./%t1-addeq guest-addeq
// RUN: klee-mc -pipe-solver -branch-hint=false -max-stp-time=5 -stop-after-n-tests=10 -use-softfp -validate-test -guest-type=sshot - 2>%t1-addeq.err >%t1-addeq.out
//
// RUN: ls klee-last | not grep ".err"
// RUN: not grep "oncret" klee-last/warnings.txt
// RUN: find -L klee-last -name *validate | xargs cat | grep OK 
// RUN: find -L klee-last -name *validate | xargs cat | not grep FAIL
//
// RUN: gcc %s  -DINT_CMP_EQ -DFP_ADD -O0 -o %t1-addinteq
// RUN: ../../../scripts/save_guest.sh ./%t1-addinteq guest-addinteq
// RUN: klee-mc -pipe-solver -branch-hint=false -max-stp-time=5 -stop-after-n-tests=10 -use-softfp -validate-test -guest-type=sshot - 2>%t1-addinteq.err >%t1-addinteq.out
//
// RUN: ls klee-last | not grep ".err"
// RUN: find -L klee-last -name *validate | xargs cat | grep OK 
// RUN: find -L klee-last -name *validate | xargs cat | not grep FAIL
//
// RUN: gcc %s  -DFP_CMP_LE -DFP_ADD -O0 -o %t1-addle
// RUN: ../../../scripts/save_guest.sh ./%t1-addle guest-addle
// RUN: klee-mc -pipe-solver -branch-hint=false -max-stp-time=5 -stop-after-n-tests=10 -use-softfp -validate-test -guest-type=sshot - 2>%t1-addle.err >%t1-addle.out
//
// RUN: ls klee-last | not grep ".err"
// RUN: not grep "oncret" klee-last/warnings.txt
// RUN: find -L klee-last -name *validate | xargs cat | grep OK 
// RUN: find -L klee-last -name *validate | xargs cat | not grep FAIL
//
// RUN: rm -rf guest-add guest-div guest-mul guest-sub guest-addeq guest-addle guest-addinteq


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