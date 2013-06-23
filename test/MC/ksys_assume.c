// RUN: gcc %s -g -I../../../include/ -O0 -o %t1
//
// Please don't crash on this test.
// RUN: klee-mc -pipe-solver - ./%t1 2>%t1.err >%t1.out
//
// And this shouldn't cause any errors
// RUN: ls klee-last | not grep .err
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <klee/klee.h>

#define BUF_SZ	32

int main(int argc, char* argv[])
{
	int		fd, i, br;
	char		buf[BUF_SZ];

	fd = open("hey", O_RDONLY);

	br = read(fd, buf, BUF_SZ);
	if (br != BUF_SZ)
		return 1;

	if (!ksys_is_sym(buf[0])) {
		ksys_report_error(
			__FILE__, __LINE__,
			"First byte of buffer is not symbolic!",
			"sym.err");
	}

	for (i = 0; i < BUF_SZ/2; i++) {
		unsigned char	c;

		ksys_assume_ne(c, 0x26);
		ksys_assume_ne(c, '.');
		ksys_assume_ne(c, 0x36);
		ksys_assume_ne(c, 0x64);
		ksys_assume_ne(c, 0x65);

		ksys_assume_ne(
			c != 0x36 && c != 0x64 && 
			c != 0x65 && c != 0x26 && c != '.', 0);
	}

	return 0;
}