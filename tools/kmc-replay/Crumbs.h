#ifndef CRUMBS_H
#define CRUMBS_H

#include <stdio.h>

struct breadcrumb;

class Crumbs
{
public:
	static Crumbs* create(const char* fname);
	struct breadcrumb* next(void);
	struct breadcrumb* next(unsigned int bc_type);
protected:
	Crumbs(const char* fname);
	virtual ~Crumbs();
private:
	FILE*	f;
};

#endif
