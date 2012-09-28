//===-- SpecialFunctionHandler.cpp ----------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#include <llvm/Module.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/LLVMContext.h>
#include "Memory.h"
#include "SpecialFunctionHandler.h"
#include "StateSolver.h"

#include "klee/Common.h"
#include "klee/ExecutionState.h"

#include "klee/Internal/Module/KModule.h"
#include "static/Sugar.h"

#include "Executor.h"

#include <errno.h>

using namespace llvm;
using namespace klee;

namespace klee
{
SFH_HANDLER(Assume)
SFH_HANDLER(AssumeEq)
SFH_HANDLER(CheckMemoryAccess)
SFH_HANDLER(DefineFixedObject)
SFH_HANDLER(Exit)
SFH_HANDLER(ForceNE)
SFH_HANDLER(Free)
SFH_HANDLER(GetPruneID)
SFH_HANDLER(Prune)
SFH_HANDLER(GetErrno)
SFH_HANDLER(GetObjSize)
SFH_HANDLER(GetObjNext)
SFH_HANDLER(GetObjPrev)
SFH_HANDLER(GetValue)
SFH_HANDLER(IsSymbolic)
SFH_HANDLER(IsValidAddr)
SFH_HANDLER(MakeSymbolic)
SFH_HANDLER(Malloc)
SFH_HANDLER(MarkGlobal)
SFH_HANDLER(Merge)
SFH_HANDLER(PreferCex)
SFH_HANDLER(PrintExpr)
SFH_HANDLER(PrintRange)
SFH_HANDLER(Range)
SFH_HANDLER(ReportError)
SFH_HANDLER(SetForking)
SFH_HANDLER(SilentExit)
SFH_HANDLER(StackTrace)
SFH_HANDLER(SymRangeBytes)
SFH_HANDLER(Warning)
SFH_HANDLER(WarningOnce)
SFH_HANDLER(Yield)
SFH_HANDLER(IsShadowed)
SFH_HANDLER(Indirect0)
SFH_HANDLER(Indirect1)
SFH_HANDLER(Indirect2)
SFH_HANDLER(ForkEq)
SFH_HANDLER(StackDepth)
#define DEF_SFH_MMU(x)			\
	SFH_HANDLER(WideStore##x)	\
	SFH_HANDLER(WideLoad##x)
DEF_SFH_MMU(8)
DEF_SFH_MMU(16)
DEF_SFH_MMU(32)
DEF_SFH_MMU(64)
DEF_SFH_MMU(128)
#undef DEF_SFH_MMU
}

static const SpecialFunctionHandler::HandlerInfo handlerInfo[] =
{
#define add(name, h, ret) {	\
	name, 			\
	&Handler##h::create,	\
	false, ret, false }
#define addDNR(name, h) {	\
	name, 			\
	&Handler##h::create,	\
	true, false, false }
  addDNR("_exit", Exit),
  { "exit", &HandlerExit::create, true, false, true },
  addDNR("klee_silent_exit", SilentExit),
  addDNR("klee_report_error", ReportError),

  /* ALL EXPCET CONSTANT ARGUMENTS */
  add("klee_get_obj_next", GetObjNext, true),
  add("klee_get_obj_size", GetObjSize, true),
  add("klee_get_obj_prev", GetObjPrev, true),

