//PRUNECOUNT 2

#include <stdio.h>

void foo(int c, int d) {
	int a,b,e;

	if (c) {
		if (d) {
			a = 1;
		}else {
			e = 3;
		}
	}
	else {
		b = 1;
	}
}

int main(char** argv, int argc) {	
	int c,d;
	char a;

	klee_make_symbolic(&c, sizeof(c));
	klee_make_symbolic(&d, sizeof(c));
	klee_assume_slt(c, 10);
	
	foo(c,d);
 
	return 0;
}
