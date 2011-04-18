#include "klee-replay.h"
#include "replay-socket.h"
#include "replay-fd.h"
#include "replay-util.h"

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ptrace.h>
#include <asm/ptrace.h>
#include <linux/net.h>

#define MAX_STREAMS 64
static int stream_fds[MAX_STREAMS];

#define MAX_DGRAMS 64
static int dgram_fds[MAX_DGRAMS];

extern int read_n_calls;
extern int write_n_calls;

/* Opens the files associated with each stream and each datagram */
void open_socket_files() {
  char* name;
  unsigned i;

  assert(__exe_fs.n_sym_streams <= MAX_STREAMS);
  for (i = 0; i < __exe_fs.n_sym_streams; i++) {
    stream_fds[i] = open(name = __exe_fs.sym_streams[i].name, O_RDWR);
    if (stream_fds[i] < 0) {
      fprintf(stderr, "KLEE_REPLAY: Cannot open stream file %s\n", name);
      exit(1);
    }
  }

  assert(__exe_fs.n_sym_dgrams <= MAX_DGRAMS);
  for (i = 0; i < __exe_fs.n_sym_dgrams; i++) {
    dgram_fds[i] = open(name = __exe_fs.sym_dgrams[i].name, O_RDWR);
    if (dgram_fds[i] < 0) {
      fprintf(stderr, "KLEE_REPLAY: Cannot open dgram file %s\n", name);
      exit(1);
    }
  }
}

static exe_disk_file_t* find_exe_disk_file(int fd)
{
  unsigned n;
  if (!fd_attrs[fd].socket)
    return NULL;

  if (!fd_attrs[fd].datagram) {
    for (n = 0; n < __exe_fs.n_sym_streams; n++) {
      if (stream_fds[n] == fd)
        return &__exe_fs.sym_streams[n];
    }
  }
  else {
    for (n = 0; n < __exe_fs.n_sym_dgrams; n++) {
      if (dgram_fds[n] == fd)
        return &__exe_fs.sym_dgrams[n];
    }
  }
  return NULL;
}

static int return_address(pid_t child, long* addr, long* addrlen, socklen_t destlen, const void* src, socklen_t srclen)
{
  int res;

  if (destlen < srclen)
    return -EINVAL;

  res = ptrace(PTRACE_POKEDATA, child, addrlen, srclen);
  assert(res == 0);

  put_data(child, addr, src, srclen);

  return 0;
}


void process_socket(pid_t child, int before) {
  static int n_calls = 0;
  static int domain, type, protocol;
  long *args;

  if (before) {
    args = (long*) ptrace(PTRACE_PEEKUSER, child, KLEE_REGSIZE * KLEE_ECX, NULL);
    domain = ptrace(PTRACE_PEEKDATA, child, args, NULL);
    type = ptrace(PTRACE_PEEKDATA, child, args + 1, NULL);
    protocol = ptrace(PTRACE_PEEKDATA, child, args + 2, NULL);

    n_calls++;

    if (__exe_fs.max_failures && *__exe_fs.socket_fail && 
        n_calls == *__exe_fs.socket_fail) {
      //__exe_fs.max_failures--;
      skip = 2;
    }  

    if (type == SOCK_RAW || type == SOCK_PACKET) {
      // override type and protocol so that the superuser privilege is not required

      type = SOCK_DGRAM;
      int res = ptrace(PTRACE_POKEDATA, child, args + 1, (long) type);
      assert(res == 0);

      protocol = 0;
      res = ptrace(PTRACE_POKEDATA, child, args + 2, (long) protocol);
      assert(res == 0);
    }
  }
  else {
    if (__exe_fs.max_failures && *__exe_fs.socket_fail && 
        n_calls == *__exe_fs.socket_fail)
      return;

    int fd = ptrace(PTRACE_PEEKUSER, child, KLEE_REGSIZE * KLEE_EAX, NULL);
    assert(fd < MAX_TOTAL_FDS);
    fd_attrs[fd].socket = (domain == PF_INET || domain == PF_INET6 || domain == PF_PACKET);
    fd_attrs[fd].datagram = type != SOCK_STREAM;
  }
}


