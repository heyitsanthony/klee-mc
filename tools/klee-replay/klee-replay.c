//===-- klee-replay.c -----------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "klee-replay.h"

#include "klee/Internal/ADT/KTest.h"

#include "replay-util.h"
#include "replay-fd.h"
#include "replay-socket.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>

#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/signal.h>
#include <sys/wait.h>
#include <sys/user.h>
#include <sys/syscall.h>
#include <sys/ptrace.h>
#include <asm/ptrace.h>

static void __emit_error(const char *msg);

static KTest* input;
static unsigned obj_index;

static const char *progname = 0;
static pid_t    monitored_pid = 0;    
static unsigned monitored_timeout;

int skip = 0;
int randomize = 0;

static void stop_monitored(pid_t process, int use_patch, long patch_code) {
  fprintf(stderr, "TIMEOUT: ATTEMPTING GDB EXIT\n");
  pid_t pid = fork();
  if (pid < 0) {
    fprintf(stderr, "ERROR: gdb_exit: fork failed\n");
  } else if (pid == 0) {
    /* Run gdb in a child process. */
    const char *gdbargs[] = {
      "/usr/bin/gdb",
      "--pid", "",
      "-q",
      "--batch",
      "--eval-command=call exit(1)",
      0,
      0
    };
    char pids[64], patch_cmd[256];

    sprintf(pids, "%d", process);
    gdbargs[2] = pids;

    if (use_patch) {
      sprintf(patch_cmd, "--eval-command=print *((long*) $" KLEE_IP_NAME ") = %#08lx", patch_code);
      gdbargs[6] = gdbargs[5];
      gdbargs[5] = patch_cmd;
    }

    /* Make sure gdb doesn't talk to the user */
    close(0);

    fprintf(stderr, "RUNNING GDB: ");
    const char* const* p;
    for (p = gdbargs; *p; ++p)
      fprintf(stderr, "%s ", *p);
    fprintf(stderr, "\n");

    execvp(gdbargs[0], (char * const *) gdbargs);
    perror("execvp");
    _exit(66);
  } else {
    /* Parent process, wait for gdb to finish. */
    pid_t pid_w;
    int status;

    do {
      pid_w = waitpid(pid, &status, 0);
    } while (pid_w < 0 && errno == EINTR);

    if (pid_w < 0) {
      perror("waitpid");
      _exit(66);
    }
  }
}

static void int_handler(int signal) {
  fprintf(stderr, "%s: Received signal %d.  Killing monitored process(es)\n", 
          progname, signal);
  if (monitored_pid) {
    stop_monitored(monitored_pid, 0, 0);
    /* Kill the process group of monitored_pid.  Since we called
       setpgrp() for pid, this will not kill us, or any of our
       ancestors */
    kill(-monitored_pid, SIGKILL);
  } else {
    _exit(99);
  }
}
static void timeout_handler(int signal) {
  fprintf(stderr, "%s: EXIT STATUS: TIMED OUT (%d seconds)\n", progname, 
          monitored_timeout);
  if (monitored_pid) {
    stop_monitored(monitored_pid, 0, 0);
    /* Kill the process group of monitored_pid.  Since we called
       setpgrp() for pid, this will not kill us, or any of our
       ancestors */
    kill(-monitored_pid, SIGKILL);
  } else {
    _exit(88);
  }
}

void process_status(int status,
                    time_t elapsed, 
                    const char *pfx) {
  fprintf(stderr, "%s: ", progname);
  if (pfx)
    fprintf(stderr, "%s: ", pfx);
  if (WIFSIGNALED(status)) {
    fprintf(stderr, "EXIT STATUS: CRASHED signal %d (%d seconds)\n",
            WTERMSIG(status), (int) elapsed);
    _exit(77);
  } else if (WIFEXITED(status)) {
    int rc = WEXITSTATUS(status);

    char msg[64];
    if (rc == 0) {
      strcpy(msg, "NORMAL");
    } else {
      sprintf(msg, "ABNORMAL %d", rc);
    }
    fprintf(stderr, "EXIT STATUS: %s (%d seconds)\n", msg, (int) elapsed);
    _exit(rc);
  } else {
    fprintf(stderr, "EXIT STATUS: NONE (%d seconds)\n", (int) elapsed);
    _exit(0);
  }
}