  add("free", Free, false),
  add("klee_assume", Assume, false),
  add("klee_assume_eq", AssumeEq, false),
  add("__klee_fork_eq", ForkEq, true),
  add("klee_check_memory_access", CheckMemoryAccess, false),
  add("klee_force_ne", ForceNE, false),
  add("klee_get_value", GetValue, true),
  add("klee_define_fixed_object", DefineFixedObject, false),
  add("klee_get_errno", GetErrno, true),
  add("klee_is_symbolic", IsSymbolic, true),
  add("klee_is_valid_addr", IsValidAddr, true),
  add("klee_make_symbolic", MakeSymbolic, false),
  add("klee_mark_global", MarkGlobal, false),
  add("klee_merge", Merge, false),
  add("klee_prefer_cex", PreferCex, false),
  add("klee_print_expr", PrintExpr, false),
  add("klee_print_range", PrintRange, false),
  add("klee_set_forking", SetForking, false),
  add("klee_stack_trace", StackTrace, false),
  add("klee_warning", Warning, false),
  add("klee_warning_once", WarningOnce, false),
  add("klee_get_prune_id", GetPruneID, true),
  add("klee_prune", Prune, false),
  add("malloc", Malloc, true),
  add("klee_stack_depth", StackDepth, true),
  add("klee_indirect0", Indirect0, true),
  add("klee_indirect1", Indirect1, true),
  add("klee_indirect2", Indirect2, true),
  add("klee_is_shadowed", IsShadowed, true),

#define DEF_WIDE(x)	\
	add("klee_wide_load_" #x, WideLoad##x, true),	\
	add("klee_wide_store_" #x, WideStore##x, false)
  DEF_WIDE(8),
  DEF_WIDE(16),
  DEF_WIDE(32),
  DEF_WIDE(64),
  DEF_WIDE(128),
#undef DEF_WIDE

  add("klee_yield", Yield, false),
  add("klee_sym_range_bytes", SymRangeBytes, true)
#undef addDNR
#undef add
};

SpecialFunctionHandler::SpecialFunctionHandler(Executor* _executor)
: executor(_executor) {}

void SpecialFunctionHandler::prepare()
{
	unsigned N = sizeof(handlerInfo)/sizeof(handlerInfo[0]);
	prepare((HandlerInfo*)(&handlerInfo), N);
}

void SpecialFunctionHandler::bind(void)
{
	unsigned N = sizeof(handlerInfo)/sizeof(handlerInfo[0]);
	bind((HandlerInfo*)&handlerInfo, N);
}

void SpecialFunctionHandler::prepare(HandlerInfo* hinfo, unsigned int N)
{
	for (unsigned i=0; i<N; ++i) {
		HandlerInfo &hi = hinfo[i];
		Function *f;

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
{
	for (unsigned i=0; i<N; ++i)
		addHandler(hinfo[i]);
}

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
		executor->terminateOnExecError(state, "missing indirect call");
		return;
	}

	handle(state, f, target, args, true);
}

bool SpecialFunctionHandler::handle(
	ExecutionState &state,
	Function *f,
	KInstruction *target,
	std::vector< ref<Expr> > &arguments,
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
		executor->terminateOnExecError(
			state,
			"expected return value from void special function");
		return true;
	}

	h->handle(state, target, arguments);

	if (missing_ret_val && insert_ret_vals) {
		state.bindLocal(target, MK_CONST(0, 64));
	}

	return true;
}

unsigned char* SpecialFunctionHandler::readBytesAtAddressNoBound(
	ExecutionState &state,
	ref<Expr> addressExpr,
	unsigned int& len,
	int terminator)
{
	return readBytesAtAddress(state, addressExpr, ~0, len, terminator);
}

unsigned char* SpecialFunctionHandler::readBytesAtAddress(
	ExecutionState &state,
	ref<Expr> addressExpr,
	unsigned int maxlen,
	unsigned int& len,
	int terminator)
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
	if (!state.addressSpace.resolveOne(address, op)) {
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
		if ((int)buf[i-offset] == terminator) {
			buf[i-offset] = '\0';
			break;
		}
		len++;
	}

	return buf;
}


// reads a concrete string from memory
std::string SpecialFunctionHandler::readStringAtAddress(
	ExecutionState &state, ref<Expr> addressExpr)
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

SFH_DEF_HANDLER(Exit)
{
	SFH_CHK_ARGS(1, "exit");
	sfh->executor->terminateOnExit(state);
}

SFH_DEF_HANDLER(SilentExit)
{
	assert(arguments.size()==1 && "invalid number of arguments to exit");
	sfh->executor->terminate(state);
}

cl::opt<bool> EnablePruning("enable-pruning", cl::init(true));

static std::map<int, int> pruneMap;