int assign_datagram(pid_t child) {
  int fd;

//fprintf(stderr, "__exe_fs.n_sym_dgrams_used = %d,  __exe_fs.n_sym_dgrams = %d\n", __exe_fs.n_sym_dgrams_used,  __exe_fs.n_sym_dgrams);

  if (__exe_fs.n_sym_dgrams_used >= __exe_fs.n_sym_dgrams) {
  //fprintf(stderr, "KLEE_REPLAY: datagrams exhausted\n");
    int res = ptrace(PTRACE_POKEUSER, child, KLEE_REGSIZE * KLEE_EAX, -ECONNREFUSED);
    assert(res == 0);
    return -1;
  }

  assert(__exe_fs.n_sym_dgrams_used < __exe_fs.n_sym_dgrams);
  fd = dgram_fds[__exe_fs.n_sym_dgrams_used];
  __exe_fs.n_sym_dgrams_used++;

  return fd;
}


void process_recv(pid_t child, int before) {
  static int fd = 0, flags = 0;
  static size_t len;
  static long *buf = NULL;
  long *args;

  if (before) {
    args = (long*) ptrace(PTRACE_PEEKUSER, child, KLEE_REGSIZE * KLEE_ECX, NULL);
    fd = ptrace(PTRACE_PEEKDATA, child, args, NULL);
    buf = (long*) ptrace(PTRACE_PEEKDATA, child, args+1, NULL);
    len = ptrace(PTRACE_PEEKDATA, child, args+2, NULL);
    flags = ptrace(PTRACE_PEEKDATA, child, args+3, NULL);

    read_n_calls++;

    if (__exe_fs.max_failures && *__exe_fs.read_fail && 
        read_n_calls == *__exe_fs.read_fail) {
      //__exe_fs.max_failures--;
      skip = 2;
    }

#if 0
    for (i=0; i < __exe_fs.n_sym_streams; i++) {
      if (fd == stream_fds[i])
	goto ok;      
    }
    fprintf(stderr, "KLEE_REPLAY: Invalid recv in client program! (fd=%d)\n", fd);
    exit(1);

  ok:
#endif

    if (!fd_attrs[fd].socket) {
      // execute the call and get an error, concretely
      return;
    }

    skip = 2;
  }
  else {
    if (__exe_fs.max_failures && *__exe_fs.read_fail && 
        read_n_calls == *__exe_fs.read_fail)
      return;

    if (!fd_attrs[fd].socket)
      return;

    if (fd_attrs[fd].datagram) {
      fd = assign_datagram(child);
      if (fd < 0) return;
    }

    read_into_buf(child, fd, buf, len);
  }
}


// XXX: right now we ignore from and fromlen fields!
void process_recvfrom(pid_t child, int before) {
  static int fd = 0, flags = 0;
  static size_t len;
  static long *buf = NULL;
  long *args;
  
  if (before) {
    args = (long*) ptrace(PTRACE_PEEKUSER, child, KLEE_REGSIZE * KLEE_ECX, NULL);
    fd = ptrace(PTRACE_PEEKDATA, child, args, NULL);
    buf = (long*) ptrace(PTRACE_PEEKDATA, child, args+1, NULL);
    len = ptrace(PTRACE_PEEKDATA, child, args+2, NULL);
    flags = ptrace(PTRACE_PEEKDATA, child, args+3, NULL);

    read_n_calls++;

    if (__exe_fs.max_failures && *__exe_fs.read_fail && 
        read_n_calls == *__exe_fs.read_fail) {
      //__exe_fs.max_failures--;
      skip = 2;
    }

    if (!fd_attrs[fd].socket) {
      // execute the call and get an error, concretely
      return;
    }

    skip = 2;
  }
  else {
    if (__exe_fs.max_failures && *__exe_fs.read_fail && 
        read_n_calls == *__exe_fs.read_fail)
      return;

    if (!fd_attrs[fd].socket)
      return;

    if (fd_attrs[fd].datagram) {
      fd = assign_datagram(child);
      if (fd < 0) return;
    }

    read_into_buf(child, fd, buf, len);
  }
}


