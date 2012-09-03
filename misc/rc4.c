// RUN: gcc %s -I../../../include -O0 -o %t1
// RUN: klee-mc -pipe-solver - ./%t1 2>%t1.err >%t1.out
//
// RUN: ls klee-last | not grep .err
#include <sys/mman.h>
#include <stdint.h>
#include <unistd.h>
#include "klee/klee.h"

int main(void)
{
	int	i, j, k;
	void	*m;
	uint8_t	*s;
	char	key[16];

	if (read(0, key, 16) != 16)
		return -1;

	m = mmap(NULL, 4096,
		PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (m == MAP_FAILED)
		return -2;

	s = m;

	for (i = 0; i <= 255; i++)
		s[i] = i;

	j = 0;
	for (i = 0; i <= 255; i++) {
		uint8_t	tmp;
		j = (j + s[i] + key[i & 0xf]) & 0xff;
		tmp = s[i];
		s[i] = s[j];
		s[j] = tmp;
		ksys_print_expr("INIT", i);
	}

	i = 0;
	j = 0;

	for (k = 0; k < 2; k++) {
		uint8_t	tmp, out_v;

		i = (i + 1) & 0xff;
		j = (j + s[i]) & 0xff;
		tmp = s[i];
		s[i] = s[j];
		s[j] = tmp;

		out_v = s[(s[i] + s[j]) & 0xff];
		ksys_assume_eq(out_v, 0);
	}

	return 0;
}