SFH_DEF_HANDLER(GetPruneID)
{
  if (!EnablePruning) {
    state.bindLocal(target, MK_CONST(0, Expr::Int32));
    return;
  }

  static unsigned globalPruneID = 1;
  SFH_CHK_ARGS(1, "klee_get_prune_id");
  int count = cast<ConstantExpr>(arguments[0])->getZExtValue();

  state.bindLocal(target, ConstantExpr::create(globalPruneID, Expr::Int32));
  pruneMap[globalPruneID] = count;

  globalPruneID++;
}

SFH_DEF_HANDLER(Prune)
{
  if (!EnablePruning)
    return;

  assert((arguments.size()==1 || arguments.size()==2) &&
	 "Usage: klee_prune(pruneID [,uniqueID])\n");
  int prune_id = cast<ConstantExpr>(arguments[0])->getZExtValue();

  assert(pruneMap.find(prune_id) != pruneMap.end() &&
	 "Invalid prune ID passed to klee_prune()");

  if (pruneMap[prune_id] == 0)
    sfh->executor->terminate(state);
  else
    pruneMap[prune_id]--;
}

SFH_DEF_HANDLER(ReportError)
{
	SFH_CHK_ARGS(4, "klee_report_error");

	// arg[0] = file
	// arg[1] = line
	// arg[2] = message
	// arg[3] = suffix
	std::string	message = sfh->readStringAtAddress(state, arguments[2]);
	std::string	suffix = sfh->readStringAtAddress(state, arguments[3]);

	sfh->executor->terminateOnError(state, message, suffix.c_str(), "");
}

SFH_DEF_HANDLER(Merge) { std::cerr << "[Merge] Merging disabled\n"; /* nop */ }

SFH_DEF_HANDLER(Malloc)
{
	// XXX should type check args
	SFH_CHK_ARGS(1, "malloc");
	sfh->executor->executeAlloc(state, arguments[0], false, target);
}

SFH_DEF_HANDLER(Assume)
{
	ref<Expr>	e;
	bool		mustBeFalse, ok;

	SFH_CHK_ARGS(1, "klee_assume");

	e = arguments[0];
	if (e->getWidth() != Expr::Bool)
		e = MK_NE(e, MK_CONST(0, e->getWidth()));

	ok = sfh->executor->getSolver()->mustBeFalse(state, e, mustBeFalse);
	if (!ok) {
		sfh->executor->terminateEarly(
			state, "assume query failed");
		return;
	}

	if (!mustBeFalse) {
		sfh->executor->addConstrOrDie(state, e);
		return;
	}

	sfh->executor->terminateOnError(
		state,
		"invalid klee_assume call (provably false)",
		"user.err");
}

SFH_DEF_HANDLER(AssumeEq)
{
	ref<Expr>	e;
	bool		mustBeTrue, mayBeTrue, ok;

	SFH_CHK_ARGS(2, "klee_assume_eq");

	e = EqExpr::create(arguments[0], arguments[1]);

	/* valid? */
	ok = sfh->executor->getSolver()->mustBeTrue(state, e, mustBeTrue);
	if (!ok) goto error;

	/* nothing to do here? */
	if (mustBeTrue) return;

	/* satisfiable? */
	ok = sfh->executor->getSolver()->mayBeTrue(state, e, mayBeTrue);
	if (!ok) goto error;

	if (!mayBeTrue) {
		sfh->executor->terminateOnError(
			state,
			"invalid klee_assume_eq call (provably false)",
			"user.err");
		return;
	}

	/* only add constraint if we know it's not already implied */
	sfh->executor->addConstrOrDie(state, e);
	return;

error:
	sfh->executor->terminateEarly(state, "assumeeq failed");
}

SFH_DEF_HANDLER(IsSymbolic)
{
	SFH_CHK_ARGS(1, "klee_is_symbolic");

	state.bindLocal(
		target,
		MK_CONST(!isa<ConstantExpr>(arguments[0]), Expr::Int32));
}

