#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "klee/Internal/ADT/KTestStream.h"

using namespace klee;

#define PFX	"[KTestStream] "
#define DEBUG(x)

KTestStream::KTestStream(KTest* in_kt)
: idx(0)
, kt(in_kt)
{ assert (kt != NULL); }

KTestStream::~KTestStream(void) { delete kt; }

char* KTestStream::feedObjData(unsigned int sz)
{
	char			*obj_buf;
	const KTestObject	*cur_obj;
	
	cur_obj = nextObject();
	if (cur_obj == NULL) {
		/* request overflow */
		fprintf(stderr, PFX "Overflow. Expected sz=%d\n", sz);
		return NULL;
	}

	if (cur_obj->numBytes != sz) {
		/* out of sync-- how to handle? */
		fprintf(stderr, PFX "OOSYNC: Expected: %d. Got: %d\n",
			sz,
			cur_obj->numBytes);

		if (sz != ~0U)
			return NULL;

		sz = cur_obj->numBytes;
	}

	obj_buf = new char[sz];
	memcpy(obj_buf, cur_obj->bytes, sz);
	DEBUG(fprintf(stderr, PFX "GOT OBJ: sz=%d (idx=%d)\n", sz, idx));
	return obj_buf;
}

KTestStream* KTestStream::create(const char* file)
{
	KTest		*kt = KTest::create(file);
	KTestStream	*kts;

	if (kt == NULL) return NULL;

	kts = new KTestStream(kt);
	kts->fname = file;
	return kts;
}


const KTestObject* KTestStream::nextObject(void)
{
	const KTestObject	*ret;
	ret = peekObject();
	idx++;
	DEBUG(fprintf(stderr, PFX "nextObj(). idx=%d\n", idx));
	return ret;
}

KTestStream* KTestStream::copy(void) const
{
	KTestStream	*kts;
	kts = create(fname.c_str());
	DEBUG(fprintf(stderr, PFX "copyObj idx=%d\n", idx));
	kts->idx = idx;
	return kts;
}
