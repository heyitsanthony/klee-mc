//===-- fd_init.c ---------------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#define _LARGEFILE64_SOURCE
#define _FILE_OFFSET_BITS 64
#include "fd.h"
#include <klee/klee.h>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>


exe_file_system_t __exe_fs;

/* NOTE: It is important that these are statically initialized
   correctly, since things that run before main may hit them given the
   current way things are linked. */

/* XXX Technically these flags are initialized w.o.r. to the
   environment we are actually running in. We could patch them in
   klee_init_fds, but we still have the problem that uclibc calls
   prior to main will get the wrong data. Not such a big deal since we
   mostly care about sym case anyway. */


exe_sym_env_t __exe_env = {
  { /* initialization moved into klee_init_fds() */ },
  022,
  0,
  0
};


/* XXX: We should provide better support for such options! */
static int __one_line_streams = 0;
static const fill_info_t *__stream_fill_info = NULL;
static unsigned __n_stream_fill_info = 0;
static const fill_info_t *__dgram_fill_info = NULL;
static unsigned __n_dgram_fill_info = 0;

static int __isupper(const char c) { return (('A' <= c) & (c <= 'Z')); }

void __fill_blocks(exe_disk_file_t* dfile, const fill_info_t* fill_info, unsigned n_fill_info) {
  unsigned i, j;
  for (i = 0; i < n_fill_info; i++) {
    const fill_info_t *p = &fill_info[i];
    switch (p->fill_method) {
    case fill_set:
      /* effectively memset(dfile->contents + p->offset, p->arg.value, p->length) */
      for (j = 0; j < p->length; j++) {
        klee_assume((unsigned char) dfile->contents[p->offset + j] ==
                    (unsigned char) p->arg.value);
      }
      break;
    case fill_copy:
      /* effectively memcpy(dfile->contents + p->offset, p->arg.string, p->length) */
      for (j = 0; j < p->length; j++) {
        klee_assume(dfile->contents[p->offset + j] == p->arg.string[j]);
      }
      break;
    default:
      assert(0 && "unknown fill method");
    }
  }
}

static void __create_new_dfile(
	exe_disk_file_t *dfile, unsigned size, const char* name,
        const fill_info_t* fill_info, unsigned n_fill_info,
        struct stat64 *defaults, int is_foreign)
{
	struct stat64 *s = malloc(sizeof(*s));
	const char *sp;
	char file_name[64], data_name[64], stat_name[64], src_name[64];
	unsigned i;

	assert(dfile);

	for (sp=name; *sp; ++sp) file_name[sp-name] = *sp;
	strcpy(&file_name[sp-name], "-name");

	for (sp=name; *sp; ++sp) data_name[sp-name] = *sp;
	strcpy(&data_name[sp-name], "-data");

	for (sp=name; *sp; ++sp) stat_name[sp-name] = *sp;
	strcpy(&stat_name[sp-name], "-stat");

	/* Handle dfile names */
	if (	strcmp(name, "STDIN") == 0 ||
		strncmp(name, "STREAM", sizeof("STREAM")-1) == 0 || 
		strncmp(name, "DGRAM", sizeof("DGRAM")-1) == 0 ||
		strcmp(name, "STDOUT") == 0)
	{
		dfile->name = strdup(name);
	} else {
		dfile->name = malloc(KLEE_MAX_PATH_LEN);
		klee_make_symbolic(dfile->name, KLEE_MAX_PATH_LEN, file_name);
		/* prefer all caps?? */
		for (i=0; i<KLEE_MAX_PATH_LEN-1; i++)
			klee_prefer_cex(dfile->name, __isupper(dfile->name[i]));
	}
	/* No reason not to allow files of size 0, but to support this, we
	need to be change the way memory is allocated and freed when
	writing/reading .bout files. */
	assert(size);

	dfile->size = size;
	dfile->contents = malloc(dfile->size);
	klee_make_symbolic(dfile->contents, dfile->size, data_name);

	/* XXX: We should provide better support for such options! */
	__fill_blocks(dfile, fill_info, n_fill_info);

	if (__one_line_streams && dfile->size) {
		for (i=0; i < dfile->size-1; i++)
			if (dfile->contents[i] == '\n')
				klee_silent_exit(0);

		if (dfile->contents[dfile->size-1] != '\n')
			klee_silent_exit(0);
	}

	klee_make_symbolic(s, sizeof(*s), stat_name);

	/* For broken tests */
	if (!klee_is_symbolic(s->st_ino) && (s->st_ino & 0x7FFFFFFF) == 0)
		s->st_ino = defaults->st_ino;

	/* Important since we copy this out through getdents, and readdir
	   will otherwise skip this entry. For same reason need to make sure
	   it fits in low bits. */
	klee_assume((s->st_ino & 0x7FFFFFFF) != 0);

