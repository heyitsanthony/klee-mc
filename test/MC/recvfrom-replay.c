// RUN: gcc %s -I../../../include/ -O0 -o %t1
// RUN: rm -f guest-*
// RUN: ../../../scripts/save_guest.sh ./%t1 guest-%t1
// RUN: klee-mc -logregs -guest-type=sshot  - ./%t1 2>%t1.err >%t1.out
// RUN: ls klee-last | not grep .err
// RUN: grep "exitcode=0" %t1.err
//
// replay tests
// RUN: kmc-replay 1 >%t1.replay.out 2>%t1.replay.err
// RUN: grep xitcode %t1.replay.err
// RUN: kmc-replay 2 >%t1.replay.out 2>%t1.replay.err
// RUN: grep xitcode %t1.replay.err
//
// RUN: rm -rf guest-%t1 guest-last

#include <sys/types.h>
#include <sys/socket.h>
#include <klee/klee.h>

int main(int argc, char* argv[])
{
	ssize_t	s;
	char	buf[1024];
	struct sockaddr	sa;
	socklen_t	addrlen;

	s = recvfrom(123, buf, 1024, ~0, &sa, &addrlen);
	if (s == -1)
		return -1;
	return 0;
}