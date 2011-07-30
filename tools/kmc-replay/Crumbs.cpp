#include <klee/breadcrumb.h>
#include <string.h>
#include "Crumbs.h"

Crumbs* Crumbs::create(const char* fname)
{
	Crumbs	*ret = new Crumbs(fname);
	if (ret->f == NULL) {
		delete ret;
		ret = NULL;
	}

	return ret;
}

Crumbs::Crumbs(const char* fname)
: f(NULL)
{
	f = fopen(fname, "rb");
}

Crumbs::~Crumbs()
{
	if (f != NULL) fclose(f);
}

struct breadcrumb* Crumbs::next(void)
{
	struct breadcrumb	hdr;
	char			*ret;
	size_t			br;

	if (feof(f)) return NULL;

	br = fread(&hdr, sizeof(hdr), 1, f);
	if (br != 1) return NULL;

	ret = new char[hdr.bc_sz];
	memcpy(ret, &hdr, sizeof(hdr));

	br = fread(ret + sizeof(hdr), hdr.bc_sz - sizeof(hdr), 1, f);
	if (br != 1) return NULL;

	return reinterpret_cast<struct breadcrumb*>(ret);
}

struct breadcrumb* Crumbs::next(unsigned int bc_type)
{
	struct breadcrumb	*bc;
	while ((bc = next()) != NULL) {
		if (bc->bc_type == bc_type)
			return bc;
		delete bc;
	}
	return bc;
}
