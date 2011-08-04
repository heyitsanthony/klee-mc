#ifndef KLEE_GZIP_H
#define KLEE_GZIP_H

namespace klee
{
class GZip
{
public:
	static bool gzipFile(const char* src, const char* dst);
	static bool gunzipFile(const char* src, const char* dst);
private:
};
}

#endif