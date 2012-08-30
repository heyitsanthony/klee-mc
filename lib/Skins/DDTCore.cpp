#include "../Core/Executor.h"
#include "klee/ExecutionState.h"
#include "DDTCore.h"
#include "ShadowObjectState.h"
#include "../Expr/ShadowAlloc.h"
#include "../Expr/ShadowBuilder.h"


using namespace klee;


class FSet : public std::set<KFunction*>
{
public:
	//uint64_t	refCount;
	const FSet& operator|(const FSet& fs) const
	{
		if (fs == *this) return *this;
		assert (0 == 1);
	}
private:
};

typedef ShadowValT<FSet> ShadowValFSet;

namespace klee {
template<> ShadowValFSet::svcache_ty	ShadowValFSet::cache =
	ShadowValFSet::svcache_ty();
}

DDTCore::DDTCore(Executor* _exe)
: exe(_exe)
{
	ExprAlloc	*ea;

	ea = Expr::getAllocator();
	Expr::setAllocator(new ShadowAlloc());
	delete ea;

	sm = new ShadowMixOr();
	Expr::setBuilder(ShadowBuilder::create(Expr::getBuilder(), sm));

	delete ObjectState::getAlloc();
	ObjectState::setAlloc(new ObjectStateFactory<ShadowObjectState>());
}

ref<Expr> DDTCore::mixLeft(const ref<Expr>& lhs, const ref<Expr>& rhs)
{
	ref<Expr>	ret;
	ref<ShadowVal>	l_sv, r_sv;

	if (rhs->isShadowed() == false)
		return lhs;

	r_sv = ShadowAlloc::getExprShadow(rhs);

	l_sv = ShadowAlloc::getExprShadow(lhs);
	if (l_sv.isNull()) {
		l_sv = r_sv;
	} else {
		l_sv = sm->mix(l_sv, r_sv);
	}

	PUSH_SHADOW(l_sv)
	ret = lhs->realloc();
	POP_SHADOW

	return ret;
}


void DDTCore::taintFunction(llvm::Function* f)
{
	assert (0 == 1);
}
