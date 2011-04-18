// RUN: %llvmgcc %s -emit-llvm -O0 -c -o %t1.bc
// RUN: %klee --libc=uclibc --posix-runtime %t1.bc --sym-streams 1 1 > %t.log
// RUN: grep -q Success %t.log
// RUN: gcc -g %s
// RUN: %replay ./a.out klee-last/test000001.ktest > %t2.log
// RUN: grep -q Success %t2.log

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

int main(int argc, char** argv)
{
	struct sockaddr_in server_addr, client_addr;
	socklen_t client_addrlen = sizeof(client_addr);
	int lfd, r, cfd;
	FILE* f;
	char buf[2];

	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	server_addr.sin_port = htons(2020);

	lfd = socket(PF_INET, SOCK_STREAM, 0);
	if (lfd < 0) { perror("socket"); return EXIT_FAILURE; }

	r = bind(lfd, (const struct sockaddr *) &server_addr, sizeof(server_addr));
	if (r < 0) { perror("bind"); return EXIT_FAILURE; }

	r = listen(lfd, 5);
	if (r < 0) { perror("listen"); return EXIT_FAILURE; }

	cfd = accept(lfd, (struct sockaddr *) &client_addr, &client_addrlen);
	if (cfd < 0) { perror("accept"); return EXIT_FAILURE; }

	f = fdopen(cfd, "w+");
	if (!f) { perror("fdopen"); return EXIT_FAILURE; }

        if (fgets(buf, sizeof buf, f) == NULL) { perror("fgets"); return EXIT_FAILURE; }

        if (fputs(buf, f) < 0) { perror("fputs"); return EXIT_FAILURE; }

	r = fclose(f);
        if (r < 0) { perror("fclose"); return EXIT_FAILURE; }

        r = close(lfd);
        if (r < 0) { perror("close"); return EXIT_FAILURE; }

        puts("Success");

	return 0;
}
