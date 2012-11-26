// RUN: %llvmgcc %s -emit-llvm -O0 -c -o %t1.bc
// RUN: %klee --libc=uclibc --posix-runtime %t1.bc --sym-datagrams 2 20 >%t.log  2>%t.err
// RUN: grep -q Success %t.log
// RUN: gcc -g -Dklee_silent_exit=exit %s
// RUN: %replay ./a.out klee-last/test000001.ktest > %t2.log
// RUN: grep -q Success %t2.log

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>

int main(int argc, char** argv)
{
  unsigned char packet[20];
  struct sockaddr_in server_addr;
  int r, fd, fd2;
  ssize_t sz;

  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  server_addr.sin_port = htons(8326);

  fd = socket(PF_INET, SOCK_DGRAM, 0);
  if (fd < 0) { perror("socket"); return EXIT_FAILURE; }

  r = bind(fd, (const struct sockaddr *) &server_addr, sizeof(server_addr));
  if (r < 0) { perror("bind"); return EXIT_FAILURE; }

  printf("dup(%d)\n", fd);
  r = dup(fd);
  if (r < 0) { perror("dup"); return EXIT_FAILURE; }
  fd2 = r;

  sz = recv(fd2, packet, sizeof(packet), 0);
  if (sz < 0)
    perror("recvfrom");
  else
    printf("received %d bytes\n", sz);

  printf("close(%d)\n", fd2);
  r = close(fd2);
  if (r < 0) { perror("close"); return EXIT_FAILURE; }

  printf("dup2(%d, %d)\n", fd, fd2);
  r = dup2(fd, fd2);
  if (r < 0) { perror("dup"); return EXIT_FAILURE; }
  fd2 = r;

  sz = recv(fd2, packet, sizeof(packet), 0);
  if (sz < 0)
    perror("recvfrom");
  else
    printf("received %d bytes\n", sz);

  printf("close(%d)\n", fd2);
  r = close(fd2);
  if (r < 0) { perror("close"); return EXIT_FAILURE; }

  printf("close(%d)\n", fd);
  r = close(fd);
  if (r < 0) { perror("close"); return EXIT_FAILURE; }

  printf("Success\n");
  return EXIT_SUCCESS;
}
