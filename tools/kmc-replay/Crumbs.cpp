#include <klee/breadcrumb.h>
#include <string.h>
#include <assert.h>
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
, crumbs_processed(0)
{
	f = fopen(fname, "rb");
	if (f == NULL) return;

	/* XXX: load on demand */
	loadTypeList();
}

Crumbs::~Crumbs()
{
	if (f != NULL) fclose(f);
}

void Crumbs::skip(unsigned int i)
{
	assert (i);
	while (i) {
		struct breadcrumb* bc = next();
		if (!bc) break;
		freeCrumb(bc);
		i--;
	}
}

struct breadcrumb* Crumbs::peek(void)
{
	struct breadcrumb	hdr;
	char			*ret;
	size_t			br;
	int			err;

	if (feof(f)) return NULL;

	br = fread(&hdr, sizeof(hdr), 1, f);
	if (br != 1) return NULL;

	ret = new char[hdr.bc_sz];
	memcpy(ret, &hdr, sizeof(hdr));

	br = fread(ret + sizeof(hdr), hdr.bc_sz - sizeof(hdr), 1, f);
	if (br != 1) return NULL;

	err = fseek(f, -((long)hdr.bc_sz), SEEK_CUR);
	assert (err == 0);

	return reinterpret_cast<struct breadcrumb*>(ret);
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

	crumbs_processed++;
	return reinterpret_cast<struct breadcrumb*>(ret);
}

void Crumbs::freeCrumb(struct breadcrumb* bc)
{
	delete [] reinterpret_cast<char*>(bc);
}

struct breadcrumb* Crumbs::next(unsigned int bc_type)
{
	struct breadcrumb	*bc;
	while ((bc = next()) != NULL) {
		if (bc->bc_type == bc_type)
			return bc;
		Crumbs::freeCrumb(bc);
	}
	return bc;
}

bool Crumbs::hasType(unsigned int v) const
{
	return (bc_types.count(v) != 0);
}

void Crumbs::loadTypeList(void)
{
	long			old_off;
	struct breadcrumb	*cur_bc;
	unsigned int		old_processed;

	assert (bc_types.size() == 0);

	old_processed = crumbs_processed;
	old_off = ftell(f);
	rewind(f);

	while ((cur_bc = next()) != NULL) {
		bc_types.insert(cur_bc->bc_type);
		freeCrumb(cur_bc);
	}

	fseek(f, old_off, SEEK_SET);
	crumbs_processed = old_processed;
}
