//===-- SpecialFunctionHandler.cpp ----------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#include <llvm/IR/Module.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/IR/LLVMContext.h>
#include "klee/klee.h"
#include "Memory.h"
#include "SymAddrSpace.h"
#include "SpecialFunctionHandler.h"
#include "StateSolver.h"
#include "Forks.h"

#include "klee/Common.h"
#include "klee/ExecutionState.h"

#include "klee/Internal/Module/KModule.h"
#include "static/Sugar.h"

#include "Executor.h"

#include <sstream>

using namespace llvm;
using namespace klee;

extern bool DebugPrintInstructions;

static ref<Expr>	ce_zero64 = NULL;

SpecialFunctionHandler::SpecialFunctionHandler(Executor* _executor)
: executor(_executor)
{ ce_zero64 = MK_CONST(0, 64); }

void SpecialFunctionHandler::prepare(const HandlerInfo** hinfo)
{
	for (unsigned i = 0; hinfo[i]; i++)
		prepare(hinfo[i]);
}

void SpecialFunctionHandler::prepare(const HandlerInfo* hinfo, unsigned int N)
{
	for (unsigned i=0; i<N; ++i) {
		const HandlerInfo	&hi(hinfo[i]);
		Function		*f;

		f = executor->getKModule()->module->getFunction(hi.name);

		// Can't create function if it doesn't exist;
		// queue for late binding in case it turns up later.
		if (f == NULL) {
			lateBindings[hi.name] = &hinfo[i];
			continue;
		}
		if (!(!hi.doNotOverride || f->isDeclaration())) continue;

		// Make sure NoReturn attribute is set, for optimization and
		// coverage counting.
		if (hi.doesNotReturn)
			f->addFnAttr(Attribute::NoReturn);

		// Change to a declaration since we handle internally (simplifies
		// module and allows deleting dead code).
		if (!f->isDeclaration())
			f->deleteBody();
	}
}


SpecialFunctionHandler::~SpecialFunctionHandler(void)
{
	foreach (it, handlers.begin(), handlers.end()) {
		SFHandler	*h = (it->second).first;
		delete h;
	}
}

void SpecialFunctionHandler::bind(const HandlerInfo* hinfo, unsigned int N)
{ for (unsigned i=0; i<N; ++i) addHandler(hinfo[i]); }

void SpecialFunctionHandler::bind(const HandlerInfo** hinfo)
{ for (unsigned i=0; hinfo[i]; ++i) addHandler(*hinfo[i]); }


SFHandler* SpecialFunctionHandler::addHandler(const struct HandlerInfo& hi)
{
	SFHandler		*old_h, *new_h;
	Function		*f;

	f = executor->getKModule()->module->getFunction(hi.name);
	if (f == NULL) return NULL;

	if (hi.doNotOverride && !f->isDeclaration())
		return NULL;

	old_h = handlers[f].first;
	if (old_h) delete old_h;

	new_h = hi.handler_init(this);
	handlers[f] = std::make_pair(new_h, hi.hasReturnValue);

	return new_h;
}

bool SpecialFunctionHandler::lateBind(const llvm::Function* f)
{
	latebindings_ty::iterator it(lateBindings.find(f->getName().str()));

	if (it == lateBindings.end())
		return false;

	addHandler(*(it->second));
	lateBindings.erase(it);

	return true;
}

void SpecialFunctionHandler::handleByName(
	ExecutionState		&state,
	const std::string	&fname,
	KInstruction		*target,
	std::vector<ref<Expr> >	&args)
{
	Function		*f;

	f = executor->getKModule()->module->getFunction(fname);
	if (f == NULL) {
		Constant	*c;
		c = executor->getKModule()->module->getOrInsertFunction(
			fname,
			FunctionType::get(
				IntegerType::get(getGlobalContext(), 64),
				false));
		f = (Function*)c;
	}

	if (f == NULL) {
		TERMINATE_EXEC(executor, state, "missing indirect call");
		return;
	}

	handle(state, f, target, args, true);
}

bool SpecialFunctionHandler::handle(
	ExecutionState &state,
	Function *f,
	KInstruction *target,
	std::vector< ref<Expr> > &args,
	bool insert_ret_vals)
{
	SFHandler		*h;
	bool			hasReturnValue, missing_ret_val;
	handlers_ty::iterator	it(handlers.find(f));

	if (it == handlers.end()) {
		if (!lateBind(f))
			return false;
		it = handlers.find(f);
		if (it == handlers.end())
			return false;
	}

	h = it->second.first;
	hasReturnValue = it->second.second;

	missing_ret_val = !hasReturnValue && !target->getInst()->use_empty();
	if (missing_ret_val && !insert_ret_vals) {
		TERMINATE_EXEC(executor,
			state,
			"expected return value from void special function");
		return true;
	}

	h->handle(state, target, args);

	if (missing_ret_val && insert_ret_vals)
		state.bindLocal(target, ce_zero64);

	return true;
}

bool SpecialFunctionHandler::hasHandler(Function* f) const
{ return handlers.find(f) != handlers.end(); }

unsigned char* SpecialFunctionHandler::readBytesAtAddressNoBound(
	ExecutionState &state,
	const ref<Expr>& addressExpr,
	unsigned int& len,
	int term_char)
{
	return readBytesAtAddress(state, addressExpr, ~0, len, term_char);
}

