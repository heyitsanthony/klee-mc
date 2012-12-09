#include "klee/Internal/ADT/KTSFuzz.h"

#include <stdlib.h>
#include <assert.h>
#include <vector>

using namespace klee;

KTSFuzz::KTSFuzz(KTest* kt)
: KTestStream(kt)
{}

void KTSFuzz::fuzz(unsigned obj_n, unsigned byte_c)
{
	assert (obj_n < kt->numObjects);
	assert (byte_c <= kt->objects[idx].numBytes);

	std::vector<bool>	v(byte_c);
	unsigned		max_byte;
	unsigned int		i;

	max_byte = kt->objects[idx].numBytes;
	for (i = 0; i < byte_c; i++) v[i] = true;
	for (; i < max_byte; i++) v[i] = false;
	for (i = 0; i < max_byte; i++) {
		bool	t = v[i];
		int	r = rand() % max_byte;
		v[i] = v[r];
		v[r] = t;
	}

	for (i = 0; i < max_byte; i++) {
		if (!v[i]) continue;
		kt->objects[idx].bytes[i] = rand();
	}
}

void KTSFuzz::fuzzPart(unsigned obj_n, double percent)
{
	assert (obj_n < kt->numObjects);
	fuzz(obj_n, kt->objects[idx].numBytes*percent);
}

KTSFuzz* KTSFuzz::create(const char* file)
{
	KTest	*kt = kTest_fromFile(file);
	if (kt == NULL) return NULL;
	return new KTSFuzz(kt);
}
