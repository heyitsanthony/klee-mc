#ifndef KLEE_ASSIGNHASH_H
#define KLEE_ASSIGNHASH_H

#include "klee/Expr.h"
#include "klee/util/Assignment.h"

namespace klee
{
class Constraints;

class AssignHash
{
public:
	AssignHash(const ref<Expr>& _e)
	: e(_e), a(e), hash_off(1), hash(0), maybe_const(true)
	{}

	virtual ~AssignHash() {}
	void commitAssignment()
	{
		uint64_t	cur_eval_hash;

		cur_eval_hash = a.evaluate(e)->hash();
		if (hash_off == 1)
			last_eval_hash = cur_eval_hash;

		/* XXX: update this to use murmur too.
		 * I don't trust this mixing function. */
		hash ^= (cur_eval_hash + hash_off)*(hash_off);
		hash_off++;

		if (last_eval_hash != cur_eval_hash)
			maybe_const = false;

		last_eval_hash = cur_eval_hash;
		a.resetBindings();
	}

	Assignment& getAssignment() { return a; }
	uint64_t getHash(void) const { return hash; }
	bool maybeConst(void) const { return maybe_const; }

	static uint64_t getEvalHash(const ref<Expr>& e, bool& maybeConst);
	static uint64_t getEvalHash(const ref<Expr>& e)
	{ bool mc; return getEvalHash(e, mc); }

	static uint64_t getConstraintHash(const ConstraintManager& c);

private:
	const ref<Expr>	&e;
	Assignment	a;
	unsigned	hash_off;
	uint64_t	hash;
	uint64_t	last_eval_hash;
	bool		maybe_const;
};
}

#endif
