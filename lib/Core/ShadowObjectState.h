#ifndef SHADOWOBJECTSTATE_H
#define SHADOWOBJECTSTATE_H

#include "UnboxingObjectState.h"

namespace klee
{
class ShadowObjectState : public UnboxingObjectState
{
friend class ObjectStateFactory<ShadowObjectState>;
public:
	virtual void write(unsigned offset, const ref<Expr>& value);
	void taintAccesses(uint64_t taint_v);
	ref<Expr> read8(unsigned offset) const;

	bool isClean(void) const { return tainted_bytes == 0; }
	bool isByteTainted(unsigned s) const;

protected:
	ShadowObjectState(unsigned size)
	: UnboxingObjectState(size)
	, is_tainted(false), tainted_bytes(0) {}
	ShadowObjectState(unsigned size, const ref<Array> &array)
	: UnboxingObjectState(size, array)
	, is_tainted(false) , tainted_bytes(0) {}

	ShadowObjectState(const ObjectState &os);

	virtual ~ShadowObjectState() {}

	virtual void write8(unsigned offset, ref<Expr>& value);
	virtual ref<Expr> read8(ref<Expr> offset) const;

private:
	bool		is_tainted;
	uint64_t	taint_v;
	unsigned	tainted_bytes; /* a hint */
};
}

#endif