// RUN: gcc %s -O0 -o %t1
// RUN: klee-mc - ./%t1 2>%t1.err >%t1.out
// RUN: ls klee-last | not grep .err
#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/socket.h>

int main(int argc, char* argv[])
{
	struct sockaddr	addr;
	socklen_t	addrlen;
	int		sock_fd;
	
	addrlen = 0;
	sock_fd = accept4(sockfd, &addr, &addrlen, 0);

#if 0
	if (addrlen != 4) {
		ksys_error("", ".err");
	assert (addrlen == 
#endif
	return 0;
}