void process_recvmsg(pid_t child, int before) {
  static int fd = 0, flags = 0;
  static struct msghdr *msgp, msg;
  static struct iovec iov;
  long *args;

  if (before) {
    args = (long*) ptrace(PTRACE_PEEKUSER, child, KLEE_REGSIZE * KLEE_ECX, NULL);
    fd = ptrace(PTRACE_PEEKDATA, child, args, NULL);
    msgp = (struct msghdr*) ptrace(PTRACE_PEEKDATA, child, args+1, NULL);
    flags = ptrace(PTRACE_PEEKDATA, child, args+2, NULL);

    read_n_calls++;

    if (__exe_fs.max_failures && *__exe_fs.read_fail && 
        read_n_calls == *__exe_fs.read_fail) {
      //__exe_fs.max_failures--;
      skip = 2;
    }

    get_data(child, (const long*) msgp, &msg, sizeof(msg));
    if (msg.msg_iovlen != 1) {
      fprintf(stderr, "\nKLEE_REPLAY: recvmsg() only currently handled for msg_iovlen = 1\n");
      skip = 0;
      return;
    }
    get_data(child, (const long*) msg.msg_iov, &iov, sizeof(iov));

    if (!fd_attrs[fd].socket) {
      // execute the call and get an error, concretely
      return;
    }

    skip = 2;
  }
  else {
    if (__exe_fs.max_failures && *__exe_fs.read_fail && 
        read_n_calls == *__exe_fs.read_fail)
      return;

    if (!fd_attrs[fd].socket)
      return;

    if (fd_attrs[fd].datagram) {
      fd = assign_datagram(child);
      if (fd < 0) return;
    }

    read_into_buf(child, fd, iov.iov_base, iov.iov_len);
  }
}


/* Reports that all bytes were correctly sent, but does nothing
   otherwise */
void process_send(pid_t child, int before) {
  static size_t len;
  long *args;
  static int fd;

  if (before) {
    args = (long*) ptrace(PTRACE_PEEKUSER, child, KLEE_REGSIZE * KLEE_ECX, NULL);
    fd = ptrace(PTRACE_PEEKDATA, child, args, NULL);
    len = ptrace(PTRACE_PEEKDATA, child, args+2, NULL);

    write_n_calls++;  

    if (__exe_fs.max_failures && *__exe_fs.write_fail && 
        write_n_calls == *__exe_fs.write_fail) {
      //__exe_fs.max_failures--;
      skip = 2;
    }

    if (fd_attrs[fd].socket)
      skip = 2;
  }
  else {
    if (__exe_fs.max_failures && *__exe_fs.write_fail && 
        write_n_calls == *__exe_fs.write_fail)
      return;

    if (fd_attrs[fd].socket) {
      int res = ptrace(PTRACE_POKEUSER, child, KLEE_REGSIZE * KLEE_EAX, len);
      assert(res == 0);
    }
  }
}


void process_listen(pid_t child) {
  static int n_calls = 0;
  long *args;
  int fd;

  n_calls++;

  if (__exe_fs.max_failures && *__exe_fs.listen_fail && 
      n_calls == *__exe_fs.listen_fail) {
    //__exe_fs.max_failures--;
    skip = 2;
    return;
  }  

  args = (long*) ptrace(PTRACE_PEEKUSER, child, KLEE_REGSIZE * KLEE_ECX, NULL);
  fd = ptrace(PTRACE_PEEKDATA, child, args, NULL);
  fd_attrs[fd].listening = 1;
}