unsigned char* SpecialFunctionHandler::readBytesAtAddress(
	ExecutionState &state,
	const ref<Expr> &addressExpr,
	unsigned int maxlen,
	unsigned int& len,
	int term_char)
{
	const MemoryObject	*mo;
	const ObjectState	*os;
	unsigned char		*buf;
	uint64_t		offset;
	ObjectPair		op;
	ref<ConstantExpr>	address;

	addressExpr = executor->toUnique(state, addressExpr);
	address = cast<ConstantExpr>(addressExpr);

	assert (address.get() && "Expected constant address");
	if (!state.addressSpace.resolveOne(address->getZExtValue(), op)) {
		klee_warning("hit multi-res on reading 'concrete' string");
		return NULL;
	}

	mo = op.first;
	os = op.second;

	offset = address->getZExtValue() - op.first->getBaseExpr()->getZExtValue();
	assert (offset < mo->size);

	buf = new unsigned char[(mo->size-offset)+1];

	len = 0;
	for (	unsigned i = offset;
		i < mo->size && (i-offset < maxlen);
		i++)
	{
		ref<Expr> cur;

		cur = state.read8(os, i);
		cur = executor->toUnique(state, cur);
		if (isa<ConstantExpr>(cur) == false) {
			klee_warning(
				"hit sym char on reading 'concrete' string");
			delete [] buf;
			return NULL;
		}

		buf[i-offset] = cast<ConstantExpr>(cur)->getZExtValue(8);
		if ((int)buf[i-offset] == term_char) {
			buf[i-offset] = '\0';
			break;
		}
		len++;
	}

	return buf;
}


// reads a concrete string from memory
std::string SpecialFunctionHandler::readStringAtAddress(
	ExecutionState &state,
	const ref<Expr>& addressExpr)
{
	unsigned char*	buf;
	unsigned int	out_len;
	buf = readBytesAtAddressNoBound(state, addressExpr, out_len, 0);
	if (buf == NULL)
		return "???";
	std::string result((const char*)buf);
	delete[] buf;
	return result;
}

/****/

SFH_DEF_ALL_EX(ResumeExit, "klee_resume_exit", true, false, false)
{
	SFH_CHK_ARGS(0, "ResumeExit");

	if (!state.getOnFini()) {
		TERMINATE_ERROR(sfh->executor,
			state, "klee_resume_exit outside fini", "fini.err");
		return;
	}

	if (state.getFini()->isInteresting(state) == true)
		state.getFini()->process(state);

	sfh->executor->terminate(state);
}

SFH_DEF_ALL_EX(Exit, "exit", true, false, true)
{
	SFH_CHK_ARGS(1, "exit");
	TERMINATE_EXIT(sfh->executor, state);
}

SFH_DEF_ALL_EX(SilentExit, "klee_silent_exit", true, false, false)
{
	assert(args.size()==1 && "invalid number of args to exit");
	sfh->executor->terminate(state);
}

cl::opt<bool> EnablePruning("enable-pruning", cl::init(true));

static std::map<int, int> pruneMap;

SFH_DEF_ALL(GetPruneID, "klee_get_prune_id", true)
{
  if (!EnablePruning) {
    state.bindLocal(target, MK_CONST(0, Expr::Int32));
    return;
  }

  static unsigned globalPruneID = 1;
  SFH_CHK_ARGS(1, "klee_get_prune_id");
  int count = cast<ConstantExpr>(args[0])->getZExtValue();

  state.bindLocal(target, ConstantExpr::create(globalPruneID, Expr::Int32));
  pruneMap[globalPruneID] = count;

  globalPruneID++;
}

SFH_DEF_ALL(Prune, "klee_prune", false)
{
  if (!EnablePruning)
    return;

  assert((args.size()==1 || args.size()==2) &&
	 "Usage: klee_prune(pruneID [,uniqueID])\n");
  int prune_id = cast<ConstantExpr>(args[0])->getZExtValue();

  assert(pruneMap.find(prune_id) != pruneMap.end() &&
	 "Invalid prune ID passed to klee_prune()");

  if (pruneMap[prune_id] == 0)
    sfh->executor->terminate(state);
  else
    pruneMap[prune_id]--;
}

static std::string reporttab2str(
	SpecialFunctionHandler	*sfh,
	ExecutionState		&es,
	const ref<Expr>		&tab_e)
{
	std::stringstream	ss;
	uint64_t		cur_ent;
	const klee::ConstantExpr *ce = dyn_cast<klee::ConstantExpr>(tab_e);

	if (ce == NULL || ce->getZExtValue() == 0)
		return "";

	/* scan the address space of the state
	 * looking for
	 * { u64 keystrp, u64 value }
	 * where keystrp is a concrete pointer to a concrete string
	 * and value may be symbolic
	 */
	cur_ent = ce->getZExtValue();
	ss << "\n{\n";
	do {
		ObjectPair	op;
		ref<Expr>	v;
		std::string	s;
		uint64_t	cur_strp;
		unsigned	n;

		if (es.addressSpace.resolveOne(cur_ent, op) == false)
			break;

		n = es.addressSpace.readConcreteSafe(
			(uint8_t*)&cur_strp, cur_ent, 8);
		if (n != 8 || cur_strp == 0)
			break;

		s = sfh->readStringAtAddress(es, klee::MK_CONST(cur_strp, 64));
		if (s.empty())
			break;

		v = es.read(op_os(op), op_mo(op)->getOffset(cur_ent+8), 64);
		ss << "{ \"" << s << "\" : \"" << v << "\" },\n";
		cur_ent += 16;
	} while(1);
	ss << "}\n";

	return ss.str();
}

SFH_DEF_ALL_EX(ReportError, "klee_report_error", true, false, false)
{
	// (file, line, message, suffix, table)
	SFH_CHK_ARGS(5, "klee_report_error");

	std::string	message = sfh->readStringAtAddress(state, args[2]);
	std::string	suffix = sfh->readStringAtAddress(state, args[3]);

	message += reporttab2str(sfh, state, args[4]);

	TERMINATE_ERROR(sfh->executor, state, message, suffix);
}

SFH_DEF_ALL_EX(Report, "klee_report", true, false, false)
{
	// (file, line, message, suffix, tbale)
	SFH_CHK_ARGS(5, "klee_report");

	std::string	message = sfh->readStringAtAddress(state, args[2]);
	std::string	suffix = sfh->readStringAtAddress(state, args[3]);

	message += reporttab2str(sfh, state, args[4]);

	REPORT_ERROR(sfh->executor, state, message, suffix);
}


