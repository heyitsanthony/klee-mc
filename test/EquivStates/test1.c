//PRUNECOUNT 1

int main(char** argv, int argc) {	
	int c;
	char a;

	klee_make_symbolic(&c, sizeof(c));

	if (c == 0) {
		a = 1;
	}
	else {
		a = 2;
	}
  
	return 0;
}