void process_bind(pid_t child, int before) {
  static int n_calls = 0;
  long *args;
  static int fd;

  if (before) {
    args = (long*) ptrace(PTRACE_PEEKUSER, child, KLEE_REGSIZE * KLEE_ECX, NULL);
    fd = ptrace(PTRACE_PEEKDATA, child, args, NULL);

    n_calls++;

    if (__exe_fs.max_failures && *__exe_fs.bind_fail && 
        n_calls == *__exe_fs.bind_fail) {
      //__exe_fs.max_failures--;
      skip = 2;
    }

    if (fd_attrs[fd].socket)
      skip = 2;
  }
  else {
    if (__exe_fs.max_failures && *__exe_fs.bind_fail && 
        n_calls == *__exe_fs.bind_fail)
      return;

    if (fd_attrs[fd].socket) {
      int res = ptrace(PTRACE_POKEUSER, child, KLEE_REGSIZE * KLEE_EAX, 0);
      assert(res == 0);
    }
  }
}


// XXX: could be improved by making sure the socket fd is valid
void process_accept(pid_t child, int before) {
  static int n_calls = 0;
  static long* addr = NULL;
  static long* addrlen = NULL;
  static long len = 0;

  long* args;

  if (before) {
    args = (long*) ptrace(PTRACE_PEEKUSER, child, KLEE_REGSIZE * KLEE_ECX, NULL);
    addr = (long*) ptrace(PTRACE_PEEKDATA, child, args+1, NULL);
    addrlen = (long*) ptrace(PTRACE_PEEKDATA, child, args+2, NULL);
    len = ptrace(PTRACE_PEEKDATA, child, addrlen, NULL);

    n_calls++;

    if (__exe_fs.max_failures && *__exe_fs.accept_fail && 
        n_calls == *__exe_fs.accept_fail) {
      //__exe_fs.max_failures--;
      skip = 2;
    }

    skip = 2;
  }
  else {
    int res, ret, fd;

    if (__exe_fs.max_failures && *__exe_fs.accept_fail && 
        n_calls == *__exe_fs.accept_fail)
      return;

    if (__exe_fs.n_sym_streams_used >= __exe_fs.n_sym_streams) {
      res = ptrace(PTRACE_POKEUSER, child, KLEE_REGSIZE * KLEE_EAX, -ENFILE);
      assert(res == 0);
      return;
    } 
    
    if (addr) {
      assert(addrlen);
      ret = return_address(child, addr, addrlen, len,
            __exe_fs.sym_streams[__exe_fs.n_sym_streams_used].src->addr,
            sizeof(struct sockaddr_in)          // XXX: better to store size in .bout
          );
    }
    else
      ret = 0;

    if (ret == 0) {
      fd = stream_fds[__exe_fs.n_sym_streams_used++];
      fd_attrs[fd].socket = 1;
      ret = fd;
    }

    res = ptrace(PTRACE_POKEUSER, child, KLEE_REGSIZE * KLEE_EAX, ret);
    assert(res == 0);
  } 
}


void process_getsockname(pid_t child, int before) {
  long *args;
  static int fd;
  static long* addr;
  static long* addrlen;
  static const exe_disk_file_t* f;

  if (before) {
    args = (long*) ptrace(PTRACE_PEEKUSER, child, KLEE_REGSIZE * KLEE_ECX, NULL);
    fd = ptrace(PTRACE_PEEKDATA, child, args, NULL);
    addr = (long*) ptrace(PTRACE_PEEKDATA, child, args+1, NULL);
    addrlen = (long*) ptrace(PTRACE_PEEKDATA, child, args+2, NULL);

    f = find_exe_disk_file(fd);
    if (f)
      skip = 2;
  }
  else {
    int res, ret;
    if (f) {
      struct sockaddr_in local;           // make up a dummy local address
      memset(&local, 0, sizeof(local));
      local.sin_family = AF_INET;
      local.sin_port = ntohs(2000);
      local.sin_addr.s_addr = ntohl(0x7f000001);

      assert(fd_attrs[fd].socket);
      ret = return_address(child, addr, addrlen, ptrace(PTRACE_PEEKDATA, child, addrlen, NULL),
        &local, sizeof(local));
    }
    else
      ret = -EBADF;
    res = ptrace(PTRACE_POKEUSER, child, KLEE_REGSIZE * KLEE_EAX, ret);
    assert(res == 0);
  }
}

