#include "klee-replay.h"
#include "replay-fd.h"
#include "replay-socket.h"
#include "replay-util.h"

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/ptrace.h>
#include <asm/ptrace.h>
#include <net/if.h>

struct fd_attrs fd_attrs[MAX_TOTAL_FDS];
int read_n_calls = 0;
int write_n_calls = 0;

static int external_fname(const char* fname) {
  if ((strlen(fname) >= 5) && (strcmp(fname + strlen(fname) - 5, ".gcda") == 0))
    return 1;

  if (strstr(fname, ".so"))
    return 1;
   
  if (strstr(fname, "/locale/"))
    return 1;

  if (strstr(fname, "/valgrind"))
    return 1;

  if (strstr(fname, "/dev/urandom"))
    return 1;

  fprintf(stderr, "KLEE_REPLAY: Not ignoring file %s\n", fname);
  return 0;
}

static char *last_open = 0;
static int last_open_external;

void process_open(pid_t child) {
  static int n_calls = 0;

  unsigned* addr = (unsigned*) ptrace(PTRACE_PEEKUSER, child, KLEE_REGSIZE * KLEE_EBX, NULL);
  last_open = get_string(child, addr);
  last_open_external = external_fname(last_open);
  if (last_open_external)
    return;

  n_calls++;
  
  if (__exe_fs.max_failures && *__exe_fs.open_fail && 
      n_calls == *__exe_fs.open_fail) {
    //__exe_fs.max_failures--;
    skip = 2;
  }  
}

void process_open_done(pid_t child) {
  assert(last_open);
  int fd = ptrace(PTRACE_PEEKUSER, child, KLEE_REGSIZE * KLEE_EAX, NULL);
  assert(fd < MAX_TOTAL_FDS);
  fd_attrs[fd].socket = 0;
  fd_attrs[fd].external = last_open_external;
//if (last_open_external) fprintf(stderr, "Ignoring file %s\n", last_open);
  free(last_open);
  last_open = 0;
}


void process_close(pid_t child) {
  static int n_calls = 0;

  int fd = ptrace(PTRACE_PEEKUSER, child, KLEE_REGSIZE * KLEE_EBX, NULL);
  assert(fd < MAX_TOTAL_FDS);
  
  if (fd_attrs[fd].external) {
    fd_attrs[fd].external = 0;
    return;
  }

  n_calls++;
  
  if (__exe_fs.max_failures && *__exe_fs.close_fail && 
      n_calls == *__exe_fs.close_fail) {
    //__exe_fs.max_failures--;
    skip = 2;
  }  
}