SFH_DEF_HANDLER(IsValidAddr)
{
	SFH_CHK_ARGS(1, "klee_is_valid_addr");
	ref<Expr>	addr(arguments[0]);
	ref<Expr>	ret;
	bool		ok;
	ObjectPair	op;

	if (addr->getKind() != Expr::Constant) {
		/* keyctl triggered this one in mremap */
		addr = sfh->executor->toConstant(state, addr, "is_valid_addr");
	}

	assert (addr->getKind() == Expr::Constant);
	ok = state.addressSpace.resolveOne(cast<ConstantExpr>(addr), op);
	ret = ConstantExpr::create((ok) ? 1 : 0, 32);

	state.bindLocal(target, ret);
}

SFH_DEF_HANDLER(PreferCex)
{
	Executor::ExactResolutionList rl;

	SFH_CHK_ARGS(2, "klee_prefex_cex");

	ref<Expr> cond = arguments[1];
	if (cond->getWidth() != Expr::Bool)
		cond = MK_NE(cond, MK_CONST(0, cond->getWidth()));

	sfh->executor->resolveExact(state, arguments[0], rl, "prefex_cex");

	assert(rl.size() == 1 &&
		"prefer_cex target must resolve to precisely one object");

	rl[0].first.first->cexPreferences.push_back(cond);
}

SFH_DEF_HANDLER(PrintExpr)
{
	SFH_CHK_ARGS(2, "klee_print_expr");

	std::string msg_str = sfh->readStringAtAddress(state, arguments[0]);
	std::cerr << msg_str << ":" << arguments[1] << "\n";
}

SFH_DEF_HANDLER(SetForking)
{
	SFH_CHK_ARGS(1, "klee_set_forking");
	ref<Expr> value = sfh->executor->toUnique(state, arguments[0]);

	if (ConstantExpr *CE = dyn_cast<ConstantExpr>(value)) {
		state.forkDisabled = CE->isZero();
		return;
	}

	sfh->executor->terminateOnError(
		state,
		"klee_set_forking requires a constant arg",
		"user.err");
}

SFH_DEF_HANDLER(StackTrace) { state.dumpStack(std::cout); }

SFH_DEF_HANDLER(StackDepth)
{ state.bindLocal(target, MK_CONST(state.stack.size(), 32)); }

SFH_DEF_HANDLER(Warning)
{
	SFH_CHK_ARGS(1, "klee_warning");

	std::string msg_str = sfh->readStringAtAddress(state, arguments[0]);
	klee_warning("%s: %s", state.getCurrentKFunc()->function->getName().data(),
	       msg_str.c_str());
}

SFH_DEF_HANDLER(WarningOnce)
{
  SFH_CHK_ARGS(1, "klee_warning_once");

  std::string msg_str = sfh->readStringAtAddress(state, arguments[0]);
  klee_warning_once(0, "%s: %s", state.getCurrentKFunc()->function->getName().data(),
                    msg_str.c_str());
}

SFH_DEF_HANDLER(PrintRange)
{
	ref<ConstantExpr>	value;
	bool			ok, res;
	std::string		msg_str;

	SFH_CHK_ARGS(2, "klee_print_range");

	msg_str = sfh->readStringAtAddress(state, arguments[0]);
	std::cerr << msg_str << ":" << arguments[1];
	if (!isa<ConstantExpr>(arguments[1])) {
		std::cerr << "\n";
		return;
	}

	// FIXME: Pull into a unique value method?
	ok = sfh->executor->getSolver()->getValue(state, arguments[1], value);
	assert(ok && "FIXME: Unhandled solver failure");
	ok= sfh->executor->getSolver()->mustBeTrue(
		state, MK_EQ(arguments[1], value), res);
	assert(ok && "FIXME: Unhandled solver failure");

	if (res) {
		std::cerr << " == " << value << '\n';
		return;
	}

	std::cerr << " ~= " << value;
	std::pair< ref<Expr>, ref<Expr> > p;
	ok = sfh->executor->getSolver()->getRange(state, arguments[1], p);
	if (!ok)
		std::cerr << " (in " << arguments[1]  << ")\n";
	else
		std::cerr	<< " (in ["
				<< p.first << ", " << p.second <<"])\n";
}

