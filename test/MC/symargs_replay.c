// RUN: gcc %s -O0 -o %t1
// RUN: rm -rf guest-%t1
// RUN: ../../../scripts/save_guest.sh ./%t1 guest-%t1
// RUN: klee-mc -pipe-solver -symargc -symargs - ./%t1 2 aaaaa aaaaaa aaaaa 2>%t1.err >%t1.out
// RUN: ls klee-last | not grep err
// RUN: ktest-tool klee-last/test000001.ktest.gz | grep argv
// RUN: rm -rf %t1.replay.out*
// RUN: kmc-replay 1 2>%t1.replay.out1
// RUN: kmc-replay 2 2>%t1.replay.out2
// RUN: kmc-replay 3 2>%t1.replay.out3
// RUN: kmc-replay 4 2>%t1.replay.out4
// RUN: kmc-replay 5 2>%t1.replay.out5
// RUN: grep 0xbeef %t1.replay.out1  %t1.replay.out2 %t1.replay.out3 %t1.replay.out4 %t1.replay.out5
// RUN: grep 0xc0de %t1.replay.out1  %t1.replay.out2 %t1.replay.out3 %t1.replay.out4 %t1.replay.out5
// RUN: grep 0xbabe %t1.replay.out1  %t1.replay.out2 %t1.replay.out3 %t1.replay.out4 %t1.replay.out5
// RUN: grep 0xf00f %t1.replay.out1  %t1.replay.out2 %t1.replay.out3 %t1.replay.out4 %t1.replay.out5
// RUN: grep 0xc0ffee %t1.replay.out1  %t1.replay.out2 %t1.replay.out3 %t1.replay.out4 %t1.replay.out5
// RUN: rm -rf guest-%t1
//
int main(int argc, char* argv[])
{
	if (argc == 0) return 1;
	if (argc == 1) return 0xbeef;
	if (argc == 2) return (argv[1][0] == '0') ? 0xc0de : 0xbabe;
	if (argc == 3) return 0xf00f;
	return 0xc0ffee;
}