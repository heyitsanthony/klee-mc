/**
 * klee-hw stdin takes a shm_pkt which describes the shm configuration
 * klee-hw is ptraced to get/set registers, etc
 *
 * accesses data through shmget
 * 1. shmid received over pipe
 * 2.
 * format: list of page extents, followed by page data
 * 
 */

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/shm.h>
#include <sys/ptrace.h>
#include <sys/mman.h>
#include <assert.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>

#include "hw_accel.h"

#define	DEBUG(x)	0

int main(void)
{
	int			br;
	void			*shm_addr;
	struct shm_pkt		sp;
	struct hw_map_extent	*shm_maps;
	uint8_t			*shm_payload;

	if (isatty(0)) {
		fprintf(stderr, "[klee-hw] This only works with klee-mc!\n");
		return 2;
	}

	/* read in shm data from pipe established by klee-mc */
	br = read(0, &sp, sizeof(sp));
	if (br != sizeof(struct shm_pkt)) {
		fprintf(stderr, 
			"[klee-hw] Got %u br bytes, expected %u\n",
			br, (int)sizeof(struct shm_pkt));
		return 1;
	}

	/* attach shm data */
	shm_addr = shmat(sp.sp_shmid, NULL, SHM_RND);
	assert (shm_addr != (void*)-1);

	shm_maps = shm_addr;
	while (shm_maps->hwm_addr != NULL) {
		void	*a, *target_a;
		unsigned target_off;

		target_a = (void*)((intptr_t)shm_maps->hwm_addr & ~(0xfffULL));
		target_off = (intptr_t)shm_maps->hwm_addr & 0xfff;

		a = mmap(
			target_a,
			shm_maps->hwm_bytes + target_off,
			PROT_READ | PROT_WRITE | PROT_EXEC,
			MAP_ANONYMOUS | MAP_PRIVATE | MAP_FIXED,
			-1,
			0);

		if (a != target_a) {
			fprintf(stderr, "[klee-hw] Could not map %p--%p\n",
				shm_maps->hwm_addr,
				(void*)((long)shm_maps->hwm_addr
					+ shm_maps->hwm_bytes));
			abort();
		}

		shm_maps++;
	}
	shm_payload = (void*)(shm_maps+1);

	/* copy to appropriate locations */
	DEBUG(fprintf(stderr, "[klee-hw] COPYING IN\n"));
	shm_maps = shm_addr;
	br = 0;
	while (shm_maps->hwm_addr != NULL) {
		memcpy(shm_maps->hwm_addr, shm_payload+br, shm_maps->hwm_bytes);
		br += shm_maps->hwm_bytes;
		shm_maps++;
	}

	DEBUG(fprintf(stderr, "[klee-hw] Copy in done\n"));

	/* signal to klee-mc that the process memory is set up */
	/* it could be faster to copy the register data
	 * in through shm, but we don't get nice code reuse with vexllm */
	// raise(SIGSTOP);
	kill(getpid(), SIGTSTP);

	/* tricky part: klee-mc reloads the registers post-kill
	 * and returns here *after* the execution of the guest code */
	DEBUG(fprintf(stderr, "[klee-hw] PAST THE RAISE\n"));
	
	/* copy data back to shm */
	shm_maps = shm_addr;
	br = 0;
	while (shm_maps->hwm_addr != NULL) {
		memcpy(shm_payload+br, shm_maps->hwm_addr, shm_maps->hwm_bytes);
		br += shm_maps->hwm_bytes;
		shm_maps++;
	}

	DEBUG(fprintf(stderr, "[klee-hw] Copy out done!\n"));
	_exit(0);
	return 0;
}