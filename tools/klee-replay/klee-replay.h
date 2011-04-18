//===-- klee-replay.h ------------------------------------------*- C++ -*--===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef __KLEE_REPLAY_H__
#define __KLEE_REPLAY_H__

#ifndef _LARGEFILE64_SOURCE
#define _LARGEFILE64_SOURCE
#endif

#define _FILE_OFFSET_BITS 64

// FIXME: This is a hack.
#include "../../runtime/POSIX/fd.h"
#include <sys/time.h>
#include <sys/reg.h>

void replay_create_files(const exe_file_system_t *exe_fs);
void replay_delete_files(const exe_file_system_t *exe_fs);

void process_status(int status,
		    time_t elapsed,
		    const char *pfx)
  __attribute__((noreturn));

extern int skip;

#if __WORDSIZE == 64

#define KLEE_REGSIZE 8
#define KLEE_EAX RAX
#define KLEE_EBX RBX
#define KLEE_ECX RCX
#define KLEE_EDX RDX
#define KLEE_ORIG_EAX ORIG_RAX
#define KLEE_IP_NAME "rip"
#define KLEE_IP(a) ((a).rip)

#else

#define KLEE_REGSIZE 4
#define KLEE_EAX EAX
#define KLEE_EBX EBX
#define KLEE_ECX ECX
#define KLEE_EDX EDX
#define KLEE_ORIG_EAX ORIG_EAX
#define KLEE_IP_NAME "eip"
#define KLEE_IP(a) ((a).eip)

#endif

#endif
