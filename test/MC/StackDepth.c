// RUN: gcc %s -O0  -o %t1
// RUN: klee-mc -use-stack-search -pipe-solver -max-time=30 -dump-stackstats=2 -stop-after-n-tests=100 -dump-states-on-halt=false - ./%t1 2>%t1.err >%t1.out
// RUN: ls klee-last | grep err


char recur(char *sym, int len, int bit)
{
	char	blowout[2048];

	blowout[2047] = sym[0];
	if (bit >= 8)
		return recur(sym+1, len-1, 0);

	if ((*sym) & (1 << bit))
		return *sym;

	return recur(sym, len, bit+1);
}

int main()
{
	char sym[256];
	int	ret;

	ret = read(0, sym, 256);
	if (ret != 256)
		return 1;
	
	recur(sym, 256, 0);
	return 0;
}
