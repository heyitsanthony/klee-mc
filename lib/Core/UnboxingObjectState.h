#ifndef UNBOXINGOBJECTSTATE_H
#define UNBOXINGOBJECTSTATE_H

#include "Memory.h"

namespace klee
{
class UnboxingObjectState : public ObjectState
{
friend class ObjectStateFactory<UnboxingObjectState>;

public:
	virtual void write(unsigned offset, const ref<Expr>& value);
protected:
	UnboxingObjectState(unsigned size) : ObjectState(size) {}
	UnboxingObjectState(unsigned size, const ref<Array> &array)
	: ObjectState(size, array) {}
	UnboxingObjectState(const ObjectState &os)
	: ObjectState(os) {}
	virtual ~UnboxingObjectState() {}

	virtual void write8(unsigned offset, ref<Expr>& value);

};
}

#endif