SFH_DEF_HANDLER(SymRangeBytes)
{
	SFH_CHK_ARGS(2, "klee_sym_range_bytes");

	ref<Expr>	addr(arguments[0]);
	ref<Expr>	max_len(arguments[1]);
	ConstantExpr	*ce_max_len, *ce_addr;
	unsigned	max_len_v, total;
	uint64_t	base_addr;

	ce_addr = dyn_cast<ConstantExpr>(addr);
	ce_max_len = dyn_cast<ConstantExpr>(max_len);

	if (ce_addr == NULL || ce_max_len == NULL) {
		sfh->executor->terminateOnError(
			state,
			"klee_sym_range_bytes expects constant addr and max_len",
			"user.err");
		return;
	}

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
		for (unsigned i = off; i < os->size && total < max_len_v; i++) {
			if (os->isByteConcrete(i)) {
				max_len_v = 0;
				break;
			}
			total++;
		}

		base_addr += os->size;
	} while (total < max_len_v);

	state.bindLocal(target, ConstantExpr::create(total, Expr::Int32));
}

SFH_DEF_HANDLER(GetErrno)
{
	// XXX should type check args
	SFH_CHK_ARGS(0, "klee_get_errno");
	state.bindLocal(target, ConstantExpr::create(errno, Expr::Int32));
}

SFH_DEF_HANDLER(Free)
{
	// XXX should type check args
	SFH_CHK_ARGS(1, "free");
	sfh->executor->executeFree(state, arguments[0]);
}

SFH_DEF_HANDLER(CheckMemoryAccess)
{
	SFH_CHK_ARGS(2, "klee_check_memory_access");

	ref<Expr> address = sfh->executor->toUnique(state, arguments[0]);
	ref<Expr> size = sfh->executor->toUnique(state, arguments[1]);
	if (!isa<ConstantExpr>(address) || !isa<ConstantExpr>(size)) {
		sfh->executor->terminateOnError(
			state,
			"check_memory_access requires constant args",
			"user.err");
		return;
	}

	ObjectPair op;

	if (!state.addressSpace.resolveOne(cast<ConstantExpr>(address), op)) {
		sfh->executor->terminateOnError(
			state,
			"check_memory_access: memory error",
			"ptr.err",
			sfh->executor->getAddressInfo(state, address));
		return;
	}


	ref<Expr> chk = op.first->getBoundsCheckPointer(
		address,
		cast<ConstantExpr>(size)->getZExtValue());

	if (!chk->isTrue()) {
		sfh->executor->terminateOnError(
			state,
			"check_memory_access: memory error",
			"ptr.err",
			sfh->executor->getAddressInfo(state, address));
	}
}

SFH_DEF_HANDLER(GetValue)
{
	SFH_CHK_ARGS(1, "klee_get_value");
	sfh->executor->executeGetValue(state, arguments[0], target);
}

SFH_DEF_HANDLER(DefineFixedObject)
{
	uint64_t		address, size;
	const ObjectState	*os;
	MemoryObject		*mo;

	SFH_CHK_ARGS(2, "klee_define_fixed_object");
	assert(isa<ConstantExpr>(arguments[0]) &&
	 "expect constant address argument to klee_define_fixed_object");
	assert(isa<ConstantExpr>(arguments[1]) &&
	 "expect constant size argument to klee_define_fixed_object");

	address = cast<ConstantExpr>(arguments[0])->getZExtValue();
	size = cast<ConstantExpr>(arguments[1])->getZExtValue();

	os = state.allocateFixed(address, size, state.prevPC->getInst());
	mo = const_cast<MemoryObject*>(state.addressSpace.resolveOneMO(address));
	assert (mo);

	mo->isUserSpecified = true; // XXX hack;
}

