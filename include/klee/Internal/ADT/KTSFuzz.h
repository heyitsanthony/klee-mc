#ifndef __COMMON_KTEST_STREAM_CORRUPT_H
#define __COMMON_KTEST_STREAM_CORRUPT_H

#include "klee/Internal/ADT/KTestStream.h"

namespace klee
{
class KTSFuzz : public KTestStream
{
public:
	static KTSFuzz* create(const char* file);
	void fuzz(unsigned obj_n, unsigned byte_c);
	void fuzzPart(unsigned obj_n, double percent);
	virtual ~KTSFuzz(void) {}

protected:
	KTSFuzz(KTest* kt);
};
}
#endif
