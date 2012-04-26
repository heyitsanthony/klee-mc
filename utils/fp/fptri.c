#include <fcntl.h>
#include <unistd.h>

#ifndef TYPE1_IN
#error uh
#endif
#ifndef TYPE2_IN
#error uh
#endif
#ifndef TYPE3_OUT
#error uh
#endif
#ifndef FPOP
#error uh
#endif

int main(void)
{
	TYPE1_IN	in1;
	TYPE2_IN	in2;
	TYPE3_OUT	out, sym_out;
	size_t		br;

	br = read(0, &in1, sizeof(in1));
	if (br != sizeof(in1)) return 6;

	br = read(0, &in2, sizeof(in2));
	if (br != sizeof(in2)) return 7;

	br = read(0, &sym_out, sizeof(sym_out));
	if (br != sizeof(sym_out)) return 6;

	out = in1 FPOP in2;
	// write(1, &out, sizeof(out));

	if (out < sym_out) {
		return 1;
	} else if (out > sym_out) {
		return 2;
	} else if (out == sym_out) {
		return 3;
	} else if (out != sym_out) {
		return 4;
	}

	return 0;
}
