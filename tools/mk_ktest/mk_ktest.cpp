#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "klee/Internal/ADT/zfstream.h"

#include <iostream>

#include "klee/Internal/ADT/KTest.h"

static void push_obj(KTest *kts, char *fname)
{
	KTestObject *o = &kts->objects[kts->numObjects++];
	char		*obj_name;
	struct stat	s;
	FILE		*f;

	obj_name = strrchr(fname, '/');
	obj_name = (obj_name == NULL)
		? fname
		: obj_name + 1;

	if (stat(fname, &s) != 0) {
		std::cerr << "Could not stat " << fname << '\n';
		abort();
	}

	o->name = new char[strlen(obj_name) + 1];
	strcpy(o->name, obj_name);
	o->numBytes = s.st_size;
	o->bytes = new unsigned char[o->numBytes];

	f = fopen(fname, "rb");
	fread(o->bytes, o->numBytes, 1, f);
	fclose(f);
}


int main(int argc, char *argv[])
{
	int		arg_c;
	char		**argv_out;
	std::ostream	*os;
	const char	*out_fname;

	if (argc < 2) {
		std::cerr << "Usage: "
			<< argv[0] << " <symfile1> <symfile2> ...\n";
		std::cerr << "Prefix command line argument files with argv!\n";
		return 1;
	}

	KTest	kts;

	kts.numObjects = 0;
	kts.objects = new KTestObject[argc - 1];
	for (int i = 1; i < argc; i++)
		push_obj(&kts, argv[i]);

	/* setup ktest arg stuff */
	arg_c = 1; /* include bin arg */
	for (int i = 1; i < argc; i++) {
		if (strncmp("argv", argv[i], 4) != 0) break;
		arg_c++;
	}
	
	argv_out = new char*[arg_c+1];
	argv_out[0] = strdup("exe");
	for (int i = 1; i < arg_c; i++) argv_out[i] = (char*)kts.objects[i-1].bytes;
	argv_out[arg_c] = NULL;

	kts.numArgs = arg_c;
	kts.args = argv_out;
	kts.symArgvs = arg_c;
	kts.symArgvLen = 0;

	out_fname = getenv("KTEST_OUTFILE");
	if (out_fname == NULL) out_fname = "out.ktest.gz";

	os = new gzofstream(
		out_fname,
		std::ios::out | std::ios::trunc | std::ios::binary);

	if (!kts.toStream(*os)) abort();
	delete os;
	
	return 0;
}
