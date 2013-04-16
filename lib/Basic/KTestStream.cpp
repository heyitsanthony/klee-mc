#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "klee/Internal/ADT/KTestStream.h"

using namespace klee;

KTestStream::KTestStream(KTest* in_kt)
: idx(0)
, kt(in_kt)
{ assert (kt != NULL); }

KTestStream::~KTestStream(void)
{
	kTest_free(kt);
}

char* KTestStream::feedObjData(unsigned int sz)
{
	char			*obj_buf;
	const KTestObject	*cur_obj;
	
	cur_obj = nextObject();
	if (cur_obj == NULL) {
		/* request overflow */
		fprintf(stderr, "KTestStream: OF\n");
		return NULL;
	}

	if (cur_obj->numBytes != sz) {
		/* out of sync-- how to handle? */
		fprintf(stderr, "KTestStream: OOSYNC: Expected: %d. Got: %d\n",
			sz,
			cur_obj->numBytes);

		if (sz != ~0U)
			return NULL;
		else
			sz = cur_obj->numBytes;
	}

	obj_buf = new char[sz];
	memcpy(obj_buf, cur_obj->bytes, sz);
	return obj_buf;
}

KTestStream* KTestStream::create(const char* file)
{
	KTest		*kt = kTest_fromFile(file);
	KTestStream	*kts;

	if (kt == NULL) return NULL;

	kts = new KTestStream(kt);
	kts->fname = file;
	return kts;
}
