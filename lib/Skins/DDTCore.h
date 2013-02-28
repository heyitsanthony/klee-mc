#ifndef DDTCORE_H
#define DDTCORE_H

namespace llvm
{
class Function;
}

namespace klee
{
class Executor;
class ShadowMix;
class DDTCore
{
public:
	DDTCore();
	virtual ~DDTCore(void) {}
	ref<Expr> mixLeft(const ref<Expr>& l, const ref<Expr>& r);

	void taintFunction(llvm::Function* f);
private:
	ShadowMix	*sm;
};
}

#endif
