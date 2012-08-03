#include "Memory.h"
#include "UnboxingObjectState.h"

using namespace klee;

void UnboxingObjectState::write8(unsigned offset, ref<Expr>& value)
{
	// can happen when ExtractExpr special cases
	if (ConstantExpr *CE = dyn_cast<ConstantExpr>(value)) {
		ObjectState::write8(offset, (uint8_t) CE->getZExtValue(8));
		return;
	}

	ObjectState::write8(offset, value);
}

#define writeN(n)		\
do {				\
unsigned NumBytes = n/8;	\
for (unsigned i = 0; i != NumBytes; ++i) {	\
	unsigned idx;	\
	idx = Context::get().isLittleEndian() ? i : (NumBytes - i - 1);	\
	ObjectState::write8(offset + idx, (uint8_t) (val >> (8 * i)));	\
}	\
} while (0)


void UnboxingObjectState::write(unsigned offset, const ref<Expr>& value)
{
	// Check for writes of constant values.
	if (ConstantExpr *CE = dyn_cast<ConstantExpr>(value)) {
		Expr::Width w = CE->getWidth();
		if (w <= 64) {
			uint64_t val = CE->getZExtValue();
			switch (w) {
			default: assert(0 && "Invalid write size!");
			case Expr::Bool:
			case Expr::Int8:
				ObjectState::write8(offset, val);
				return;
			case Expr::Int16: writeN(16); return;
			case Expr::Int32: writeN(32); return;
			case Expr::Int64: writeN(64); return;
			}
		}
	}

	ObjectState::write(offset, value);
}
