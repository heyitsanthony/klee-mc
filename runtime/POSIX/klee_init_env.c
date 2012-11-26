//===-- klee_init_env.c ---------------------------------------------------===//
//
//					 The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "klee/klee.h"
#ifndef _LARGEFILE64_SOURCE
#define _LARGEFILE64_SOURCE
#endif
#include "fd.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <sys/syscall.h>
#include <unistd.h>

static void __emit_error(const char *msg)
{ klee_report_error(__FILE__, __LINE__, msg, "user.err"); }

/* Helper function that converts a string to an integer, and
	 terminates the program with an error message is the string is not a
	 proper number */
static long int __str_to_int(char *s, const char *error_msg) {
	long int res = 0;
	char c;

	if (!*s)
	__emit_error(error_msg);

	if (*s == '0' && (*(s+1) == 'X' || *(s+1) == 'x')) {
	/* hexadecimal */
	s += 2;
	while ((c = *s++)) {
		if (c == '\0')
		break;
		else if (c>='0' && c<='9')
		res = res*16 + (c-'0');
		else if (c>='A' && c<='F')
		res = res*16 + (c-'A') + 10;
		else if (c>='a' && c<='f')
		res = res*16 + (c-'a') + 10;
		else
		__emit_error(error_msg);
	}
	}

	else if (*s == '0') {
	/* octal */
	s++;
	while ((c = *s++)) {
		if (c == '\0')
		break;
		else if (c>='0' && c<='7')
		res = res*10 + (c-'0');
		else
		__emit_error(error_msg);
	}
	}

	else {
	/* decimal */
	while ((c = *s++)) {
		if (c == '\0')
		break;
		else if (c>='0' && c<='9')
		res = res*10 + (c-'0');
		else
		__emit_error(error_msg);
	}
	}

	return res;
}

static int __isprint(const char c) {
	/* Assume ASCII */
	return (32 <= c && c <= 126);
}

static int __getodigit(const char c) {
	return (('0' <= c) && (c <= '7')) ? (c - '0') : -1;
}

static int __getxdigit(const char c) {
	return (('0' <= c) && (c <= '9')) ? (c - '0') :
		 (('A' <= c) && (c <= 'F')) ? (c - 'A') + 10:
		 (('a' <= c) && (c <= 'f')) ? (c - 'a') + 10: -1;
}

/* Convert in-place, but it's okay because no escape sequences "expand". */
static size_t __convert_escape_sequences(char *s)
{
	char *d0 = s, *d = s;

	while (*s) {
	if (*s != '\\')
		*d++ = *s++;
	else {
		s++;
		switch (*s++) {
		int n[3];
		default:	*d++ = s[-1]; break;
		case 'a': *d++ = '\a'; break;
		case 'b': *d++ = '\b'; break;
		case 'f': *d++ = '\f'; break;
		case 'n': *d++ = '\n'; break;
		case 'r': *d++ = '\r'; break;
		case 't': *d++ = '\t'; break;
		case 'v': *d++ = '\v'; break;
		case '0':
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
		n[0] = __getodigit(s[-1]);
		if ((n[1] = __getodigit(*s)) >= 0) {
			s++;
			if ((n[2] = __getodigit(*s)) >= 0) {
			s++;
			*d++ = (n[0] << 6) | (n[1] << 3) | n[2];
			}
			else
			*d++ = (n[0] << 3) | n[1];
		}
		else
			*d++ = n[0];
		break;
		case 'x':
		if ((n[0] = __getxdigit(*s)) >= 0) {
			s++;
			if ((n[1] = __getxdigit(*s)) >= 0) {
			s++;
			*d++ = (n[0] << 4) | n[1];
			}
			else
			*d++ = n[0];
		}
		else /* error */
			__emit_error("invalid escape sequence");
		break;
		}
	}
	}
	return d - d0;
}

static int __streq(const char *a, const char *b) {
	while (*a == *b) {
	if (!*a)
		return 1;
	a++;
	b++;
	}
	return 0;
}

static char *__get_sym_str(int numChars, char *name) {
	int i;
	char *s = malloc(numChars+1);
	klee_mark_global(s);
	klee_make_symbolic(s, numChars+1, name);

	for (i=0; i<numChars; i++)
	klee_prefer_cex(s, __isprint(s[i]));

	s[numChars] = '\0';
	return s;
}

