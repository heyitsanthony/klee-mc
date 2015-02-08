#include "klee-replay.h"
#include "replay-util.h"
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ptrace.h>
#include <asm/ptrace.h>

#define WORD_SIZE sizeof(long)

union U {
  long word;
  char chars[WORD_SIZE];
};

void get_data(pid_t child, const long* addr, void* buf, size_t len) {
  union U u;
  size_t i;

  for (i = 0; i < len / WORD_SIZE; i++)
    ((long*)buf)[i] = ptrace(PTRACE_PEEKDATA, child, addr + i, NULL);

  if (len % WORD_SIZE != 0) {
    u.word = ptrace(PTRACE_PEEKDATA, child, addr + len / WORD_SIZE, NULL);
    memcpy((char*) buf + len - len % WORD_SIZE, u.chars, len % WORD_SIZE);
  }
}

void put_data(pid_t child, long* addr, const void* buf, size_t len) {
  union U u;
  size_t i;

  for (i = 0; i < len / WORD_SIZE; i++) {
    int res = ptrace(PTRACE_POKEDATA, child, addr + i, ((long*)buf)[i]);
    assert(res == 0);
  }

  if (len % WORD_SIZE != 0) {
    u.word = ptrace(PTRACE_PEEKDATA, child, addr + len / WORD_SIZE, NULL);
    memcpy(&u.chars, (char*) buf + len - len % WORD_SIZE, len % WORD_SIZE);
    int res = ptrace(PTRACE_POKEDATA, child, addr + len / WORD_SIZE, u.word);
    assert(res == 0);
  }
}

void set_data(pid_t child, long* addr, char byte, size_t len) {
  union U u, v;
  size_t i;

  memset(v.chars, byte, WORD_SIZE);

  for (i = 0; i < len / WORD_SIZE; i++) {
    int res = ptrace(PTRACE_POKEDATA, child, addr + i, v.word);
    assert(res == 0);
  }

  if (len % 4 != 0) {
    u.word = ptrace(PTRACE_PEEKDATA, child, addr + len / WORD_SIZE, NULL);
    memcpy(&u.chars, &v.chars, len % WORD_SIZE);
    int res = ptrace(PTRACE_POKEDATA, child, addr + len / WORD_SIZE, u.word);
    assert(res == 0);
  }
}

char* get_string(pid_t child, unsigned* addr) {
  size_t n = 0;
  union U u, r[1024];

  u.word = ptrace(PTRACE_PEEKDATA, child, addr, NULL);
  
  while (memchr(&u, '\0', sizeof(u)) == NULL) {
    assert(n < sizeof(r) / sizeof(*r));
    r[n++] = u;    
    u.word = ptrace(PTRACE_PEEKDATA, child, addr + n, NULL);
  }  
  r[n++] = u;

  void* str = calloc(n, WORD_SIZE);
  memcpy(str, r, n * WORD_SIZE);
  ((char*)str)[(n*WORD_SIZE)-1] = '\0';
  return (char*)str;
}

// XXX: Does these ptrace calls work on 64-bit systems?
void read_into_buf(pid_t child, int fd, long* addr, size_t len) {
  void* buf = malloc(len);
  ssize_t r = read(fd, buf, len);
  if (r < 0) {
    int res = ptrace(PTRACE_POKEUSER, child, KLEE_REGSIZE * KLEE_EAX, -errno);
    assert(res == 0);
  }
  else {
    put_data(child, addr, buf, r);
    int res = ptrace(PTRACE_POKEUSER, child, KLEE_REGSIZE * KLEE_EAX, r);
    assert(res == 0);
  }
  free(buf);
}
