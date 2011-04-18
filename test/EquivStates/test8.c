void goal() {
	printf("hi");
}

void dbgcall() {
	printf("dbg");
}

int main(char** argv, int argc) {		

//	printf("GOAL1");
	dbgcall();	
//	goal();	 
if (argc == 2)
    {
	goal();
/*      if (strcmp (argv[1], "--version") == 0) {
		printf("HIT");
	}*/
		
    }
}
