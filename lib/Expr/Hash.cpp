////////
//
// Simple hash functions for various kinds of Exprs
//
///////
#include <llvm/IR/Function.h>
#include "klee/Expr.h"

using namespace klee;

Expr::Hash Expr::computeHash(void)
{
	int		n = getNumKids();
	uint64_t	dat[2*n+1];

	for (int i = 0; i < n; i++)
		dat[i] = getKid(i)->skeleton();
	dat[n] = getKind();
	for (int i = 0; i < n; i++)
		dat[i+n+1] = getKid(i)->hash();

	skeletonHash = hashImpl(&dat, (n+1)*8, 0);
	hashValue = hashImpl(&dat[n], (n+1)*8, 0);
	return hashValue;
}

/* transparent */
Expr::Hash NotOptimizedExpr::computeHash(void)
{
	skeletonHash = src->skeleton();
	hashValue = src->hash();
	return hashValue;
}

Expr::Hash BindExpr::computeHash(void)
{
	uint64_t dat[3] = {
		let_expr->skeleton(),
		(uint64_t)getKind(),
		let_expr->hash() };
	skeletonHash = hashImpl(&dat, 16, 0);
	hashValue = hashImpl(&dat[1], 16, 0);
	return hashValue;
}

Expr::Hash CastExpr::computeHash(void)
{
	uint64_t dat[4] = {
		src->skeleton(),
		getWidth(), (uint64_t)getKind(),
		src->hash()};
	skeletonHash = hashImpl(&dat, 3*8, 0);
	hashValue = hashImpl(&dat[1], 3*8, 0);
	return hashValue;
}

Expr::Hash ExtractExpr::computeHash(void)
{
	uint64_t dat[5] = {
		expr->skeleton(),
		Expr::Extract, offset, getWidth(),
		expr->hash() };
	skeletonHash = hashImpl(&dat, 4*8, 0);
	hashValue = hashImpl(&dat[1], 4*8, 0);
	return hashValue;
}

/* skeletal hash ignores updates AND indices --
 * this makes sense since the common case (read 0 x) and (read 1 x)
 * are indistinguishable because h(x) = h(y).
 * */
Expr::Hash ReadExpr::computeHash(void)
{
	uint64_t dat[3] = {Expr::Read, index->hash(), updates.hash()};

	skeletonHash = Expr::Read;
	hashValue = hashImpl(&dat, 3*8, 0);
	return hashValue;
}

Expr::Hash NotExpr::computeHash(void)
{
	uint64_t dat[3] = {expr->skeleton(), Expr::Not, expr->hash()};
	skeletonHash = hashImpl(&dat, 2*8, 0);
	hashValue = hashImpl(&dat[1], 2*8, 0);
	return hashValue;
}

unsigned MallocKey::hash(void) const
{
	unsigned long		alloc_v;
	const llvm::Instruction	*ins;

	if (hash_v != 0)
		return hash_v;

	ins = dyn_cast<llvm::Instruction>(allocSite);
	if (ins == NULL) {
		alloc_v = 1;
	} else {
		/* before we were using the pointer value from allocSite.
		 * this broke stuff horribly, so use bb+func names. */
		std::string	s_bb, s_f;

		s_bb = ins->getParent()->getName().str();
		s_f = ins->getParent()->getParent()->getName().str();
		alloc_v = 0;
		for (unsigned int i = 0; i < s_bb.size(); i++)
			alloc_v = alloc_v*33+s_bb[i];
		for (unsigned int i = 0; i < s_f.size(); i++)
			alloc_v = alloc_v*33+s_f[i];
	}

	uint32_t dat[2] = {(uint32_t)alloc_v, iteration};
	hash_v = Expr::hashImpl(&dat, sizeof(dat), 0);
	return hash_v;
}

#include "murmur3.h"
Expr::Hash Expr::hashImpl(const void* data, size_t len, Hash hash)
{
	Hash	ret[128/(sizeof(Hash)*8)];
	memset(ret, 0, sizeof(ret));
	MurmurHash3_x64_128(data, len, hash, &ret);
	return ret[0];
}
