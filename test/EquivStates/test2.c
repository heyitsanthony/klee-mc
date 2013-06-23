//PRUNECOUNT 9
#include <stdio.h>

int main(char** argv, int argc) {	
	int c;
	char a;

	klee_make_symbolic(&c, sizeof(c));
	klee_assume_slt(c, 10);

	int i;
	for (i = 0; i < c; i++) {
		printf("hi");
	}
	//printf("c=%d",c);
 
	return 0;
}
