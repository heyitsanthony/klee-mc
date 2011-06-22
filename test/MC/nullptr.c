// RUN: gcc %s -O0 -o %t1
// RUN: klee-mc - ./%t1 2>%t1.err >%t1.out
//
// Concrete / no syscalls so there should only be one explored path.
// RUN: grep "explored paths = 1" %t1.err
//
// It should exit with the string length
// RUN: ls klee-last | grep "ptr.err"

int main(void)
{
	char	*x = (char*)0;
	*x = 1;
	return 0;
}