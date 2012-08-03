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
protected:
	ShadowObjectState(unsigned size) : UnboxingObjectState(size) {}
	ShadowObjectState(unsigned size, const ref<Array> &array)
	: UnboxingObjectState(size, array) {}
	ShadowObjectState(const ObjectState &os)
	: UnboxingObjectState(os) {}
	virtual ~ShadowObjectState() {}

	virtual void write8(unsigned offset, ref<Expr>& value);
};
}

#endif