void process_read(pid_t child, int before) {
  static int fd;
  static long *buf = NULL;
  static size_t len;

  if (before) {
    fd = ptrace(PTRACE_PEEKUSER, child, KLEE_REGSIZE * KLEE_EBX, NULL);
    buf = (long*) ptrace(PTRACE_PEEKUSER, child, KLEE_REGSIZE * KLEE_ECX, NULL);
    len = ptrace(PTRACE_PEEKUSER, child, KLEE_REGSIZE * KLEE_EDX, NULL);
    assert(fd < MAX_TOTAL_FDS);

    if (fd_attrs[fd].external) {
      return;
    }

    read_n_calls++;

    if (__exe_fs.max_failures && *__exe_fs.read_fail && 
        read_n_calls == *__exe_fs.read_fail) {
      //__exe_fs.max_failures--;
      skip = 2;
    }

    if (fd_attrs[fd].socket)
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


void process_write(pid_t child) {
  int fd = ptrace(PTRACE_PEEKUSER, child, KLEE_REGSIZE * KLEE_EBX, NULL);
  assert(fd < MAX_TOTAL_FDS);

  if (fd_attrs[fd].external) {
    return;
  }
  
  write_n_calls++;  

  if (__exe_fs.max_failures && *__exe_fs.write_fail && 
      write_n_calls == *__exe_fs.write_fail) {
    //__exe_fs.max_failures--;
    skip = 2;
  }
}


void process_dup(pid_t child, int before) {
  static int oldfd;

  if (before) {
    oldfd = ptrace(PTRACE_PEEKUSER, child, KLEE_REGSIZE * KLEE_EBX, NULL);
    printf("captured oldfd=%d\n", oldfd);
  }
  else {
    int newfd = ptrace(PTRACE_PEEKUSER, child, KLEE_REGSIZE * KLEE_EAX, NULL);
    if (newfd >= 0) {
      assert(oldfd < MAX_TOTAL_FDS);
      assert(newfd < MAX_TOTAL_FDS);

      printf("copying from %d to %d\n", oldfd, newfd);
      fd_attrs[newfd] = fd_attrs[oldfd];
    }
  }
}


void process_chmod(pid_t child) {
  static int n_calls = 0;

  n_calls++;

  if (__exe_fs.max_failures && *__exe_fs.chmod_fail && 
      n_calls == *__exe_fs.chmod_fail) {
    //__exe_fs.max_failures--;
    skip = 2;
  }
}


void process_fchmod(pid_t child) {
  static int n_calls = 0;

  int fd = ptrace(PTRACE_PEEKUSER, child, KLEE_REGSIZE * KLEE_EBX, NULL);
  assert(fd < MAX_TOTAL_FDS);

  if (fd_attrs[fd].external) {
    return;
  }

  n_calls++;

  if (__exe_fs.max_failures && *__exe_fs.fchmod_fail && 
      n_calls == *__exe_fs.fchmod_fail) {
    //__exe_fs.max_failures--;
    skip = 2;
  }
}


void process_ftruncate(pid_t child) {
  static int n_calls = 0;

  int fd = ptrace(PTRACE_PEEKUSER, child, KLEE_REGSIZE * KLEE_EBX, NULL);
  assert(fd < MAX_TOTAL_FDS);

  if (fd_attrs[fd].external) {
    return;
  }

  n_calls++;

  if (__exe_fs.max_failures && *__exe_fs.ftruncate_fail && 
      n_calls == *__exe_fs.ftruncate_fail) {
    //__exe_fs.max_failures--;
    skip = 2;
  }
}

void process_getcwd(pid_t child) {
  static int n_calls = 0;

  n_calls++;

  if (__exe_fs.max_failures && *__exe_fs.getcwd_fail && 
      n_calls == *__exe_fs.getcwd_fail) {
    //__exe_fs.max_failures--;
    skip = 2;
  }
}

void process_kill(pid_t child) {
  // int pid = ptrace(PTRACE_PEEKUSER, child, KLEE_REGSIZE * KLEE_EBX, NULL);
  // int signal = ptrace(PTRACE_PEEKUSER, child, KLEE_REGSIZE * KLEE_ECX, NULL);
  
  // Originally we tried just skipping the blanket kill system
  // calls. Unfortunately, busyboxy has some super smart artificially
  // intelligent programs which I guess look at the whole process list
  // and kill things by pid. This is a problem, since they tend to
  // kill us and our timing and piping friends. So now we just disable
  // kill.

  //if (pid == 0 || pid == -1)
  //  skip = 2;
  skip = 2;
}

void process_chroot(pid_t child) {
  /* This is simulating a process that runs with CAP_SYS_CHROOT
     capability (e.g., running as root) */
  unsigned* addr = (unsigned*) ptrace(PTRACE_PEEKUSER, child, KLEE_REGSIZE * KLEE_EBX, NULL);
  char* path = get_string(child, addr);
  if (strcmp(path, "/") == 0)
    skip = 2;
  free(path);
}


// XXX: right now supports readfds only (and ignores {write,except}fds, timeout) 
void process_select(pid_t child, int before) {
  static int n_calls = 0;
  static int nfds;
  static long *readfds_p /*, *writefds_p, *exceptfds_p */;
  static fd_set in_readfds /*, in_writefds, in_exceptfds */;
  static fd_set out_readfds /*, out_writefds, out_exceptfds */;
/*static struct timeval *timeout;*/
  static int external_nfds, count;
  static fd_set os_readfds;

  if (before) {
    int i;
    external_nfds = 0;
    count = 0;

    nfds = ptrace(PTRACE_PEEKUSER, child, KLEE_REGSIZE * KLEE_EBX, NULL);
    readfds_p = (long *) ptrace(PTRACE_PEEKUSER, child, KLEE_REGSIZE * KLEE_ECX, NULL);
    if (readfds_p)
      get_data(child, readfds_p, &in_readfds, sizeof(in_readfds));
  /*writefds = (fd_set *) ptrace(PTRACE_PEEKUSER, child, KLEE_REGSIZE * KLEE_EDX, NULL);*/
  /*exceptfds = (fd_set *) ptrace(PTRACE_PEEKUSER, child, KLEE_REGSIZE * ESI, NULL);*/
  /*timeout = (struct timeval *) ptrace(PTRACE_PEEKUSER, child, KLEE_REGSIZE * EDI, NULL);*/

    n_calls++;

    if (__exe_fs.max_failures && *__exe_fs.select_fail && 
        n_calls == *__exe_fs.select_fail) {
      //__exe_fs.max_failures--;
      skip = 2;
      return;
    }

    FD_ZERO(&out_readfds);
    FD_ZERO(&os_readfds);

    for (i = 0; i < nfds; ++i) {
      unsigned flags = 0;
      if (readfds_p && FD_ISSET(i, &in_readfds)) {
        if (fd_attrs[i].external) {
          external_nfds = i + 1;
          FD_SET(i, &os_readfds);
        }
        else if (!fd_attrs[i].socket)
          flags |= 01;
        else if (fd_attrs[i].datagram)
          flags |= (__exe_fs.n_sym_dgrams_used  < __exe_fs.n_sym_dgrams)  ? 01 : 0; /* more dgrams available */
        else if (fd_attrs[i].listening)
          flags |= (__exe_fs.n_sym_streams_used < __exe_fs.n_sym_streams) ? 01 : 0; /* more streams available */
        else
          flags |= 01;
        if (flags & 01) FD_SET(i, &out_readfds);
      }
      if (flags) ++count;
    }

    if (external_nfds > 0) {
    //fprintf(stderr, "KLEE_REPLAY: running select concretely\n");
      ptrace(PTRACE_POKEUSER, child, KLEE_REGSIZE * KLEE_EBX, &os_readfds);
    }
    else {
      if (readfds_p)
        put_data(child, readfds_p, &out_readfds, sizeof(out_readfds));
      skip = 2;
    }
  }
  else {
    if (__exe_fs.max_failures && *__exe_fs.select_fail && 
        n_calls == *__exe_fs.select_fail)
      return;

    if (external_nfds > 0)
      return; /* concrete */

    int res = ptrace(PTRACE_POKEUSER, child, KLEE_REGSIZE * KLEE_EAX, count);
    assert(res == 0);
  }
}


void process_ioctl(pid_t child, int before) {
  static int fd;
  static int req;
  static void *argp;
  static int ret;

  if (before) {
    fd = ptrace(PTRACE_PEEKUSER, child, KLEE_REGSIZE * KLEE_EBX, NULL);
    req = ptrace(PTRACE_PEEKUSER, child, KLEE_REGSIZE * KLEE_ECX, NULL);
    argp = (void *) ptrace(PTRACE_PEEKUSER, child, KLEE_REGSIZE * KLEE_EDX, NULL);

    if (fd_attrs[fd].external)
      return;

    if (fd_attrs[fd].socket) {
      switch (req) {
      case SIOCGIFINDEX: {
        int r = 0;
        put_data(child, (long *) ((char *) argp + offsetof(struct ifreq, ifr_ifindex)), &r, sizeof(r));
        ret = 0;
        skip = 2;
        break;
      }
      case SIOCGIFHWADDR: {
        char (*ia)[14] = (char (*)[14]) ((char *) argp + offsetof(struct ifreq, ifr_hwaddr.sa_data));
        struct ifreq ifr;
        get_data(child, argp, &ifr, sizeof(ifr));
        set_data(child, (long *) ia, strncmp(ifr.ifr_name, "lo", IFNAMSIZ) != 0 ? 0xdd : 0, sizeof(*ia));
        ret = 0;
        skip = 2;
        break;
      }
      case SIOCGIFADDR: {
        struct sockaddr *psa = (struct sockaddr *) ((char *) argp + offsetof(struct ifreq, ifr_addr)), sa;
        get_data(child, (long *) psa, &sa, sizeof(sa));
        switch (sa.sa_family) {
        default: {
          sa.sa_family = AF_INET;
          put_data(child, (long *) psa, &sa, sizeof(sa));
          /* fall through */
        }
        case AF_INET: {
          struct in_addr *ia = (struct in_addr *) ((char *) psa + offsetof(struct sockaddr_in, sin_addr));
          set_data(child, (long *) ia, 0xdd, sizeof(*ia));
          ret = 0;
          break;
        }
        case AF_INET6: {
          struct in6_addr *ia = (struct in6_addr *) ((char *) psa + offsetof(struct sockaddr_in6, sin6_addr));
          set_data(child, (long *) ia, 0xdd, sizeof(*ia));
          ret = 0;
          break;
        }
        }
        skip = 2;
        break;
      }
      case SIOCGIFFLAGS: {
        struct ifreq ifr;
        get_data(child, argp, &ifr, sizeof(ifr));
        if (strncmp(ifr.ifr_name, "lo", IFNAMSIZ) == 0)
          ifr.ifr_flags = IFF_UP | IFF_RUNNING | IFF_LOOPBACK;
        else if (strncmp(ifr.ifr_name, "eth", 3) == 0)
          ifr.ifr_flags = IFF_UP | IFF_RUNNING | IFF_BROADCAST | IFF_MULTICAST;
        else {
          errno = EBADF;
          ret = -1;
          skip = 2;
          break;
        }

        put_data(child, argp, &ifr, sizeof(ifr));
        ret = 0;
        skip = 2;
        break;
      }
      case SIOCGIFCONF: {
        struct ifconf ifc;

        get_data(child, argp, &ifc, sizeof(ifc));
        if (ifc.ifc_len < 0 || (size_t)ifc.ifc_len < 3 * sizeof(struct ifreq)) {
          errno = EINVAL;
          ret = -1;
          skip = 2;
          break;
        }

        struct ifreq ifrs[3], *ifr;
        struct sockaddr_in* sin;

        ifr = ifrs;
        strncpy(ifr->ifr_name, "lo", IFNAMSIZ);
        memset(&ifr->ifr_addr, '\0', sizeof(ifr->ifr_addr));
        sin = (struct sockaddr_in*) &ifr->ifr_addr;
        sin->sin_family = AF_INET;
        sin->sin_addr.s_addr = htonl(0x7f000001);

        ++ifr;
        strncpy(ifr->ifr_name, "eth0", IFNAMSIZ);
        memset(&ifr->ifr_addr, '\0', sizeof(ifr->ifr_addr));
        sin = (struct sockaddr_in*) &ifr->ifr_addr;
        sin->sin_family = AF_INET;
        memset(&sin->sin_addr.s_addr, 0xdd, sizeof(sin->sin_addr.s_addr));

        ++ifr;
        strncpy(ifr->ifr_name, "eth1", IFNAMSIZ);
        memset(&ifr->ifr_addr, '\0', sizeof(ifr->ifr_addr));
        sin = (struct sockaddr_in*) &ifr->ifr_addr;
        sin->sin_family = AF_INET;
        memset(&sin->sin_addr.s_addr, 0xee, sizeof(sin->sin_addr.s_addr));

        ifc.ifc_len = 3 * sizeof(struct ifreq);
        put_data(child, (long *) ((char *) argp + offsetof(struct ifconf, ifc_len)), &ifc.ifc_len, sizeof(ifc.ifc_len));
        put_data(child, (long *) ifc.ifc_req, &ifrs, sizeof(ifrs));
        skip = 2;
        break;
      }
      default:
        errno = EINVAL;
        ret = -1;
        skip = 2;
      } /* switch */
    }
  }
  else {
    if (skip) {
      int res = ptrace(PTRACE_POKEUSER, child, KLEE_REGSIZE * KLEE_EAX, ret);
      assert(res == 0);
    }
  }
}