static void process_syscall(pid_t child) {
  long res, orig_eax = ptrace(PTRACE_PEEKUSER, child, KLEE_REGSIZE * KLEE_ORIG_EAX, NULL);

  if (orig_eax < 0 && errno != 0) {
    fprintf(stderr, "PEEKUSER failed with error '%s'\n", strerror(errno));
    exit(1);
  }
//printf("orig_eax = %d\n", (unsigned char)orig_eax);

  skip = 0;
  switch (orig_eax) {
  case SYS_open: process_open(child); break;
  case SYS_close: process_close(child); break;
  case SYS_write: process_write(child); break;
#if __WORDSIZE != 64
  case SYS__newselect:
#endif
  case SYS_select: process_select(child, 1); break;
  case SYS_read: process_read(child, 1); break;
  case SYS_dup: process_dup(child, 1); break;
  case SYS_dup2: process_dup(child, 1); break;
  case SYS_chmod: process_chmod(child); break;
  case SYS_fchmod: process_fchmod(child); break;
  case SYS_ftruncate: process_ftruncate(child); break;
  case SYS_getcwd: process_getcwd(child); break;
  case SYS_kill: process_kill(child); break;
  case SYS_chroot: process_chroot(child); break;
#ifdef __i386__
  case SYS_socketcall: process_socketcall(child, 1); break;
#else
// !!! DAR: IMPLEMENT !!!
#endif
  case SYS_ioctl: process_ioctl(child, 1); break;

    // Just go to the continue so that the exit will be picked up by
    // waitpid in the main loop.
  case SYS_exit_group: goto sys_cont; break;
  }

  //fprintf(stderr, "KLEE-REPLAY: SYSCALL: %ld \n", orig_eax);
  if (skip) {
    // fprintf(stderr, "...skipping %ld\n", orig_eax);
    res = ptrace(PTRACE_POKEUSER, child, KLEE_REGSIZE * KLEE_ORIG_EAX, SYS_getpid);
    assert(res == 0);
  }
  
  res = ptrace(PTRACE_SINGLESTEP, child, NULL, NULL);
  assert(res == 0);
  int status;
  while (1) {
    pid_t pid_w = waitpid(child, &status, 0);
    if (pid_w >= 0)
      break;
    
    if (errno != EINTR) {
      perror("waitpid");
      break;
    }
  }
  assert(WIFSTOPPED(status));
  
  if (skip) {
    res = ptrace(PTRACE_POKEUSER, child, KLEE_REGSIZE * KLEE_EAX, -EIO);
    assert(res == 0);
  }
  
  switch (orig_eax) {
  case SYS_open: process_open_done(child); break;
  case SYS_read: process_read(child, 0); break;
#if __WORDSIZE != 64
  case SYS__newselect:
#endif
  case SYS_select: process_select(child, 0); break;
#ifdef __i386__
  case SYS_socketcall: process_socketcall(child, 0); break;
#else
// !!! DAR: IMPLEMENT !!!
#endif
  case SYS_dup: process_dup(child, 0); break;
  case SYS_dup2: process_dup(child, 0); break;
  case SYS_ioctl: process_ioctl(child, 0); break;
  }

 sys_cont:
  //  fprintf(stderr, "continuing\n");
  res = ptrace(PTRACE_SYSCALL, child, 0, 0);
  assert(res == 0);
}