SFH_DEF_ALL(Merge, "klee_merge", false)
{ std::cerr << "[Merge] Merging disabled\n"; /* nop */ }

SFH_DEF_ALL(Malloc, "klee_malloc_fixed", true)
{
	SFH_CHK_ARGS(1, "malloc");

	uint64_t	sz;

	if (args[0]->getKind() != Expr::Constant) {
		TERMINATE_ERROR(sfh->executor,
			state,
			"symbolic malloc",
			"malloc.err");
		return;
	}

	sz = cast<ConstantExpr>(args[0])->getZExtValue();
	sfh->executor->executeAllocConst(
		state,
		sz,
		false,
		target,
		true /* zero memory */);
}

static ref<Expr> op_to_expr(
	int op,
	const ref<Expr>& e1, const ref<Expr>& e2, const ref<Expr>& e3)
{
	switch (op) {
	case KLEE_CMP_OP_EQ: return MK_EQ(e1, e2);
	case KLEE_CMP_OP_NE: return MK_NE(e1, e2);
	case KLEE_CMP_OP_UGT: return MK_UGT(e1, e2);
	case KLEE_CMP_OP_UGE: return MK_UGE(e1, e2);
	case KLEE_CMP_OP_ULT: return MK_ULT(e1, e2);
	case KLEE_CMP_OP_ULE: return MK_ULE(e1, e2);
	case KLEE_CMP_OP_SGT: return MK_SGT(e1, e2);
	case KLEE_CMP_OP_SGE: return MK_SGE(e1, e2);
	case KLEE_CMP_OP_SLT: return MK_SLT(e1, e2);
	case KLEE_CMP_OP_SLE: return MK_SLE(e1, e2);
	case KLEE_MK_OP_ITE:
		return MK_ITE(
			(e1->getWidth() == 1)
				? e1
				: MK_NE(e1, klee::MK_CONST(0, e1->getWidth())),
			e2, e3);
	case KLEE_MK_OP_AND: return MK_AND(e1, e2);
	case KLEE_MK_OP_OR: return MK_OR(e1, e2);
	case KLEE_MK_OP_XOR: return MK_XOR(e1, e2);
	case KLEE_MK_OP_NOT: return MK_NOT(e1);
	default: return NULL;
	}
}

static ref<Expr> cmpop_to_expr(
	int cmpop,
	const ref<Expr>& e1, const ref<Expr>& e2)
{ return op_to_expr(cmpop, e1, e2, ce_zero64); }


SFH_DEF_ALL(MkExpr, "__klee_mk_expr", true)
{
	ref<Expr>		e;
	const ConstantExpr	*ce;

	SFH_CHK_ARGS(4, "klee_mk_expr");

	ce = dyn_cast<ConstantExpr>(args[0]);
	if (ce == NULL) goto error;

	e = op_to_expr(ce->getZExtValue(), args[1], args[2], args[3]);
	if (e.isNull()) goto error;

	e = state.constraints.simplifyExpr(e);

	/* klee_mk_expr must always return 64-bit */
	if (e->getWidth() != 64) e = MK_ZEXT(e, 64);

	state.bindLocal(target, e);
	return;
error:
	TERMINATE_EARLY(sfh->executor, state, "__klee_mk_expr failed");
}

/* call prefer_op when there's a state which
 * should always be taken, regardless of branch prediction */
SFH_DEF_ALL(PreferOp, "klee_prefer_op", true)
{
	ref<Expr>		e;
	const ConstantExpr	*ce;
	Forks			*f;
	Executor::StatePair	sp;

	SFH_CHK_ARGS(3, "klee_prefer_op");

	ce = dyn_cast<ConstantExpr>(args[2]);
	if (ce == NULL) goto error;

	e = cmpop_to_expr(ce->getZExtValue(), args[0], args[1]);
	if (e.isNull()) goto error;

	/* huehuehuehue */
	f = sfh->executor->getForking();
	f->setPreferTrueState(true);
	sp = sfh->executor->fork(state, e, true);
	f->setPreferTrueState(false);

	/* XXX: something that needs to be done here is to force a
	 * real-time state. Probably should make a real-time scheduler
	 * built-in, since it's so important to fully execute error paths. */

	if (sp.first != NULL) sp.first->bindLocal(target, MK_CONST(1, 64));
	if (sp.second != NULL) sp.second->bindLocal(target, ce_zero64);

	return;
error:
	TERMINATE_EARLY(sfh->executor, state, "prefer-op failed");
}


SFH_DEF_ALL(Assume, "__klee_assume", false)
{
	ref<Expr>	e;
	bool		mustBeTrue, mayBeTrue, ok;

	SFH_CHK_ARGS(1, "klee_assume");

	e = args[0];

	if (e->getWidth() > 1) {
		/* if (e) => if ((e) != 0) */
		e = MK_NE(e, MK_CONST(0, e->getWidth()));
	}

	/* valid? */
	ok = sfh->executor->getSolver()->mustBeTrue(state, e, mustBeTrue);
	if (!ok) goto error;

	/* no need to add the assume constraint if implied by model */
	if (mustBeTrue) return;

	/* satisfiable? */
	ok = sfh->executor->getSolver()->mayBeTrue(state, e, mayBeTrue);
	if (!ok) goto error;

	if (!mayBeTrue) {
		TERMINATE_ERROR(sfh->executor,
			state,
			"invalid klee_assume_op call (provably unsat)",
			"user.err");
		return;
	}

	/* only add constraint if we know it's not already implied */
	sfh->executor->addConstrOrDie(state, e);
	return;

error:
	TERMINATE_EARLY(sfh->executor, state, "assume failed");
}

