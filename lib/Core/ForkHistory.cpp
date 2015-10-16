#include <unordered_map>
#include <llvm/Support/CommandLine.h>
#include <iostream>
#include "klee/util/ExprVisitor.h"
#include "klee/util/ExprTimer.h"
#include "ForkHistory.h"
#include "Forks.h"

using namespace klee;

llvm::cl::opt<unsigned> ForkCondIdxMod("forkcond-idx-mod", llvm::cl::init(4));

typedef std::unordered_map<Expr::Hash, ref<Expr> > xtion_map;

class MergeArrays : public ExprVisitor
{
public:
	MergeArrays(void) : ExprVisitor(false, true) { use_hashcons = false; }
	virtual ~MergeArrays(void) {}

	ref<Expr> apply(const ref<Expr>& e) override
	{
		ref<Expr>	ret;
		assert (!Expr::errors);

		ret = ExprVisitor::apply(e);

		/* must have changed a zero inside of a divide. oops */
		if (Expr::errors) {
			Expr::errors = 0;
			std::cerr << "[MergeArrays] Fixing up ExprError\n";
			return MK_CONST(0xff, 8);
		}

		return ret;
	}
protected:
	Action visitConstant(const ConstantExpr& ce) override
	{
		if (ce.getWidth() > 64)
			return Action::skipChildren();

		return Action::changeTo(
			MK_CONST(
				ce.getZExtValue() & 0x800000000000001f,
				ce.getWidth()));
	}

	Action visitRead(const ReadExpr& r) override
	{
		ref<Array>		arr = r.getArray();
		ref<Array>		repl_arr;
		std::string		merge_name;
		const ConstantExpr	*ce_re_idx;
		unsigned		num_run, new_re_idx;

		for (num_run = 0; arr->name[num_run]; num_run++) {
			if (	arr->name[num_run] >= '0' &&
				arr->name[num_run] <= '9')
			{
				break;
			}
		}

		if (num_run == arr->name.size())
			return Action::skipChildren();

		merge_name = arr->name.substr(0, num_run);
		auto it = merge_arrs.find(merge_name);
		if (it != merge_arrs.end()) {
			repl_arr = it->second;
		} else {
			repl_arr = Array::create(merge_name, 1024);
			merge_arrs.emplace(merge_name, repl_arr);
		}

		new_re_idx = 0;
		ce_re_idx = dyn_cast<ConstantExpr>(r.index);
		if (ce_re_idx != NULL) {
			/* XXX: this loses information about structures
			 * since fields will alias but it is good for strings
			 * since it catches all idxmod-graphs */
			new_re_idx = ce_re_idx->getZExtValue() % ForkCondIdxMod;
		}

		return Action::changeTo(
			MK_READ(UpdateList(repl_arr, NULL),
				MK_CONST(new_re_idx, 32)));
	}
private:
	typedef std::unordered_map<std::string, ref<Array> > mergearr_t;
	mergearr_t	merge_arrs;
};


ForkHistory::ForkHistory()
	: condFilter(std::make_unique<ExprTimer<MergeArrays>>(1000))
{}

ForkHistory::~ForkHistory() {}

void ForkHistory::trackTransition(const ForkInfo& fi)
{
	static xtion_map	eh;

	/* Track forking condition transitions */
	for (unsigned i = 0; i < fi.size(); i++) {
		if (!fi.res[i]) continue;

		ref<Expr> new_cond;
		auto x_it = eh.find(fi.conditions[i]->hash());
		if (x_it == eh.end()) {
			new_cond = condFilter->apply(fi.conditions[i]);
			x_it = eh.emplace(
				fi.conditions[i]->hash(), new_cond).first;
		}

		new_cond = x_it->second;
		if (new_cond.isNull())
			continue;

		auto cur_st = fi.resStates[i];
		if (cur_st->prevForkCond.isNull() == false) {
			condXfer.emplace(cur_st->prevForkCond, new_cond);
			hasSucc.insert(cur_st->prevForkCond->hash());
		}

		cur_st->prevForkCond = new_cond;
	}
}

bool ForkHistory::hasSuccessor(const ExecutionState& st) const
{ return hasSucc.count(st.prevForkCond->hash()) != 0; }

/* XXX memoize? */
bool ForkHistory::hasSuccessor(const ref<Expr>& cond) const
{
	ref<Expr>	e(condFilter->apply(cond));
	if (e.isNull())
		return true;
	return hasSucc.count(e->hash());
}