static void run_monitored(char *executable, int argc, char **argv) {
  pid_t pid;
  const char *t = getenv("KLEE_REPLAY_TIMEOUT");  
  if (!t)
    t = "10000000";  
  monitored_timeout = atoi(t);
  
  if (monitored_timeout==0) {
    fprintf(stderr, "ERROR: invalid timeout (%s)\n", t);
    _exit(1);
  }

  /* Kill monitored process(es) on SIGINT and SIGTERM */
  signal(SIGINT, int_handler);
  signal(SIGTERM, int_handler);
  
  signal(SIGALRM, timeout_handler);
  pid = fork();
  if (pid < 0) {
    perror("fork");
    _exit(66);
  } else if (pid == 0) {
    /* This process actually executes the target program.
     *  
     * Create a new process group for pid, and the process tree it may spawn. We
     * do this, because later on we might want to kill pid _and_ all processes
     * spawned by it and its descendants.
     */
    setpgrp();

    long res = ptrace(PTRACE_TRACEME, 0, NULL, NULL);
    if (res < 0) {
      perror("ptrace");
      _exit(66);
    }

    execv(executable, argv);
    perror("execv");
    _exit(66);
  } else {
    /* Parent process which monitors the child. */
    int status;
    time_t start = time(0);
    sigset_t masked;

    sigemptyset(&masked);
    sigaddset(&masked, SIGALRM);

    monitored_pid = pid;
    alarm(monitored_timeout);
    while (1) {
      pid_t pid_w = waitpid(pid, &status, 0);
      if (pid_w < 0) {
        if (errno != EINTR) {
          perror("waitpid");
          _exit(66);
        }
      }

      if (WIFSTOPPED(status)) {
        //        sigprocmask(SIG_BLOCK, &masked, 0);
        int sig = WSTOPSIG(status);
        // SYSGOOD isn't reliable ?
        if (sig == SIGTRAP || (sig & 0x80)) { // syscall
          process_syscall(pid);
        } else {
          // continue with signal delivery
          if (sig==SIGTERM || sig==SIGABRT || sig==SIGSEGV) {
            fprintf(stderr, "KLEE_REPLAY: received signal: %d\n", sig);

            /* If the progress crashes then we want to make sure that
               the gcov output gets written. This is normally done in
               the exit handlers, and we don't have an easy way to run
               those from outside the process.

               The simple but gross strategy is to attach gdb and use
               it to call exit, which will call the exit
               handlers. However, before we do that we have to make
               sure to detach.

               The problem is that if the program actually crashed,
               and we simply detach, there is a race condition of
               whether the program will crash again (and exit w/o
               writing gcov) before gdb manages to attach. We deal
               with this by writing an infinite loop (0xFEEB on x86)
               at the current instruction pointer.

               However, because this infinite loop might be written
               inside a libc routine which is used during writing the
               gcov, we make sure to restore this instruction by
               writing it back once we are in gdb.
            */

            struct user_regs_struct regs;
            ptrace(PTRACE_GETREGS, pid, 0, &regs);
            long prev_inst = ptrace(PTRACE_PEEKTEXT, pid, KLEE_IP(regs), NULL);
            ptrace(PTRACE_POKETEXT, pid, KLEE_IP(regs), 0xFEEB);
            fprintf(stderr, "KLEE-REPLAY: wrote inf loop at: %lx (prev: %#08lx)\n", KLEE_IP(regs), prev_inst);
            fprintf(stderr, "KLEE-REPLAY: detaching\n");
            long res = ptrace(PTRACE_DETACH, pid, 0, 0);
            if (res < 0) {
              fprintf(stderr, "KLEE-REPLAY: detach failed: %ld %d\n", res, errno);
              perror("ptrace detach");
            }
            stop_monitored(pid, 1, prev_inst);

            /* If calling exit from gdb did not succeed, we send a
               SIGKILL here.  This also kills any processes that the
               monitored process may have spawned.  Since we called
               setpgrp() for pid, this will not kill us, or any of our
               ancestors */
            kill(-pid, SIGKILL);
          } else {
            fprintf(stderr, "KLEE-REPLAY: delivering signal: %d\n", sig);
            ptrace(PTRACE_SYSCALL, pid, 0, sig);
          }
        }
        //        sigprocmask(SIG_UNBLOCK, &masked, 0);
      }
      else {
        break;
      }
    }

    /* Just in case, kill the process group of pid.  Since we called setpgrp()
       for pid, this will not kill us, or any of our ancestors */
    kill(-pid, SIGKILL);
    process_status(status, time(0) - start, 0);
  }
}

static void usage(void) {
  fprintf(stderr, "Usage: %s <executable> { <ktest-files> }\n", progname);
  fprintf(stderr, "   or: %s --create-files-only <ktest-file>\n", progname);
  fprintf(stderr, "   or: %s --randomize <executable> <ktest-file>\n", progname);
  fprintf(stderr, "\n");
  fprintf(stderr, "Set KLEE_REPLAY_TIMEOUT environment variable to set a timeout (in seconds).\n");
  exit(1);
}

