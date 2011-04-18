//PRUNECOUNT 8

void foo() {
	printf("hi");
}

int bar(int n[], int a, int b, int c, int d) {
	printf("hello");
	int x = n[0];
	int y = n[1];
	int z = n[2];
	int k = 0;
	if (x) {
		k = x + y + z;
	}
	else {
		k = x + y;
	}
	return k;
}

int main(char** argv, int argc) {
	int c, x;
	int n[] = {1, 2, 3, 4, 5};

	klee_make_symbolic(&c,sizeof(c));	

	if (c) {
		x = 1;
	}
	else {
		x = 2;
	}

	foo();
	bar(n, 1, 2, 3, 4);

	return x;
}
