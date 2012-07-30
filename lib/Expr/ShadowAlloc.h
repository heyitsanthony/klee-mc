#ifndef SHADOWALLOC_H
#define SHADOWALLOC_H

#include "ExprAlloc.h"

namespace klee {
class ShadowAlloc : public ExprAlloc
{
public:
	ShadowAlloc() : is_shadowing(false) {}
	virtual ~ShadowAlloc() {}
	void startShadow(uint64_t v) { is_shadowing = true; shadow_v = v; }
	void stopShadow(void) { is_shadowing = false; }

	EXPR_BUILDER_DECL_ALL	

private:
	bool		is_shadowing;
	uint64_t	shadow_v;
};
}

#endif