int main(int argc, char** argv) {
  int prg_argc;
  char ** prg_argv;  

  progname = argv[0];

  if (argc < 3)
    usage();

  /* Special case hack for only creating files and not actually executing the
   * program.
   */
  if (strcmp(argv[1], "--create-files-only") == 0) {
    if (argc != 3)
      usage();

    char* input_fname = argv[2];

    input = kTest_fromFile(input_fname);
    if (!input) {
      fprintf(stderr, "%s: error: input file %s not valid.\n", progname,
              input_fname);
      exit(1);
    }
    
    prg_argc = input->numArgs;
    prg_argv = input->args;
    prg_argv[0] = argv[1];
    klee_init_env(&prg_argc, &prg_argv);
    
    replay_create_files(&__exe_fs);
    return 0;
  }

  /* Normal execution path ... */

  if (strcmp(argv[1], "--randomize") == 0) {
    --argc;
    ++argv;
    if (argc != 3)
      usage();

    randomize = 1;
    struct timeval now;
    gettimeofday(&now, 0);
    srand(now.tv_usec);
  }

  char* executable = argv[1];
  
  /* Verify the executable exists. */
  FILE *f = fopen(executable, "r");
  if (!f) {
    fprintf(stderr, "Error: executable %s not found.\n", executable);
    exit(1);
  }
  fclose(f);

  int idx = 0;
  for (idx = 2; idx != argc; ++idx) {
    char* input_fname = argv[idx];
    unsigned i;
    
    input = kTest_fromFile(input_fname);
    if (!input) {
      fprintf(stderr, "%s: error: input file %s not valid.\n", progname, 
              input_fname);
      exit(1);
    }
    
    obj_index = 0;
    prg_argc = input->numArgs;
    prg_argv = input->args;
    prg_argv[0] = argv[1];
    klee_init_env(&prg_argc, &prg_argv);

    if (idx > 2)
      fprintf(stderr, "\n");
    fprintf(stderr, "%s: TEST CASE: %s\n", progname, input_fname);
    fprintf(stderr, "%s: ARGS: ", progname);
    for (i=0; i != (unsigned) prg_argc; ++i)
      fprintf(stderr, "\"%s\" ", prg_argv[i]); 
    fprintf(stderr, "\n");

    /* Run the test case machinery in a subprocess, eventually this parent
       process should be a script or something which shells out to the actual
       execution tool. */
    pid_t pid = fork();
    if (pid < 0) {
      perror("fork");
      _exit(66);
    } else if (pid == 0) {
      /* Create the input files, pipes, etc., and run the process. */
      replay_create_files(&__exe_fs);
      open_socket_files();
      run_monitored(executable, prg_argc, prg_argv);
      _exit(0);
    } else {
      /* Wait for the test case. */
      pid_t pid_w;
      int status;

      do {
        pid_w = waitpid(pid, &status, 0);
      } while (pid_w < 0 && errno == EINTR);
      
      if (pid_w < 0) {
        perror("waitpid");
        _exit(66);
      }

      replay_delete_files(&__exe_fs);
    }
  }

  return 0;
}


/* Klee functions */

int __fputc_unlocked(int c, FILE *f) {
  return fputc_unlocked(c, f);
}

int __fgetc_unlocked(FILE *f) {
  return fgetc_unlocked(f);
}

int klee_get_errno() {
  return errno;
}

void klee_warning(char *name) {
  fprintf(stderr, "WARNING: %s\n", name);
}

void klee_warning_once(char *name) {
  fprintf(stderr, "WARNING: %s\n", name);
}

int klee_assume(uint64_t x) {
  if (!x) {
    fprintf(stderr, "WARNING: klee_assume(0)!\n");
  }
  return 0;
}

int klee_is_symbolic(uint64_t x) {
  return 0;
}

void klee_prefer_cex(void *buffer, uint64_t condition) {
  ;
}

void fill_randomized(void *addr, size_t nbytes) {
  unsigned char *i = addr, *e = i + nbytes;
  while (i != e)
    *i++ = rand() & 0xff;
}