	/* uclibc opendir uses this as its buffer size, try to keep
	   reasonable. */
	klee_assume((s->st_blksize & ~0xFFFF) == 0);

	klee_prefer_cex(s, !(s->st_mode & ~(S_IFMT | 0777)));
	klee_prefer_cex(s, s->st_dev == defaults->st_dev);
	klee_prefer_cex(s, s->st_rdev == defaults->st_rdev);
	klee_prefer_cex(s, (s->st_mode&0700) == 0600);
	klee_prefer_cex(s, (s->st_mode&0070) == 0020);
	klee_prefer_cex(s, (s->st_mode&0007) == 0002);
	klee_prefer_cex(s, (s->st_mode&S_IFMT) == S_IFREG);
	klee_prefer_cex(s, s->st_nlink == 1);
	klee_prefer_cex(s, s->st_uid == defaults->st_uid);
	klee_prefer_cex(s, s->st_gid == defaults->st_gid);
	klee_prefer_cex(s, s->st_blksize == 4096);
	klee_prefer_cex(s, s->st_atime == defaults->st_atime);
	klee_prefer_cex(s, s->st_mtime == defaults->st_mtime);
	klee_prefer_cex(s, s->st_ctime == defaults->st_ctime);

	s->st_size = dfile->size;
	s->st_blocks = 8;
	dfile->stat = s;

	dfile->src = NULL;
  if (is_foreign)
  {
    struct sockaddr_storage *ss;

    klee_assume((dfile->stat->st_mode & S_IFMT) == S_IFSOCK);

    for (sp=name; *sp; ++sp)
      src_name[sp-name] = *sp;
    strcpy(&src_name[sp-name], "-src");

    dfile->src = calloc(1, sizeof(*(dfile->src)));
    dfile->src->addrlen = sizeof(struct sockaddr_in);
    dfile->src->addr = ss = malloc(sizeof(*(dfile->src->addr)));
    klee_make_symbolic(dfile->src->addr, sizeof(*dfile->src->addr), src_name);
    /* Assume the port number is non-zero for PF_INET and PF_INET6. */
    /* Since the address family will be assigned later, we
       conservatively assume that the port number is non-zero for
       every address family supported. */
    klee_assume(/* ss->ss_family != AF_INET  || */ ((struct sockaddr_in  *)ss)->sin_port  != 0);
    klee_assume(/* ss->ss_family != AF_INET6 || */ ((struct sockaddr_in6 *)ss)->sin6_port != 0);
    klee_prefer_cex(dfile->src->addr, dfile->src->addr->ss_family == AF_INET);
  }
}

static unsigned __sym_uint32(const char *name) {
  unsigned x;
  klee_make_symbolic(&x, sizeof x, name);
  return x;
}

