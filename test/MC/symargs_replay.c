// RUN: gcc %s -O0 -o %t1
// RUN: rm -rf guest-%t1
// RUN: ../../../scripts/save_guest.sh ./%t1 guest-%t1
// RUN: klee-mc -pipe-solver -symargc -symargs - ./%t1 2 aaaaa aaaaaa aaaaa >%t1.err >%t1.out
// RUN: ls klee-last | not grep err
// RUN: kmc-replay 1 2>%t1.replay.out
// RUN: grep Exit %t1.replay.out
// RUN: rm -rf guest-%t1
//
int main(int argc, char* argv[])
{
	if (argc == 0) return 1;
	if (argc == 1) return 999;
	if (argc == 2) return (argv[1] == '0') ? 11111 : 22222;
	if (argc == 3) return 123123;
	return 123;
}