static void __add_arg(int *argc, char **argv, char *arg, int argcMax)
{
	if (*argc==argcMax) {
		__emit_error("too many arguments for klee_init_env");
	} else {
		argv[*argc] = arg;
		(*argc)++;
	}
}

struct env_info
{
	int		argc;
	char		**argv;
	int		k;

	int		new_argc;
	char		*new_argv[1024];

	unsigned	max_len, min_argvs, max_argvs;
	unsigned	sym_files, sym_file_len;
	unsigned	sym_streams, sym_stream_len;
	unsigned	sym_dgrams, sym_dgram_len;
	int		sym_stdout_flag;
	int		save_all_writes_flag;
	int		one_line_streams_flag;
	fill_info_t	sfill[100];	/* stream fill */
	unsigned	n_sfill;
	fill_info_t	dfill[100];	/* datagram fill */
	unsigned	n_dfill;
	int		fd_fail;
	int		fd_short;
	char**		final_argv;
	char		sym_arg_name[5];
	unsigned	sym_arg_num;
};


static void fill_symconnections(struct env_info* ei);
static void fill_symstreams(struct env_info* ei);
static void fill_streams(struct env_info* ei);
static void fill_symfiles(struct env_info* ei);
static void fill_datagrams(struct env_info* ei);
static void fill_symargs(struct env_info* ei);
static void fill_symarg(struct env_info* ei);
static void fill_symdgrams(struct env_info* ei);

#define HAS_ARG(s, x) __streq(s, "-"x) || __streq(s, "--"x)
#define HAS_ARGV(x)	HAS_ARG(ei->argv[ei->k],x)

void eat_arg(struct env_info* ei)
{
	if (HAS_ARGV("sym-arg")) fill_symarg(ei);
	else if (HAS_ARGV("sym-args")) fill_symargs(ei);
	else if (HAS_ARGV("sym-files")) fill_symfiles(ei);
	else if (HAS_ARGV("sym-stdout")) {
		ei->sym_stdout_flag = 1;
		ei->k++;
	} else if (HAS_ARGV("save-all-writes")) {
		ei->save_all_writes_flag = 1;
		ei->k++;
	}
	else if (HAS_ARGV("max-fail")) {
		const char *msg = "--max-fail expects an integer argument <max-failures>";
		if (++ei->k == ei->argc)
			__emit_error(msg);

		ei->fd_fail = __str_to_int(ei->argv[ei->k++], msg);
	}
	else if (HAS_ARGV("fd-fail")) {
		ei->fd_fail = 1;
		ei->k++;
	}
	else if (HAS_ARGV("fd-short")) {
		ei->fd_short = 1;
		ei->k++;
	}
	/* "sym-connections": for backward compatability */
	else if (HAS_ARGV("sym-connections"))
		fill_symconnections(ei);
	else if (HAS_ARGV("sym-streams")) 
		fill_symstreams(ei);
	else if (HAS_ARGV("sym-datagrams")) {
		fill_symdgrams(ei);
	}
	else if (HAS_ARGV("one-line-streams")) {
		ei->one_line_streams_flag = 1;
		ei->k++;
	}
	else if (HAS_ARGV("fill-streams")) 
		fill_streams(ei);
	else if (HAS_ARGV("fill-datagrams"))
		fill_datagrams(ei);
	else {
		/* simply copy arguments */
		__add_arg(&ei->new_argc, ei->new_argv, ei->argv[ei->k++], 1024);
	}
}