SFH_DEF_ALL(Feasible, "__klee_feasible", true)
{
	ref<Expr>		e;
	bool			mayBeTrue, ok;

	SFH_CHK_ARGS(1, "klee_feasible");

	/* mayBeTrue, etc expect boolean exprs, so
	 * convert to bool if necessary */
	e = args[0];
	if (e->getWidth() != 1)
		e = MK_NE(e, MK_CONST(0, e->getWidth()));

	ok = sfh->executor->getSolver()->mayBeTrue(state, e, mayBeTrue);
	if (!ok) goto error;

	state.bindLocal(target, MK_CONST(mayBeTrue, 64));
	return;

error:
	TERMINATE_EARLY(sfh->executor, state, "feasible-op failed");
}


SFH_DEF_ALL(IsSymbolic, "klee_is_symbolic", true)
{
	SFH_CHK_ARGS(1, "klee_is_symbolic");

	state.bindLocal(
		target,
		MK_CONST(!isa<ConstantExpr>(args[0]), Expr::Int32));
}

SFH_DEF_ALL(IsValidAddr, "klee_is_valid_addr", true)
{
	SFH_CHK_ARGS(1, "klee_is_valid_addr");
	ref<Expr>	addr(args[0]);
	ref<Expr>	ret;
	bool		ok;
	ObjectPair	op;

	if (addr->getKind() != Expr::Constant) {
		/* keyctl triggered this one in mremap */
		addr = sfh->executor->toConstant(state, addr, "is_valid_addr");
	}

	assert (addr->getKind() == Expr::Constant);
	ok = state.addressSpace.resolveOne(
		cast<ConstantExpr>(addr)->getZExtValue(), op);
	ret = ConstantExpr::create((ok) ? 1 : 0, 32);

	state.bindLocal(target, ret);
}

SFH_DEF_ALL(PreferCex, "klee_prefer_cex", false)
{
	Executor::ExactResolutionList rl;

	SFH_CHK_ARGS(2, "klee_prefex_cex");

	ref<Expr> cond = args[1];
	if (cond->getWidth() != Expr::Bool)
		cond = MK_NE(cond, MK_CONST(0, cond->getWidth()));

	sfh->executor->resolveExact(state, args[0], rl, "prefex_cex");

	assert(rl.size() == 1 &&
		"prefer_cex target must resolve to precisely one object");

	rl[0].first.first->cexPreferences.push_back(cond);
}

SFH_DEF_ALL(PrintExpr, "klee_print_expr", false)
{
	SFH_CHK_ARGS(2, "klee_print_expr");

	std::string msg_str = sfh->readStringAtAddress(state, args[0]);
	std::cerr << msg_str << ":" << args[1] << "\n";
}

SFH_DEF_ALL(SetForking, "klee_set_forking", false)
{
	SFH_CHK_ARGS(1, "klee_set_forking");
	ref<Expr> value = sfh->executor->toUnique(state, args[0]);

	if (ConstantExpr *CE = dyn_cast<ConstantExpr>(value)) {
		state.forkDisabled = CE->isZero();
		return;
	}

	TERMINATE_ERROR(sfh->executor,
		state,
		"klee_set_forking requires a constant arg",
		"user.err");
}

SFH_DEF_ALL(StackTrace, "klee_stack_trace", false)
{ sfh->executor->printStackTrace(state, std::cerr); }


SFH_DEF_ALL(StackDepth, "klee_stack_depth", true)
{ state.bindLocal(target, MK_CONST(state.stack.size(), 32)); }

SFH_DEF_ALL(Warning, "klee_warning", false)
{
	SFH_CHK_ARGS(1, "klee_warning");

	std::string msg_str = sfh->readStringAtAddress(state, args[0]);
	klee_warning("%s: %s", state.getCurrentKFunc()->function->getName().data(),
	       msg_str.c_str());
}

SFH_DEF_ALL(WarningOnce, "klee_warning_once", false)
{
  SFH_CHK_ARGS(1, "klee_warning_once");

  std::string msg_str = sfh->readStringAtAddress(state, args[0]);
  klee_warning_once(
  	state.pc,
  	"%s: %s",
	state.getCurrentKFunc()->function->getName().data(),
        msg_str.c_str());
}

SFH_DEF_ALL(PrintRange, "klee_print_range", false)
{
	ref<ConstantExpr>	value;
	bool			ok, res;
	std::string		msg_str;

	SFH_CHK_ARGS(2, "klee_print_range");

	msg_str = sfh->readStringAtAddress(state, args[0]);
	std::cerr << msg_str << ":" << args[1];
	if (!isa<ConstantExpr>(args[1])) {
		std::cerr << "\n";
		return;
	}

	// FIXME: Pull into a unique value method?
	ok = sfh->executor->getSolver()->getValue(state, args[1], value);
	assert(ok && "FIXME: Unhandled solver failure");
	ok= sfh->executor->getSolver()->mustBeTrue(
		state, MK_EQ(args[1], value), res);
	assert(ok && "FIXME: Unhandled solver failure");

	if (res) {
		std::cerr << " == " << value << '\n';
		return;
	}

	std::cerr << " ~= " << value;
	std::pair< ref<Expr>, ref<Expr> > p;
	ok = sfh->executor->getSolver()->getRange(state, args[1], p);
	if (!ok)
		std::cerr << " (in " << args[1]  << ")\n";
	else
		std::cerr	<< " (in ["
				<< p.first << ", " << p.second <<"])\n";
}