void process_getpeername(pid_t child, int before) {
  long *args;
  static int fd;
  static long* addr;
  static long* addrlen;
  static const exe_disk_file_t* f;

  if (before) {
    args = (long*) ptrace(PTRACE_PEEKUSER, child, KLEE_REGSIZE * KLEE_ECX, NULL);
    fd = ptrace(PTRACE_PEEKDATA, child, args, NULL);
    addr = (long*) ptrace(PTRACE_PEEKDATA, child, args+1, NULL);
    addrlen = (long*) ptrace(PTRACE_PEEKDATA, child, args+2, NULL);

    f = find_exe_disk_file(fd);
    if (f)
      skip = 2;
  }
  else {
    int res, ret;
    if (f) {
      assert(fd_attrs[fd].socket);
      assert(f->src);
      ret = return_address(child, addr, addrlen, ptrace(PTRACE_PEEKDATA, child, addrlen, NULL),
        f->src->addr, f->src->addrlen);
    }
    else
      ret = -EBADF;
    res = ptrace(PTRACE_POKEUSER, child, KLEE_REGSIZE * KLEE_EAX, ret);
    assert(res == 0);
  }
}

static void skip_and_ret_zero(pid_t child, int before) {
  if (before) {
    skip = 2;
  }
  else {
    int res = ptrace(PTRACE_POKEUSER, child, KLEE_REGSIZE * KLEE_EAX, 0);
    assert(res == 0);
  }
}

static const char* sock_sys_calls[18] = 
  {"", "SYS_SOCKET", "SYS_BIND", "SYS_CONNECT", "SYS_LISTEN", 
   "SYS_ACCEPT", "SYS_GETSOCKNAME", "SYS_GETPEERNAME", 
   "SYS_SOCKETPAIR", "SYS_SEND", "SYS_RECV", "SYS_SENDTO", 
   "SYS_RECVFROM", "SYS_SHUTDOWN", "SYS_SETSOCKOPT", 
   "SYS_GETSOCKOPT", "SYS_SENDMSG", "SYS_RECVMSG" };

void process_socketcall(pid_t child, int before) {
  static long type = 0;
  if (before)
    type = ptrace(PTRACE_PEEKUSER, child, KLEE_REGSIZE * KLEE_EBX, NULL);
  else
    assert(type);

  //if (before) fprintf(stderr, "KLEE_REPLAY: Processing %s\n", sock_sys_calls[type]);

  switch (type) {
  case SYS_SOCKET:
    process_socket(child, before);
    break;
  case SYS_ACCEPT:
    process_accept(child, before);
    break;
  case SYS_RECV:
    process_recv(child, before);
    break;
  case SYS_RECVFROM:
    process_recvfrom(child, before);
    break;
  case SYS_RECVMSG:
    process_recvmsg(child, before);
    break;
  case SYS_LISTEN:
    process_listen(child);
    break;
  case SYS_BIND:
    process_bind(child, before);
    break;
  case SYS_SETSOCKOPT:
    fprintf(stderr, "KLEE_REPLAY: Skipping %s\n",  sock_sys_calls[type]);
    skip_and_ret_zero(child, before);
    break;
  case SYS_SEND:
  case SYS_SENDTO:
    process_send(child, before);
    break;
  case SYS_GETSOCKNAME:
    process_getsockname(child, before);
    break;
  case SYS_GETPEERNAME:
    process_getpeername(child, before);
    break;
  default:
    if (before)
      fprintf(stderr, "KLEE_REPLAY: Executing concretely %s\n", 
              sock_sys_calls[type]);
  }
  
  if (!before)
    type = 0;
}