void show_help(int argc, char** argv)
{
	if (argc != 2 || !__streq(argv[1], "--help"))
		return;

	__emit_error("klee_init_env\n\n\
usage: (klee_init_env) [options] [program arguments]\n\
	-sym-arg <N>				- Replace by a symbolic argument with length N\n\
	-sym-args <MIN> <MAX> <N> - Replace by at least MIN arguments and at most\n\
								MAX arguments, each with maximum length N\n\
	-sym-files <NUM> <N>		- Make stdin and up to NUM symbolic files, each\n\
								with maximum size N.\n\
	-sym-stdout				 - Make stdout symbolic.\n\
	-save-all-writes\n\
	-max-fail <N>			 - Allow up to <N> injected failures\n\
	-fd-fail					- Shortcut for '-max-fail 1'\n\
	-fd-short				 - Allow returning amounts that fall short of those requested\n\
	-sym-streams <N> <LEN>	- Prepare <N> streams of <LEN> bytes each\n\
	-sym-datagrams <N> <LEN>	- Prepare <N> datagrams of <LEN> bytes each\n\
	-one-line-streams		 - Constrain the streams to single lines\n\
	-fill-{streams|datagrams} - Fill (Concretize) streams or datagrams with:\n\
	 <OFF> set <LEN> <VAL>		<LEN> bytes of <VAL> from <OFF>\n\
	 <OFF> copy <STR>			 <STR> from <OFF>\n\n");
}

void klee_init_env(int* argcPtr, char*** argvPtr)
{
	int		argc = *argcPtr;
	char		**argv = *argvPtr;
	struct env_info	ei;

	// Recognize --help when it is the sole argument.
	show_help(argc, argv);

	memset(&ei, 0, sizeof(ei));
	memcpy(ei.sym_arg_name, "arg", 4);
	ei.argc = argc;
	ei.argv = argv;
	ei.k = 0;

	while (ei.k < ei.argc) eat_arg(&ei);

	ei.final_argv = (char**) malloc((ei.new_argc+1) * sizeof(*ei.final_argv));
	klee_mark_global(ei.final_argv);
	memcpy(ei.final_argv, ei.new_argv, ei.new_argc * sizeof(*ei.final_argv));
	ei.final_argv[ei.new_argc] = 0;

	*argcPtr = ei.new_argc;
	*argvPtr = ei.final_argv;

	klee_init_fds(
		ei.sym_files, ei.sym_file_len,
		ei.sym_stdout_flag, ei.save_all_writes_flag,
		ei.sym_streams, ei.sym_stream_len,
		ei.sym_dgrams, ei.sym_dgram_len,
		ei.fd_fail, ei.fd_short, ei.one_line_streams_flag,
		ei.sfill, ei.n_sfill,
		ei.dfill, ei.n_dfill);
}

static void fill_datagrams(struct env_info* ei)
{
	const char* msg = "--fill-datagrams expects arguments <offset> \"set\" <length> <value> or <offset> \"copy\" <string>";
	const char* msg2 = "--fill-datagrams: too many blocks";

	if (ei->n_dfill >= sizeof(ei->dfill) / sizeof(*ei->dfill))
		__emit_error(msg2);
	if (ei->k + 2 >= ei->argc)
		__emit_error(msg);

	ei->k++;
	ei->dfill[ei->n_dfill].offset = __str_to_int(ei->argv[ei->k++], msg);
	if (__streq(ei->argv[ei->k], "set")) {
		ei->k++;
		if (ei->k + 1 >= ei->argc)
			__emit_error(msg);
		ei->dfill[ei->n_dfill].fill_method = fill_set;
		ei->dfill[ei->n_dfill].length = __str_to_int(ei->argv[ei->k++], msg);
		ei->dfill[ei->n_dfill].arg.value = __str_to_int(ei->argv[ei->k++], msg);
		ei->n_dfill++;
	}
	else if (__streq(ei->argv[ei->k], "copy")) {
		ei->k++;
		if (ei->k >= ei->argc)
			__emit_error(msg);
		ei->dfill[ei->n_dfill].fill_method = fill_copy;
		ei->dfill[ei->n_dfill].arg.string = ei->argv[ei->k++];
		ei->dfill[ei->n_dfill].length =
			__convert_escape_sequences(ei->dfill[ei->n_dfill].arg.string);
		ei->n_dfill++;
	}
	else
		__emit_error(msg);
}

static void fill_symarg(struct env_info* ei)
{
	const char *msg = "--sym-arg expects an integer argument <max-len>";
	if (++ei->k == ei->argc)
		__emit_error(msg);

	ei->max_len = __str_to_int(ei->argv[ei->k++], msg);
	ei->sym_arg_name[3] = '0' + ei->sym_arg_num++;
	__add_arg(&ei->new_argc, ei->new_argv,
			__get_sym_str(ei->max_len, ei->sym_arg_name),
			1024);
}