SFH_DEF_ALL(SymRangeBytes, "klee_sym_range_bytes", true)
{
	SFH_CHK_ARGS(2, "klee_sym_range_bytes");

	ConstantExpr	*ce_max_len, *ce_addr;
	unsigned	max_len_v, total;
	uint64_t	base_addr;

	EXPECT_CONST("klee_sym_range_bytes", ce_addr, 0);
	EXPECT_CONST("klee_sym_range_bytes", ce_max_len, 1);

	total = 0;
	base_addr = ce_addr->getZExtValue();
	do {
		const MemoryObject	*cur_mo;
		const ObjectState	*os;
		unsigned		off;

		cur_mo = state.addressSpace.resolveOneMO(base_addr);
		if (cur_mo == NULL)
			break;

		os = state.addressSpace.findObject(cur_mo);
		assert (os != NULL);

		max_len_v = ce_max_len->getZExtValue();

		assert (cur_mo->address <= base_addr);
		off = base_addr - cur_mo->address;
		for (unsigned i = off; i < cur_mo->size && total < max_len_v; i++) {
			if (os->isByteConcrete(i)) {
				max_len_v = 0;
				break;
			}
			total++;
		}

		base_addr += cur_mo->size;
	} while (total < max_len_v);

	state.bindLocal(target, MK_CONST(total, Expr::Int32));
}

typedef std::pair<std::string, unsigned> global_key_t;

SFH_DEF_ALL(GlobalInc, "klee_global_inc", true)
{
	static std::map<global_key_t, uint64_t> global_regs;

	/* klee_global_inc(const char* key, subkey, inc_amount)
	 *
	 * ex. klee_global_inc("instr_count", GET_PC(kmc_get_regs()))
	 * counts number of times hit instruction
	 */
	assert (0 == 1 && "STUB");

}


SFH_DEF_ALL(Free, "klee_free_fixed", false)
{
	SFH_CHK_ARGS(1, "free");

	if (args[0]->getKind() != Expr::Constant) {
		TERMINATE_ERROR(sfh->executor,
			state,
			"klee_free_fixed without constant ptr",
			"user.err");
		return;
	}

	sfh->executor->executeFree(state, args[0]);
}

SFH_DEF_ALL(CheckMemoryAccess, "klee_check_memory_access", false)
{
	SFH_CHK_ARGS(2, "klee_check_memory_access");

	ref<Expr> address = sfh->executor->toUnique(state, args[0]);
	ref<Expr> size = sfh->executor->toUnique(state, args[1]);
	if (!isa<ConstantExpr>(address) || !isa<ConstantExpr>(size)) {
		TERMINATE_ERROR(sfh->executor,
			state,
			"check_memory_access requires constant args",
			"user.err");
		return;
	}

	ObjectPair op;

	if (!state.addressSpace.resolveOne(
		cast<ConstantExpr>(address)->getZExtValue(), op)) {
		TERMINATE_ERROR_LONG(sfh->executor,
			state,
			"check_memory_access: memory error",
			"ptr.err",
			sfh->executor->getAddressInfo(state, address), false);
		return;
	}


	ref<Expr> chk = op.first->getBoundsCheckPointer(
		address,
		cast<ConstantExpr>(size)->getZExtValue());

	if (!chk->isTrue()) {
		TERMINATE_ERROR_LONG(sfh->executor,
			state,
			"check_memory_access: memory error",
			"ptr.err",
			sfh->executor->getAddressInfo(state, address), false);
	}
}

SFH_DEF_ALL(GetValue, "klee_get_value", true)
{
	SFH_CHK_ARGS(1, "klee_get_value");
	sfh->executor->executeGetValue(state, args[0], target);
}

SFH_DEF_ALL(GetValuePred, "klee_get_value_pred", true)
{
	SFH_CHK_ARGS(2, "klee_get_value_pred");
	ref<Expr>	p(args[1]);

	if (p->getWidth() > 1) p = MK_NE(p, MK_CONST(0,p->getWidth()));
	sfh->executor->executeGetValue(state, args[0], target, p);
}


SFH_DEF_ALL(DefineFixedObject, "klee_define_fixed_object", false)
{
	uint64_t		address, size;
	const ConstantExpr	*addr_ce, *size_ce;
	const ObjectState	*os;
	MemoryObject		*mo;

	SFH_CHK_ARGS(2, "klee_define_fixed_object");

	EXPECT_CONST("klee_define_fixed_object",addr_ce, 0);
	EXPECT_CONST("klee_define_fixed_object", size_ce, 1);

	address = addr_ce->getZExtValue();
	size = size_ce->getZExtValue();

	os = state.allocateFixed(address, size, state.prevPC->getInst());
	mo = const_cast<MemoryObject*>(state.addressSpace.resolveOneMO(address));
	assert (mo);

	mo->isUserSpecified = true; // XXX hack;
}

#define MAKESYM_ARGIDX_ADDR   0
#define MAKESYM_ARGIDX_LEN    1
#define MAKESYM_ARGIDX_NAME   2
/* XXX: handle with intrinsic library */
SFH_DEF_ALL(MakeSymbolic, "klee_make_symbolic", false)
{
  	Executor::ExactResolutionList	rl;
	std::string			name;
	ref<Expr>			addr;

	if (args.size() == 2) {
		name = "unnamed";
	} else {
		SFH_CHK_ARGS(3, "klee_make_symbolic");
		name = sfh->readStringAtAddress(
			state, args[MAKESYM_ARGIDX_NAME]);
	}

	addr = args[MAKESYM_ARGIDX_ADDR];
	if (addr->getKind() == Expr::Constant) {
		const ConstantExpr	*ce(cast<const ConstantExpr>(addr));
		const MemoryObject	*mo;

		mo = state.addressSpace.resolveOneMO(ce->getZExtValue());
		const_cast<MemoryObject*>(mo)->setName(name);
		sfh->executor->makeSymbolic(state, mo, name.c_str());
		return;
	}

	sfh->executor->resolveExact(state, addr, rl, "make_symbolic");

	foreach (it, rl.begin(), rl.end()) {
		MemoryObject *mo = const_cast<MemoryObject*>(it->first.first);
		const ObjectState *old = it->first.second;
		ExecutionState *s = it->second;
		ref<Expr>	cond;
		bool		res, ok;

		mo->setName(name);

		if (old->readOnly) {
			TERMINATE_ERROR(sfh->executor,
				*s,
				"cannot make readonly object symbolic",
				"user.err");
			return;
		}

		// FIXME: Type coercion should be done consistently somewhere.
		// UleExpr instead of EqExpr to make symbol partially symbolic.
		cond = MK_ULE(	MK_ZEXT(args[MAKESYM_ARGIDX_LEN],
					Context::get().getPointerWidth()),
				mo->getSizeExpr());

		ok = sfh->executor->getSolver()->mustBeTrue(*s, cond, res);
		assert(ok && "FIXME: Unhandled solver failure");

		if (res) {
			sfh->executor->makeSymbolic(*s, mo, name.c_str());
			continue;
		}

		TERMINATE_ERROR(sfh->executor,
			*s,
			"size for klee_make_symbolic[_name] too big",
			"user.err");
	}
}

