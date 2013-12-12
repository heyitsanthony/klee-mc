// RUN: %llvmgcc -I../../../include -I../../../runtime/mmu/  -I../../../runtime/  -emit-llvm -c -o %t1.bc %s
// RUN: %klee -use-sym-mmu=false -batch-time=5 -use-batching-search -use-random-search --stop-after-n-errors=1 --no-externals --libc=none -use-cache  --use-cex-cache  -dump-states-on-halt=false -max-time=30 %t1.bc 2>%t1.err
// RUN: ls klee-last | not grep err
#define SHADOW_PG_BUCKETS	4
#define DOGFOOD_MAX_ITERS	4

#include <stdint.h>
#include <stdlib.h>
#include "mmu/shadow.c"
#include "klee/klee.h"

struct shadow_info	g_si;

//#define REPLAY_SEQ	1
#ifdef REPLAY_SEQ
static int pgs[] = {0, 4, 1, 4, 0};
static int bits[] = {0, 1, 0, 0, 1};
static void replay_seq(void)
{
	unsigned	i;
	for (i = 0; i < DOGFOOD_MAX_ITERS; i++)
		shadow_put(&g_si, 1024+(pgs[i]*4096), bits[i]);
}
#endif

int main(void)
{
	unsigned		i;

	shadow_init(&g_si, 32, 1, 0);

#ifndef REPLAY_SEQ
	for (i = 0; i < DOGFOOD_MAX_ITERS; i++) {
		unsigned item = klee_range(0, 16, "pg");
		unsigned bit = klee_range(0, 2, "bit");
		shadow_put(&g_si, 1024+(item*4096), bit);
	}
#else
	replay_seq();
#endif

	shadow_fini(&g_si);
	return 0;
}