#define MAKESYM_ARGIDX_ADDR   0
#define MAKESYM_ARGIDX_LEN    1
#define MAKESYM_ARGIDX_NAME   2
/* XXX: handle with intrinsic library */
SFH_DEF_HANDLER(MakeSymbolic)
{
  	Executor::ExactResolutionList	rl;
	std::string			name;
	ref<Expr>			addr;

	if (arguments.size() == 2) {
		name = "unnamed";
	} else {
		SFH_CHK_ARGS(3, "klee_make_symbolic");
		name = sfh->readStringAtAddress(
			state, arguments[MAKESYM_ARGIDX_NAME]);
	}

	addr = arguments[MAKESYM_ARGIDX_ADDR];
	if (addr->getKind() == Expr::Constant) {
		const ConstantExpr	*ce(cast<const ConstantExpr>(addr));
		const MemoryObject	*mo;

		mo = state.addressSpace.resolveOneMO(ce->getZExtValue());
		const_cast<MemoryObject*>(mo)->setName(name);
		sfh->executor->makeSymbolic(state, mo);
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
			sfh->executor->terminateOnError(
				*s,
				"cannot make readonly object symbolic",
				"user.err");
			return;
		}

		// FIXME: Type coercion should be done consistently somewhere.
		// UleExpr instead of EqExpr to make symbol partially symbolic.
		cond = MK_ULE(	MK_ZEXT(arguments[MAKESYM_ARGIDX_LEN],
					Context::get().getPointerWidth()),
				mo->getSizeExpr());

		ok = sfh->executor->getSolver()->mustBeTrue(*s, cond, res);
		assert(ok && "FIXME: Unhandled solver failure");

		if (res) {
			sfh->executor->makeSymbolic(*s, mo);
			continue;
		}
 
		sfh->executor->terminateOnError(
			*s,
			"size for klee_make_symbolic[_name] too big",
			"user.err");
	}
}


SFH_DEF_HANDLER(MarkGlobal)
{
	SFH_CHK_ARGS(1, "klee_mark_global");

	Executor::ExactResolutionList rl;
	sfh->executor->resolveExact(state, arguments[0], rl, "mark_global");

	foreach (it, rl.begin(), rl.end()) {
		MemoryObject *mo = const_cast<MemoryObject*>(it->first.first);
		assert(!mo->isLocal());
		mo->setGlobal(true);
	}
}

SFH_DEF_HANDLER(ForceNE)
{
	ref<Expr>	lhs, rhs, cmp_expr;
	bool		mustBeEqual, success;

	SFH_CHK_ARGS(2, "klee_force_ne");

	lhs = arguments[0];
	rhs = arguments[1];

	cmp_expr = NeExpr::create(lhs, rhs);

	success = sfh->executor->getSolver()->mustBeFalse(
		state, cmp_expr, mustBeEqual);

	assert(success && "FIXME: Unhandled solver failure");
	if (!mustBeEqual) {
		sfh->executor->addConstrOrDie(state, cmp_expr);
		return;
	}

	sfh->executor->terminate(state);
}

SFH_DEF_HANDLER(Yield) { sfh->executor->yield(state); }

SFH_DEF_HANDLER(IsShadowed)
{
	state.bindLocal(
		target,
		MK_CONST(arguments[0]->isShadowed(), Expr::Int32));
}

/* indirect call-- first parameter is function name */
SFH_DEF_HANDLER(Indirect0)
{
	std::vector<ref<Expr> >	args;
	std::string	fname(sfh->readStringAtAddress(state, arguments[0]));
	sfh->handleByName(state, fname, target, args);
}

SFH_DEF_HANDLER(Indirect1)
{
	std::vector<ref<Expr> >	args;
	std::string	fname(sfh->readStringAtAddress(state, arguments[0]));
	args.push_back(arguments[1]);
	sfh->handleByName(state, fname, target, args);
}

SFH_DEF_HANDLER(Indirect2)
{
	std::vector<ref<Expr> >	args;
	std::string	fname(sfh->readStringAtAddress(state, arguments[0]));
	args.push_back(arguments[1]);
	args.push_back(arguments[2]);
	sfh->handleByName(state, fname, target, args);
}

