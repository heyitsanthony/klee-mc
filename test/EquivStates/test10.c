//PRUNECOUNT 0

int foo(int* cp) {
        if (*cp == 0) {
                return *cp + 1;
        }
        else {
		printf("hi");
                return *cp + 1;
        }
}

int main(char** argv, int argc) {	
	int c;
	char a;

	klee_make_symbolic(&c, sizeof(c));
	
	foo(&c);
	
	if (c) {
		return 0;
	}	
	else {
		return 1;
	}
}
