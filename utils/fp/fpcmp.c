#include <fcntl.h>
#include <unistd.h>

#ifndef TYPE1_IN
#error uh
#endif
#ifndef TYPE2_IN
#error uh
#endif
#ifndef FPOP
#error uh
#endif

int main(void)
{
	TYPE1_IN	in1;
	TYPE2_IN	in2;
	size_t		br;

	br = read(0, &in1, sizeof(in1));
	if (br != sizeof(in1)) return 6;

	br = read(0, &in2, sizeof(in2));
	if (br != sizeof(in2)) return 7;

	if (in1 FPOP in2)
		return 1;

	return 0;
}