SFH_DEF_ALL(MakeVSymbolic, "klee_make_vsym", false)
{
	std::string		name;
	ref<Expr>		addr;
	const ConstantExpr	*ce;
	const MemoryObject	*mo;
	ObjectState		*os;

	if (args.size() == 2) {
		name = "unnamed";
	} else {
		SFH_CHK_ARGS(3, "klee_make_symbolic");
		name = sfh->readStringAtAddress(
			state, args[MAKESYM_ARGIDX_NAME]);
	}

	addr = args[MAKESYM_ARGIDX_ADDR];
	if (addr->getKind() != Expr::Constant) {
		TERMINATE_ERROR(sfh->executor,
			state,
			"expected constant address for make_vsym",
			"user.err");
		return;
	}

	ce = cast<const ConstantExpr>(addr);
	mo = state.addressSpace.resolveOneMO(ce->getZExtValue());
	const_cast<MemoryObject*>(mo)->setName(name);

	os = sfh->executor->makeSymbolic(state, mo, name.c_str());
	state.markSymbolicVirtual(os->getArray());
}


SFH_DEF_ALL(MarkGlobal, "klee_mark_global", false)
{
	SFH_CHK_ARGS(1, "klee_mark_global");

	Executor::ExactResolutionList rl;
	sfh->executor->resolveExact(state, args[0], rl, "mark_global");

	foreach (it, rl.begin(), rl.end()) {
		MemoryObject *mo = const_cast<MemoryObject*>(it->first.first);
		assert(!mo->isLocal());
		mo->setGlobal(true);
	}
}


SFH_DEF_ALL(Yield, "klee_yield", false) { sfh->executor->yield(state); }

SFH_DEF_ALL(IsShadowed, "klee_is_shadowed", true)
{
	state.bindLocal(
		target,
		MK_CONST(args[0]->isShadowed(), Expr::Int32));
}

/* indirect call-- first parameter is function name */
SFH_DEF_ALL(Indirect0, "klee_indirect0", true)
{
	std::vector<ref<Expr> >	indir_args;
	std::string	fname(sfh->readStringAtAddress(state, args[0]));
	sfh->handleByName(state, fname, target, indir_args);
}

SFH_DEF_ALL(Indirect1, "klee_indirect1", true)
{
	std::vector<ref<Expr> >	indir_args;
	std::string	fname(sfh->readStringAtAddress(state, args[0]));
	indir_args.push_back(args[1]);

	sfh->handleByName(state, fname, target, indir_args);
}

SFH_DEF_ALL(Indirect2, "klee_indirect2", true)
{
	std::vector<ref<Expr> >	indir_args;
	std::string	fname(sfh->readStringAtAddress(state, args[0]));
	indir_args.push_back(args[1]);
	indir_args.push_back(args[2]);
	sfh->handleByName(state, fname, target, indir_args);
}

SFH_DEF_ALL(Indirect3, "klee_indirect3", true)
{
	std::vector<ref<Expr> >	indir_args;
	std::string	fname(sfh->readStringAtAddress(state, args[0]));
	indir_args.push_back(args[1]);
	indir_args.push_back(args[2]);
	indir_args.push_back(args[3]);
	sfh->handleByName(state, fname, target, indir_args);
}

SFH_DEF_ALL(Indirect4, "klee_indirect4", true)
{
	std::vector<ref<Expr> >	indir_args;
	std::string	fname(sfh->readStringAtAddress(state, args[0]));
	indir_args.push_back(args[1]);
	indir_args.push_back(args[2]);
	indir_args.push_back(args[3]);
	indir_args.push_back(args[4]);
	sfh->handleByName(state, fname, target, indir_args);
}


readreg_map_ty HandlerReadReg::vars;
const SpecialFunctionHandler::HandlerInfo HandlerReadReg::hinfo =
{ "klee_read_reg", &HandlerReadReg::create, false, true, false};
SFH_DEF_HANDLER(ReadReg)
{
	std::map<std::string, uint64_t>::iterator	it;

	it = vars.find(sfh->readStringAtAddress(state, args[0]));
	if (it == vars.end()) return;

	state.bindLocal(target, MK_CONST(it->second, 64));
}

#if 0
SFH_DEF_HANDLER(GetObjSize)
{
	// XXX should type check args
	SFH_CHK_ARGS(1, "klee_get_obj_size");
	Executor::ExactResolutionList rl;
	sfh->executor->resolveExact(
		state, args[0], rl, "klee_get_obj_size");
	foreach (it, rl.begin(), rl.end()) {
		it->second->bindLocal(
			target,
			MK_CONST(it->first.first->size, Expr::Int32));
	}
}
#endif
SFH_DEF_ALL(GetObjSize, "klee_get_obj_size", true)
{
	const MemoryObject	*mo;
	const ConstantExpr	*ce;

	SFH_CHK_ARGS(1, "klee_get_obj_size");

	EXPECT_CONST("klee_get_obj_size", ce, 0);

	mo = state.addressSpace.resolveOneMO(ce->getZExtValue());
	state.bindLocal(target, MK_CONST(mo->size, Expr::Int32));
}