void klee_make_symbolic(void *addr, size_t nbytes, const char *name) {
  if (randomize) {
    char* tn;
    if (strcmp("model_version", name) == 0) {
      assert(nbytes == 4);
      *((int*) addr) = 2;
    }
    else if (tn = strchr(name, '\0'), tn - name > 5 && strcmp(tn - 5, "_fail") == 0) {
      assert(nbytes == 4);
      *((int*) addr) = 0;
    }
    else if (tn - name > 5 && strcmp(tn - 5, "-name") == 0) {
      char* s = addr;
      if (strncmp(name, "FILE", 4) == 0) {
        assert(nbytes >= KLEE_MAX_PATH_LEN);
        size_t i;
        for (i = 0; i < KLEE_MAX_PATH_LEN - 1; ++i)
          s[i] = rand() % 26 + 'A';
        s[i] = '\0';
      }
      else if (
        strncmp(name, "STDIN", 5) == 0 ||
        strncmp(name, "STDOUT", 6) == 0 ||
        strncmp(name, "STREAM", 6) == 0 ||
        strncmp(name, "DGRAM", 5) == 0
      ) {
        size_t len = tn - 5 - name;
        assert(nbytes >= len + 1);
        strncpy(s, name, len);
        s[len] = '\0';
      }
    }
    else if (tn - name > 5 && strcmp(tn - 5, "-stat") == 0) {
      assert(nbytes == sizeof(struct stat64));
      struct stat64 defaults, *s = addr;

      stat64(".", &defaults);

      s->st_mode &= (S_IFMT | 0777);
      s->st_dev = defaults.st_dev;
      s->st_rdev = defaults.st_rdev;
      s->st_mode &= ~0777;
      s->st_mode |=  0622;
      s->st_mode &= ~S_IFMT;
      if (strncmp(name, "STREAM", 6) == 0 || strncmp(name, "DGRAM", 5) == 0)
        s->st_mode |= S_IFSOCK;
      else
        s->st_mode |=  S_IFREG;
      s->st_nlink = 1;
      s->st_uid = defaults.st_uid;
      s->st_gid = defaults.st_gid;
      s->st_blksize = 4096;
      s->st_atime = defaults.st_atime;
      s->st_mtime = defaults.st_mtime;
      s->st_ctime = defaults.st_ctime;
    }
    else
      fill_randomized(addr, nbytes);

    return;
  }

  /* XXX remove model version code once new tests gen'd */
  if (obj_index >= input->numObjects) {
    if (strcmp("model_version", name) == 0) {
      assert(nbytes == 4);
      *((int*) addr) = 0;
    } else {
      __emit_error("ran out of appropriate inputs");
    }
  } else {
    KTestObject *boo = &input->objects[obj_index];

    if (strcmp("model_version", name) == 0 &&
        strcmp("model_version", boo->name) != 0) {
      assert(nbytes == 4);
      *((int*) addr) = 0;
    } else {
      if (boo->numBytes != nbytes) {
        fprintf(stderr, "make_symbolic mismatch, different sizes: "
                "%d in input file, %zu in code\n", boo->numBytes, nbytes);
        exit(1);
      } else {
        memcpy(addr, boo->bytes, nbytes);
        obj_index++;
      }
    }
  }
}

/* Redefined here so that we can check the value read. */
int klee_range(int64_t min, int64_t max, const char* name) {
  int64_t r;  
  if (randomize) {
    r = rand() % (max - min + 1) + min;
    return r;
  }
  klee_make_symbolic(&r, sizeof r, name); 

  if (r < min || r >= max) {
    fprintf(stderr,
        "klee_range(%"PRIu64", %"PRIu64", %s) returned invalid result:"
        "%"PRIu64"\n", min, max, name, r);
    exit(1);
  }
  return r;
}

void klee_report_error(const char *file, int line, 
                       const char *message, const char *suffix) {
  __emit_error(message);
}

void klee_mark_global(void *object) {
  ;
}

/*** HELPER FUNCTIONS ***/

static void __emit_error(const char *msg) {
  fprintf(stderr, "ERROR: %s\n", msg);
  exit(1);
}
