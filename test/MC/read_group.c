// RUN: gcc %s -O0 -I../../../include/  -o %t1
// RUN: klee-mc -pipe-solver -concrete-vfs - ./%t1 2>%t1.err >%t1.out
//
// Should not have any errors.
// RUN: ls klee-last | not grep err

#include "klee/klee.h"
#include <unistd.h>
#include <fcntl.h>
#include <syscall.h>
#include <stdio.h>

int main(int argc, char* argv[])
{
	int	fd;
	char	buf[64];
	ssize_t	br;

	fd  = open("/etc/group", O_RDONLY);
	if (fd < 0) {
		ksys_report_error(
			__FILE__,
			__LINE__, 
			"Could not open /etc/group. Concretes on?",
			"open.err");
		return -1;
	}

	br = read(fd, buf, 4);
	if (br < 4) {
		ksys_report_error(
			__FILE__, __LINE__,
			"Could not read 4 bytes from /etc/group. What.",
			"size.err");
	}

	if (buf[0] != 'r') {
		ksys_report_error(
			__FILE__, __LINE__,
			"Reading bogus groups file. Expected root on line 1",
			"data.err");
	}

	/* expect: "root:0"...*/
	br = read(fd, buf, 2);
	if (br != 2) {
		ksys_report_error(
			__FILE__, __LINE__,
			"Could not read 6 bytes from /etc/group. What.",
			"size.err");
	}

	if (buf[0] != ':') {
		ksys_report_error(
			__FILE__, __LINE__,
			"Reading bogus groups file. Expected ':' at character 5",
			"data.err");
	}

	/* we did it! */

	close(fd);

	return 0;
}