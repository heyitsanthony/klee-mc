// RUN: gcc %s -O0 -I../../../include/  -o %t1
// RUN: klee-mc - ./%t1 2>%t1.err >%t1.out
//
// Should not have any errors.
// RUN: ls klee-last | not grep err
#include <klee/klee.h>
#include <string.h>
#include <syscall.h>
#include <unistd.h>

int main(void)
{
	char	asd[32];
	char	*ret;
	if ((ret = getcwd(asd, 32)) == NULL)
		return 0;

	if (ret != asd) {
		ksys_print_expr("Given", asd);
		ksys_print_expr("Returned", ret);
		ksys_report_error(
			__FILE__,
			__LINE__,
			"getcwd return pointer does not match input",
			"badcwd.err");
	}

	if (ret[9] != 0) {
		ksys_report_error(
			__FILE__,
			__LINE__,
			"getcwd did not set terminator",
			"badcwd.err");
	}

	return strlen(ret);
}