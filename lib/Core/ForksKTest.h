#ifndef FORKSKTEST_H
#define FORKSKTEST_H

#include "klee/Internal/Support/Timer.h"
#include "Forks.h"

struct KTest;

namespace klee
{
class ForksKTest : public Forks
{
public:
	ForksKTest(Executor& exe)
	: Forks(exe), kt(0)
	, make_err_tests(true)
	, cheap_fork_c(0) {}

	virtual ~ForksKTest();
	void setKTest(const KTest* _kt, const ExecutionState* es = NULL);
	void setMakeErrTests(bool v) { make_err_tests = v; }
	unsigned getCheapForks(void) const { return cheap_fork_c; }

protected:
	bool setupForkAffinity(
		ExecutionState& current,
		const struct ForkInfo& fi,
		unsigned* cond_idx_map) override;

	bool evalForks(ExecutionState& current, struct ForkInfo& fi) override;

	bool isBadOverflow(ExecutionState& current);
	virtual bool updateSymbolics(ExecutionState& current);
	virtual void addBinding(ref<Array>& a, std::vector<uint8_t>& v);
	const Assignment* getCurrentAssignment(void) const {
		return kt_assignment.get();
	}

	int findCondIndex(const struct ForkInfo& fi, bool& non_const);

private:
	const KTest	*kt;
	std::unique_ptr<Assignment> kt_assignment;
	std::vector<ref<Array> > arrs;
	unsigned	base_objs;
	bool		make_err_tests;
	unsigned	cheap_fork_c;
	WallTimer	wt;

};

/* this is a smarter version of ForksKTest which caches states on update
 * if a new state comes up with a matching object stream head,
 * we can jump to it quickly */
class ForksKTestStateLogger : public ForksKTest
{
public:
	ForksKTestStateLogger(Executor& exe) : ForksKTest(exe) {}
	virtual ~ForksKTestStateLogger();
	ExecutionState* getNearState(const KTest* _kt);
protected:
	virtual bool updateSymbolics(ExecutionState& current);
	virtual void addBinding(ref<Array>& a, std::vector<uint8_t>& v);
private:
	typedef std::map<const Assignment, ExecutionState*> statecache_ty;
	typedef std::pair<unsigned, const std::string>	arrkey_ty;
	typedef std::map<arrkey_ty, ref<Array> >	arrcache_ty;

	arrcache_ty	arr_cache;
	statecache_ty	state_cache;
};
}

#endif
