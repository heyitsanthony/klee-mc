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

int main(int argc, char* argv[])
{
	TYPE1_IN	in1;
	TYPE2_IN	in2;
	TYPE3_OUT	out, sym_out;
	size_t		br;
	int		fd = 0;

	if (argc > 1) {
		fd = open(argv[1], O_RDONLY);
	}

	br = read(fd, &in1, sizeof(in1));
	if (br != sizeof(in1)) return 6;

	br = read(fd, &in2, sizeof(in2));
	if (br != sizeof(in2)) return 7;

	br = read(fd, &sym_out, sizeof(sym_out));
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
