// tests if replay results  
// RUN: rm -rf guest-symfp-replay klee-out-symfpreplay
// RUN: gcc %s -O0 -o %t1
// RUN: ../../../scripts/save_guest.sh ./%t1 guest-symfp-replay
// RUN: klee-mc -seed-rng=10 -mm-type=deterministic -output-dir=klee-out-symfpreplay -stop-after-n-tests=20 -pipe-solver -write-paths  - ./%t1 2>%t1.err >%t1.out
// RUN: klee-mc -seed-rng=10 -pipe-solver -exit-on-error -mm-type=deterministic -replay-path-dir=klee-out-symfpreplay/ -stop-after-n-tests=21 -guest-type=sshot -guest-sshot=guest-symfp-replay - 2>>%t1.2.err >>%t1.out
// RUN: ls klee-last | not grep err
// RUN: rm -rf guest-symfp-replay klee-out-symfpreplay

typedef int(*fptr)(const char*, const char*);


int ok(const char* x, const char* y)
{
	if (strcmp(y, x) == 0) {
		return 1;
	}
	return 3;
}

int g(const char* x, const char* y)
{
	return 6;
}

int h(const char* x, const char* y)
{
	return strcmp(x, y) != 0;
}

static fptr fps[] = {ok, g, h, g, h};

int main(void)
{
	unsigned x;
	if (read(0, &x, 4) == 0) {
		return 0;
	}

	if (x > 4) return 1;

	return fps[x]("masdmadsmasdm", "abcdefghijklmnopqrstuvwxyz");
}