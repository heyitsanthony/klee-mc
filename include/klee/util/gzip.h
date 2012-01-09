#ifndef KLEE_GZIP_H
#define KLEE_GZIP_H

#include <stdio.h>

namespace klee
{
class GZip
{
public:
	static bool gzipFile(const char* src, const char* dst);
	static bool gunzipFile(const char* src, const char* dst);
	static FILE* gunzipTempFile(const char* src);
private:
};
}

#endif