#define ALLOC_EXEFS(x)		\
	__exe_fs.x = malloc(sizeof(*__exe_fs.x));			\
	klee_make_symbolic(__exe_fs.x, sizeof(*__exe_fs.x), #x);	\

static void alloc_failures(void)
{
	ALLOC_EXEFS(open_fail);
	ALLOC_EXEFS(read_fail);
	ALLOC_EXEFS(write_fail);
	ALLOC_EXEFS(close_fail);
	ALLOC_EXEFS(select_fail);
	ALLOC_EXEFS(ftruncate_fail);
	ALLOC_EXEFS(getcwd_fail);
	ALLOC_EXEFS(socket_fail);
	ALLOC_EXEFS(bind_fail);
	ALLOC_EXEFS(connect_fail);
	ALLOC_EXEFS(listen_fail);
	ALLOC_EXEFS(accept_fail);
}

static void setup_symfiles(
	struct stat64* s, unsigned n_files, unsigned file_length)
{
	char	fname[] = "FILE?";
	unsigned k;

	__exe_fs.n_sym_files = n_files;
	__exe_fs.sym_files = malloc(sizeof(*__exe_fs.sym_files) * n_files);
	__exe_fs.n_sym_files_used = 0;

	for (k=0; k < n_files; k++) {
		fname[strlen(fname)-1] = '1' + k;
		__create_new_dfile(
			&__exe_fs.sym_files[k],
			file_length,
			fname,
			NULL, 0, s, 0);
	}
}

static void setup_streams(
	struct stat64* s, unsigned n_streams, unsigned stream_len)
{
	char	sname[] = "STREAM?";
	unsigned k;

	__exe_fs.n_sym_streams = n_streams;
	__exe_fs.sym_streams = malloc(sizeof(*__exe_fs.sym_streams) * n_streams);
	__exe_fs.n_sym_streams_used = 0;

	if (n_streams && !__exe_fs.sym_streams)
		klee_warning("malloc returned NULL for sym_streams");

	for (k=0; k < n_streams; k++) {
		sname[strlen(sname)-1] = '1' + k;
		__create_new_dfile(
			&__exe_fs.sym_streams[k],
			stream_len, sname,
			__stream_fill_info,
			__n_stream_fill_info, s, 1);
	}
}

static void setup_dgrams(
	struct stat64* s, unsigned n_dgrams, unsigned dgram_len)
{
	char	dname[] = "DGRAM?";
	unsigned k;

	__exe_fs.n_sym_dgrams = n_dgrams;
	__exe_fs.sym_dgrams = malloc(sizeof(*__exe_fs.sym_dgrams) * n_dgrams);
	__exe_fs.n_sym_dgrams_used = 0;

	if (n_dgrams && !__exe_fs.sym_dgrams)
		klee_warning("malloc returned NULL for sym_dgrams");

	for (k=0; k < n_dgrams; k++) {
		dname[strlen(dname)-1] = '1' + k;
		__create_new_dfile(
			&__exe_fs.sym_dgrams[k],
			dgram_len,
			dname,
			__dgram_fill_info, __n_dgram_fill_info, s, 1);
	}
}

static void setup_stdio(
	struct stat64* s,
	unsigned file_length,
	int sym_stdout_flag)
{
	exe_file_t* f;

	// stdin
	f = calloc(1, sizeof(*f));
	assert(f);
	f->fd = 0;
	f->flags = eOpen | eReadable;
	__exe_env.fds[f->fd] = f;

	// stdout
	f = calloc(1, sizeof(*f));
	assert(f);
	f->fd = 1;
	f->flags = eOpen | eWriteable;
	__exe_env.fds[f->fd] = f;

	// stderr
	f = calloc(1, sizeof(*f));
	assert(f);
	f->fd = 2;
	f->flags = eOpen | eWriteable;
	__exe_env.fds[f->fd] = f;

	/* setting symbolic stdin */
	if (file_length) {
		__exe_fs.sym_stdin = malloc(sizeof(*__exe_fs.sym_stdin));
		__exe_fs.sym_stdin->name = strdup("STDIN");
		__create_new_dfile(
			__exe_fs.sym_stdin, file_length, "STDIN", NULL, 0, s, 0);
		__exe_env.fds[0]->dfile = __exe_fs.sym_stdin;
	}
	else __exe_fs.sym_stdin = NULL;

	/* setting symbolic stdout */
	if (sym_stdout_flag) {
		__exe_fs.sym_stdout = malloc(sizeof(*__exe_fs.sym_stdout));
		__create_new_dfile(
			__exe_fs.sym_stdout, 1024, "STDOUT", NULL, 0, s, 0);
		__exe_env.fds[1]->dfile = __exe_fs.sym_stdout;
		__exe_fs.stdout_writes = 0;
	}
	else __exe_fs.sym_stdout = NULL;
}


/* n_files: number of symbolic input files, excluding stdin
   file_length: size in bytes of each symbolic file, including stdin
   sym_stdout_flag: 1 if stdout should be symbolic, 0 otherwise
   save_all_writes_flag: 1 if all writes are executed as expected, 0 if
                         writes past the initial file size are discarded
                         (file offset is always incremented)
   max_failures: maximum number of system call failures */
void klee_init_fds(
	unsigned n_files, unsigned file_length,
	int sym_stdout_flag, int save_all_writes_flag,
	unsigned n_streams, unsigned stream_len,
	unsigned n_dgrams, unsigned dgram_len,
	unsigned max_failures, int fd_short, int one_line_streams,
	const fill_info_t stream_fill_info[], unsigned n_stream_fill_info,
	const fill_info_t dgram_fill_info[], unsigned n_dgram_fill_info)
{
	struct stat64 s;

	__one_line_streams = one_line_streams;
	__stream_fill_info = stream_fill_info;
	__n_stream_fill_info = n_stream_fill_info;
	__dgram_fill_info = dgram_fill_info;
	__n_dgram_fill_info = n_dgram_fill_info;

	stat64(".", &s);

	setup_stdio(&s, file_length, sym_stdout_flag);
	setup_symfiles(&s, n_files, file_length);
  	setup_streams(&s, n_streams, stream_len);
	setup_dgrams(&s, n_streams, dgram_len);

	__exe_fs.max_failures = max_failures;
	if (__exe_fs.max_failures) alloc_failures();

	__exe_fs.fd_short = fd_short;

	__exe_env.save_all_writes = save_all_writes_flag;
	__exe_env.version = __sym_uint32("model_version");
	klee_assume(__exe_env.version == 2);
}
