// tests if replay results  
// RUN: rm -rf guest-replay klee-out-replay
// RUN: gcc %s -O0 -o %t1
// RUN: ../../../scripts/save_guest.sh ./%t1 guest-replay
// RUN: klee-mc -seed-rng=10 -chk-constraints -mm-type=deterministic -output-dir=klee-out-replay -stop-after-n-tests=20 -pipe-solver -write-paths  - ./%t1 2>%t1.err >%t1.out
// RUN: klee-mc -replay-suppress-forks -only-replay -seed-rng=10 -pipe-solver -chk-constraints -mm-type=deterministic -replay-path-dir=klee-out-replay/ -stop-after-n-tests=21 -guest-type=sshot -guest-sshot=guest-replay - 2>>%t1.2.err >>%t1.out
// RUN: ls klee-last | not grep \.err$
// RUN: rm -rf guest-replay klee-out-replay

int ok(const char* x, const char* y)
{
	if (strcmp(y, x) == 0) {
		return 1;
	}

	return 3;
}

int main(void)
{
	char	x[16];
	if (read(0, x, 16) == 0) {
		return 0;
	}

	x[16] = 0;
	return ok(x, "abcdefghijklmnopqrstuvwxyz");
}