#if 0
SFH_DEF_HANDLER(GetObjSize)
{
	// XXX should type check args
	SFH_CHK_ARGS(1, "klee_get_obj_size");
	Executor::ExactResolutionList rl;
	sfh->executor->resolveExact(
		state, arguments[0], rl, "klee_get_obj_size");
	foreach (it, rl.begin(), rl.end()) {
		it->second->bindLocal(
			target,
			MK_CONST(it->first.first->size, Expr::Int32));
	}
}
#endif
SFH_DEF_HANDLER(GetObjSize)
{
	const MemoryObject	*mo;
	const ConstantExpr	*ce;

	SFH_CHK_ARGS(1, "klee_get_obj_size");

	ce = dyn_cast<ConstantExpr>(arguments[0]);
	if (ce == NULL) {
		sfh->executor->terminateOnError(
			state,
			"symbolic passed to klee_get_obj_size",
			"user.err");
		return;
	}

	mo = state.addressSpace.resolveOneMO(ce->getZExtValue());
	state.bindLocal(target, MK_CONST(mo->size, Expr::Int32));
}

SFH_DEF_HANDLER(GetObjNext)
{
	const ConstantExpr	*ce;
	uint64_t		req;
	uint64_t		obj_addr;

	SFH_CHK_ARGS(1, "klee_get_obj_next");

	ce = dyn_cast<ConstantExpr>(arguments[0]);
	if (ce == NULL) {
		sfh->executor->terminateOnError(
			state,
			"klee_get_obj_next: expected constant argument",
			"user.err");
		return;
	}

	req = ce->getZExtValue();
	if ((req + 1) < req) {
		state.bindLocal(target, MK_CONST(0, Expr::Int64));
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

SFH_DEF_HANDLER(GetObjPrev)
{
	const ConstantExpr	*ce;
	uint64_t		req;
	uint64_t		obj_addr;

	SFH_CHK_ARGS(1, "klee_get_obj_prev");

	ce = dyn_cast<ConstantExpr>(arguments[0]);
	if (ce == NULL) {
		std::cerr << "OOPS: ARG=" << arguments[0] << '\n';
		sfh->executor->terminateOnError(
			state,
			"klee_get_obj_prev: expected constant argument",
			"user.err");
		return;
	}

	req = ce->getZExtValue();
	if (req == 0) {
		state.bindLocal(target, MK_CONST(0, Expr::Int64));
		return;
	}

	MMIter	mm_it(state.addressSpace.lower_bound(req));
	if (mm_it == state.addressSpace.end())
		--mm_it;

	obj_addr = (*mm_it).first->address;
	state.bindLocal(target, MK_CONST(obj_addr, Expr::Int64));
}

/* llvm has a habit of optimizing branches into selects; sometimes this is
 * undesirable */
SFH_DEF_HANDLER(ForkEq)
{
	SFH_CHK_ARGS(2, "klee_fork_eq");

	ref<Expr>		cond(MK_EQ(arguments[0], arguments[1]));
	Executor::StatePair	sp(sfh->executor->fork(state, cond, true));

	if (sp.first != NULL) sp.first->bindLocal(target, MK_CONST(1, 32));
	if (sp.second != NULL) sp.second->bindLocal(target, MK_CONST(0, 32));
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
SFH_DEF_HANDLER(WideLoad##x) 	\
{	\
	ObjectPair		op;	\
	ref<Expr>		user_addr, val;	\
\
	if (getObjectFromBase(state, arguments[0], op) == false) {	\
		sfh->executor->terminateOnError(			\
			state,						\
			"Could not resolve addr for wide load",		\
			"mmu.err");	\
		return; \
	} \
\
	user_addr = arguments[1];	\
	val = state.read(		\
		op.second,	\
		MK_SUB(user_addr, MK_CONST(op.first->address, 64)), x);	\
	state.bindLocal(target, val);	\
}

#define wide_store_def(x)	\
SFH_DEF_HANDLER(WideStore##x)	\
{	\
	ObjectState		*os;	\
	ObjectPair		op;	\
	ref<Expr>		val, user_addr;	\
\
	if (getObjectFromBase(state, arguments[0], op) == false) {	\
		sfh->executor->terminateOnError(	\
			state,	\
			"Could not resolve addr for wide store",	\
			"mmu.err");	\
		return;	\
	}	\
	user_addr = arguments[1];	\
	val = arguments[2];		\
	if (arguments.size() == 4) 	\
		val = MK_CONCAT(arguments[3], val);	\
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
