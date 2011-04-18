// RUN: %llvmgcc %s -emit-llvm -O0 -c -o %t1.bc
// RUN: %klee --libc=uclibc --posix-runtime %t1.bc > %t.log
// RUN: grep -q "PASSED" %t.log

#include <stdio.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <assert.h>
#include <linux/net.h>
#include <errno.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <stdio.h>
#include <assert.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>

#define EXPECT_FAILURE(rc, err) if ((rc) >= 0) { \
  printf("succeeded unexpectedly\n"); \
  return EXIT_FAILURE; \
} else if (errno != (err)) { \
  printf("failed with unexpected error (errno=%d, %s)\n", errno, strerror(errno)); \
  return EXIT_FAILURE; \
} else \
  printf("failed with expected error (errno=%d, %s)\n", errno, strerror(errno))

int main(int argc, char **argv) {
  struct sockaddr_in server_addr, client_addr;
  
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(1080);

  int r = bind(STDIN_FILENO, (struct sockaddr *) &server_addr, 
	       sizeof(server_addr));
  EXPECT_FAILURE(r, ENOTSOCK);

  printf("PASSED\n");
   
  return 0;
}