SFH_DEF_ALL(GetObjNext, "klee_get_obj_next", true)
{
	const ConstantExpr	*ce;
	uint64_t		req;
	uint64_t		obj_addr;

	SFH_CHK_ARGS(1, "klee_get_obj_next");

	EXPECT_CONST("klee_get_obj_next", ce, 0);

	req = ce->getZExtValue();
	if ((req + 1) < req) {
		state.bindLocal(target, ce_zero64);
		return;
	}
	req++;

	MMIter	mm_it(state.addressSpace.lower_bound(req));
	if (mm_it == state.addressSpace.end()) {
		obj_addr = 0;
	} else
		obj_addr = (*mm_it).first->address;

	state.bindLocal(target, MK_CONST(obj_addr, Expr::Int64));
}

SFH_DEF_ALL(HookReturn, "klee_hook_return", false)
{
	Function		*f;
	KFunction		*kf;
	const ConstantExpr	*ce_addr, *ce_idx;
	uint64_t		addr, idx;

	/* (backtrack idx, function_addr, aux_expr) */
	SFH_CHK_ARGS(3, "klee_hook_return");

	EXPECT_CONST("klee_hook_return", ce_idx, 0);
	EXPECT_CONST("klee_hook_return", ce_addr, 1);

	addr = ce_addr->getZExtValue();
	f = (Function*)((void*)addr);
	kf = sfh->executor->getKModule()->getKFunction(f);
	if (kf == NULL) {
		f = sfh->executor->getFuncByAddr(addr);
		kf = sfh->executor->getKModule()->getKFunction(f);
	}

	if (kf == NULL) {
		TERMINATE_ERROR(sfh->executor,
			state,
			"klee_hook_return: given bad function",
			"user.err");
		return;
	}


	idx = ce_idx->getZExtValue();
	if (idx >= state.stack.size()) {
		TERMINATE_ERROR(sfh->executor,
			state,
			"klee_hook_return: hook index out of bounds",
			"user.err");
		return;
	}

	StackFrame	&sf(state.stack[(state.stack.size()-1) - idx]);

	sf.onRet = kf;
	sf.onRet_expr = args[2];
}

SFH_DEF_ALL(GetObjPrev, "klee_get_obj_prev", true)
{
	const ConstantExpr	*ce;
	uint64_t		req;
	uint64_t		obj_addr;

	SFH_CHK_ARGS(1, "klee_get_obj_prev");

	EXPECT_CONST("klee_get_obj_prev", ce, 0);

	req = ce->getZExtValue();
	if (req == 0) {
		state.bindLocal(target, ce_zero64);
		return;
	}

	MMIter	mm_it(state.addressSpace.upper_bound(req));
	if (mm_it == state.addressSpace.end())
		--mm_it;

	do {
		obj_addr = (*mm_it).first->address;
		--mm_it;
	} while (obj_addr > req && mm_it != state.addressSpace.end());

	state.bindLocal(target, MK_CONST(obj_addr, Expr::Int64));
}

/* llvm has a habit of optimizing branches into selects; sometimes this is
 * undesirable */
SFH_DEF_ALL(ForkEq, "__klee_fork_eq", true)
{
	SFH_CHK_ARGS(2, "klee_fork_eq");

	ref<Expr>		cond(MK_EQ(args[0], args[1]));
	Executor::StatePair	sp(sfh->executor->fork(state, cond, true));

	if (sp.first != NULL) sp.first->bindLocal(target, MK_CONST(1, 32));
	if (sp.second != NULL) sp.second->bindLocal(target, MK_CONST(0, 32));
}

SFH_DEF_ALL(IsReadOnly, "klee_is_readonly", true)
{
	const MemoryObject	*mo;
	const ObjectState	*os;
	const ConstantExpr	*ce;

	SFH_CHK_ARGS(1, "klee_is_readonly");
	EXPECT_CONST("klee_is_readonly", ce, 0);

	mo = state.addressSpace.resolveOneMO(ce->getZExtValue());
	if (mo == NULL) {
		state.bindLocal(
			target, MK_CONST(((uint32_t)~0), Expr::Int32));
		return;
	}

	os = state.addressSpace.findObject(mo);
	if (os->isReadOnly()) {
		state.bindLocal(target, MK_CONST(1, Expr::Int32));
		return;
	}

	state.bindLocal(target, MK_CONST(0, Expr::Int32));
}


SFH_DEF_ALL(Watch, "klee_watch", false)
{
	SFH_CHK_ARGS(1, "klee_watch");

	const ConstantExpr	*ce = dyn_cast<ConstantExpr>(args[0]);
	assert (ce != NULL);

	if (ce->getZExtValue())
		DebugPrintInstructions = true;
	else
		DebugPrintInstructions = false;
}



SFH_DEF_ALL(ConcretizeState, "klee_concretize_state", false)
{
	SFH_CHK_ARGS(1, "klee_concretize_state");

	ref<Expr>		e(args[0]);
	const ConstantExpr	*ce;
	ExecutionState		*es;

	ce = dyn_cast<ConstantExpr>(e);
	if (ce && ce->getZExtValue() != 0) {
		/* bad input; silent nop */
		return;
	}

	es = sfh->executor->concretizeState(state, (ce == NULL) ? e : NULL);
	if (es != NULL) sfh->executor->terminate(*es);
}

SFH_DEF_ALL(ConstrCount, "klee_constr_count", true)
{
	state.bindLocal(
		target,
		MK_CONST(state.constraints.size(), Expr::Int32));
}


SFH_DEF_ALL(ExprHash, "__klee_expr_hash", true)
{
	SFH_CHK_ARGS(1, "klee_expr_hash");
	state.bindLocal(target, MK_CONST(args[0]->hash(), 64));
}

