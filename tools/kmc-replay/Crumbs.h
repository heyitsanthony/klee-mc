#ifndef KMC_CRUMBS_H
#define KMC_CRUMBS_H

#include <set>
#include <stdio.h>

struct breadcrumb;

/* convenient way to read breadcrumb files */
class Crumbs
{
public:
	static Crumbs* create(const char* fname);
	struct breadcrumb* next(void);
	struct breadcrumb* next(unsigned int bc_type);

	struct breadcrumb* peek(void);
	void skip(unsigned int i = 1);

	bool hasType(unsigned int v) const;
	unsigned int getNumProcessed(void) const { return crumbs_processed; }
	static void freeCrumb(struct breadcrumb* bs);
protected:
	Crumbs(const char* fname);
	virtual ~Crumbs();
private:
	void 			loadTypeList();
	FILE*			f;
	std::set<unsigned int>	bc_types;
	unsigned int		crumbs_processed;
};

#endif