static void fill_symargs(struct env_info* ei)
{
	int n_args, i;

	const char *msg =
		"--sym-args expects three integer arguments <min-argvs> <max-argvs> <max-len>";

	if (ei->k+3 >= ei->argc)
		__emit_error(msg);

	ei->k++;
	ei->min_argvs = __str_to_int(ei->argv[ei->k++], msg);
	ei->max_argvs = __str_to_int(ei->argv[ei->k++], msg);
	ei->max_len = __str_to_int(ei->argv[ei->k++], msg);

	n_args = klee_range(ei->min_argvs, ei->max_argvs+1, "n_args");
	for (i=0; i < n_args; i++) {
		ei->sym_arg_name[3] = '0' + ei->sym_arg_num++;
		__add_arg(
			&ei->new_argc,
			ei->new_argv,
			__get_sym_str(ei->max_len, ei->sym_arg_name),
			1024);
	}
}

static void fill_symfiles(struct env_info* ei)
{
	const char* msg = "--sym-files expects two integer arguments <no-sym-files> <sym-file-len>";

	if (ei->k+2 >= ei->argc)
		__emit_error(msg);

	ei->k++;
	ei->sym_files = __str_to_int(ei->argv[ei->k++], msg);
	ei->sym_file_len = __str_to_int(ei->argv[ei->k++], msg);
}

static void fill_streams(struct env_info* ei)
{
	const char* msg = "--fill-streams expects arguments <offset> \"set\" <length> <value> or <offset> \"copy\" <string>";
	const char* msg2 = "--fill-streams: too many blocks";

	if (ei->n_sfill >= sizeof(ei->sfill) / sizeof(*ei->sfill))
		__emit_error(msg2);

	if (ei->k + 2 >= ei->argc)
		__emit_error(msg);
		ei->k++;
		ei->sfill[ei->n_sfill].offset = __str_to_int(ei->argv[ei->k++], msg);
	if (__streq(ei->argv[ei->k], "set")) {
		ei->k++;
		if (ei->k + 1 >= ei->argc)
			__emit_error(msg);
		ei->sfill[ei->n_sfill].fill_method = fill_set;
		ei->sfill[ei->n_sfill].length = __str_to_int(ei->argv[ei->k++], msg);
		ei->sfill[ei->n_sfill].arg.value = __str_to_int(ei->argv[ei->k++], msg);
		ei->n_sfill++;
		}
	else if (__streq(ei->argv[ei->k], "copy")) {
		ei->k++;
		if (ei->k >= ei->argc)
			__emit_error(msg);
		ei->sfill[ei->n_sfill].fill_method = fill_copy;
		ei->sfill[ei->n_sfill].arg.string = ei->argv[ei->k++];
		ei->sfill[ei->n_sfill].length =
			__convert_escape_sequences(ei->sfill[ei->n_sfill].arg.string);
		ei->n_sfill++;
	}
	else
		__emit_error(msg);

}

static void fill_symstreams(struct env_info* ei)
{
	const char* msg = "--sym-streams expects two integer arguments <no-streams> <bytes-per-stream>";

	if (ei->k+2 >= ei->argc)
		__emit_error(msg);

	ei->k++;
	ei->sym_streams = __str_to_int(ei->argv[ei->k++], msg);
	ei->sym_stream_len = __str_to_int(ei->argv[ei->k++], msg);
}


static void fill_symconnections(struct env_info* ei)
{
	const char* msg = "--sym-connections expects two integer arguments <no-connections> <bytes-per-connection>";

	if (ei->k+2 >= ei->argc)
		__emit_error(msg);

	ei->k++;
	ei->sym_streams = __str_to_int(ei->argv[ei->k++], msg);
	ei->sym_stream_len = __str_to_int(ei->argv[ei->k++], msg);
}

static void fill_symdgrams(struct env_info* ei)
{
	const char* msg = "--sym-datagrams expects two integer arguments <no-datagrams> <bytes-per-datagram>";

	if (ei->k+2 >= ei->argc)
		__emit_error(msg);

	ei->k++;
	ei->sym_dgrams = __str_to_int(ei->argv[ei->k++], msg);
	ei->sym_dgram_len = __str_to_int(ei->argv[ei->k++], msg);
}