SFH_DEF_ALL(SymCoreHash, "klee_sym_corehash", true)
{
	std::vector< ref<ReadExpr> >	reads;
	std::set<const Array*>		arrays;
	const Array*			arr;
	Expr::Hash			min_hash;
	ref<ReadExpr>			min_read;

	if (args[0]->getKind() == Expr::Constant) {
		TERMINATE_ERROR(sfh->executor,
			state,
			"sym core hash argument is constant",
			"symhash.err");
		return;
	}

	ExprUtil::findReads(args[0], false, reads);

	if (reads.empty()) {
		TERMINATE_ERROR(sfh->executor,
			state,
			"no reads found in symbolic",
			"symhash.err");
		return;
	}

	min_hash = reads[0]->hash();
	min_read = reads[0];
	foreach (it, reads.begin(), reads.end()) {
		const ref<klee::Array>	cur_arr((*it)->getArray());
		Expr::Hash	cur_hash;

		arrays.insert(cur_arr.get());
		cur_hash = (*it)->hash();
		if (min_hash > cur_hash) {
			min_hash = cur_hash;
			min_read = *it;
		}
	}

#if 0
	if (arrays.size() != 1) {
#endif
#if 1
	if (arrays.empty()) {
#endif
#if 0
		std::stringstream	ss;

		ss << "multiple arrays in pointer: expr=\n";
		ss << args[0] << '\n';
		sfh->executor->terminateOnError(
			state,
			ss.str().c_str(),
			"symhash.err");
		std::cerr << "Multiple arrays matched on expr=\n";
		std::cerr << args[0] << '\n';
#endif
		state.bindLocal(target, ce_zero64);
		return;
	}
	arr = *(arrays.begin());
	state.bindLocal(target, MK_CONST(min_hash ^ arr->hash(), 64));
}

static bool getObjectFromBase(ExecutionState& state, ref<Expr>& e, ObjectPair& op)
{
	uint64_t	obj_base;

	obj_base = dyn_cast<klee::ConstantExpr>(e)->getZExtValue();
	MMIter	it(state.addressSpace.lower_bound(obj_base));

	op = *it;
	if (op.first->isInBounds(obj_base, 1))
		return true;

	--it;
	if (it == state.addressSpace.begin())
		return false;

	op = *it;
	if (op.first->isInBounds(obj_base, 1))
		return true;

	return false;
}

#define wide_load_def(x)	\
SFH_DEF_ALL(WideLoad##x, "klee_wide_load_" #x, true) 	\
{	\
	ObjectPair		op;	\
	ref<Expr>		user_addr, val;	\
\
	if (getObjectFromBase(state, args[0], op) == false) {	\
		TERMINATE_ERROR(sfh->executor,		\
			state,				\
			"Could not resolve addr for wide load",	\
			"mmu.err");	\
		return; \
	} \
\
	user_addr = args[1];	\
	val = state.read(		\
		op.second,	\
		MK_SUB(user_addr, MK_CONST(op.first->address, 64)), x);	\
	state.bindLocal(target, val);	\
}

#define wide_store_def(x)	\
SFH_DEF_ALL(WideStore##x, "klee_wide_store_" #x, false)	\
{	\
	ObjectState		*os;	\
	ObjectPair		op;	\
	ref<Expr>		val, user_addr;	\
\
	if (getObjectFromBase(state, args[0], op) == false) {	\
		TERMINATE_ERROR(sfh->executor,\
			state,	\
			"Could not resolve addr for wide store",	\
			"mmu.err");	\
		return;	\
	}	\
	user_addr = args[1];	\
	val = args[2];		\
	if (args.size() == 4) 	\
		val = MK_CONCAT(args[3], val);	\
\
	os = state.addressSpace.getWriteable(op);		\
	state.write(	\
		os,	\
		MK_SUB(user_addr, MK_CONST(op.first->address, 64)), val); \
}

#define wide_op_def(x)	\
	wide_load_def(x) \
	wide_store_def(x)

wide_op_def(8)
wide_op_def(16)
wide_op_def(32)
wide_op_def(64)
wide_op_def(128)

static SpecialFunctionHandler::HandlerInfo hi_exit =
{ "_exit", &HandlerExit::create, true, false, false };

static const SpecialFunctionHandler::HandlerInfo *handlerInfo[] =
{
#define add(h) &Handler##h::hinfo
&hi_exit,
add(Exit),
add(SilentExit),
add(ReportError),
add(Report),
add(ResumeExit),
  /* ALL EXPCET CONSTANT ARGUMENTS */
add(GetObjNext),
add(GetObjSize),
add(GetObjPrev),
add(Free),
add(Malloc),
add(Assume),
add(Feasible),
add(PreferOp),
add(ForkEq),
add(CheckMemoryAccess),
add(GetValue),
add(GetValuePred),
add(DefineFixedObject),
add(IsSymbolic),
add(IsValidAddr),
add(MakeSymbolic),
add(MarkGlobal),
add(Merge),
add(PreferCex),
add(PrintExpr),
add(PrintRange),
add(SetForking),
add(StackTrace),
add(Watch),
add(Warning),
add(WarningOnce),
add(GetPruneID),
add(Prune),
add(StackDepth),
add(ReadReg),
add(HookReturn),
add(Indirect0),
add(Indirect1),
add(Indirect2),
add(Indirect3),
add(Indirect4),
add(SymCoreHash),
add(ExprHash),
add(IsShadowed),
add(Yield),
add(SymRangeBytes),
add(MkExpr),
add(GlobalInc),
add(ConstrCount),
add(IsReadOnly),
add(ConcretizeState),
add(MakeVSymbolic),
#define DEF_WIDE(x)	\
add(WideLoad##x),	\
add(WideStore##x)
  DEF_WIDE(8),
  DEF_WIDE(16),
  DEF_WIDE(32),
  DEF_WIDE(64),
  DEF_WIDE(128),
#undef DEF_WIDE
#undef add
NULL
};

void SpecialFunctionHandler::prepare() { prepare(handlerInfo); }
void SpecialFunctionHandler::bind(void) { bind(handlerInfo); }
