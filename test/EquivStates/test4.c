//PRUNECOUNT 1

int main(char** argv, int argc) {	
	int c, d;
	char a, b;

	klee_make_symbolic(&c, sizeof(c));

	d = c + 1; 

	if (c == 0) {
		a = 1;
	}
	else {
		a = 2;
	}

	if (d == 1) {
		b = 2;
	}
	else {
		b = 3;
	}
  
	return 0;
}
