#include <llvm/Constants.h>
#include <llvm/DerivedTypes.h>
#include <llvm/Module.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Target/TargetData.h>
#include <assert.h>

#include "klee/Common.h"
#include "klee/ExecutionState.h"
#include "klee/Internal/Module/KModule.h"
#include "static/Sugar.h"

#include "ExternalDispatcher.h"
#include "Globals.h"
#include "Executor.h"

using namespace klee;
using namespace llvm;

namespace
{
	cl::opt<bool> UseAsmAddresses("use-asm-addresses", cl::init(false));
}

Globals::Globals(
	const KModule* km,
	ExecutionState* in_st,
	const ExternalDispatcher* in_ed)
: kmodule(km)
, init_state(in_st)
, ed(in_ed)
{
	Module *m;

	m = kmodule->module;

	if (m->getModuleInlineAsm() != "")
		klee_warning("executable has module level assembly (ignoring)");

	assert (m->lib_begin() == m->lib_end() &&
		"XXX do not support dependent libraries");

	// represent function globals using the address of the actual llvm function
	// object. given that we use malloc to allocate memory in states this also
	// ensures that we won't conflict. we don't need to allocate a memory object
	// since reading/writing via a function pointer is unsupported anyway.
	foreach (i, m->begin(), m->end()) {
		Function		*f = i;
		ref<ConstantExpr>	addr(0);

		// If the symbol has external weak linkage then it is implicitly
		// not defined in this module; if it isn't resolvable then it
		// should be null.
		if (	f->hasExternalWeakLinkage() &&
			(ed == NULL || !ed->resolveSymbol(f->getNameStr())))
		{
			addr = Expr::createPointer(0);
			std::cerr
			<< "KLEE:ERROR: couldn't find symbol for weak linkage of "
			   "global function: " << f->getNameStr() << std::endl;
		} else {
			addr = Expr::createPointer((uint64_t) (void*) f);
			legalFunctions.insert((uint64_t) (void*) f);
		}

		globalAddresses.insert(std::make_pair(f, addr));
	}

// Disabled, we don't want to promote use of live externals.
#ifdef HAVE_CTYPE_EXTERNALS
#ifndef WINDOWS
#ifndef DARWIN
	/* From /usr/include/errno.h: it [errno] is a per-thread variable. */
	int *errno_addr = __errno_location();
	addExternalObject((void *)errno_addr, sizeof *errno_addr, false);

	/* from /usr/include/ctype.h:
	These point into arrays of 384, so they can be indexed by any `unsigned
	char' value [0,255]; by EOF (-1); or by any `signed char' value
	[-128,-1).  ISO C requires that the ctype functions work for `unsigned */
	const uint16_t **addr = __ctype_b_loc();
	addExternalObject((void *)(*addr-128), 384 * sizeof **addr, true);
	addExternalObject(addr, sizeof(*addr), true);

	const int32_t **lower_addr = __ctype_tolower_loc();
	addExternalObject(
		(void *)(*lower_addr-128), 384 * sizeof **lower_addr, true);
	addExternalObject(lower_addr, sizeof(*lower_addr), true);

	const int32_t **upper_addr = __ctype_toupper_loc();
	addExternalObject(
		(void *)(*upper_addr-128), 384 * sizeof **upper_addr, true);
	addExternalObject(upper_addr, sizeof(*upper_addr), true);
#endif
#endif
#endif

	// allocate and initialize globals, done in two passes since we may
	// need address of a global in order to initialize some other one.

	// allocate memory objects for all globals
	foreach (i, m->global_begin(), m->global_end()) {
		if (i->isDeclaration())
			allocGlobalVariableDecl(*i);
		else
			allocGlobalVariableNoDecl(*i);
	}

	// link aliases to their definitions (if bound)
	foreach (i, m->alias_begin(), m->alias_end()) {
		// Map the alias to its aliasee's address.
		// This works because we have addresses for everything,
		// even undefined functions.
		globalAddresses.insert(
			std::make_pair(i,
				Executor::evalConstant(
					kmodule, this, i->getAliasee())));
	}

	// once all objects are allocated, do the actual initialization
	foreach (i, m->global_begin(), m->global_end()) {
		MemoryObject		*mo;
		const ObjectState	*os;
		ObjectState		*wos;

		if (!i->hasInitializer()) continue;

		mo = globalObjects.find(i)->second;
		os = init_state->addressSpace.findObject(mo);

		assert(os);

		wos = init_state->addressSpace.getWriteable(mo, os);

		initializeGlobalObject(wos, i->getInitializer(), 0);
		// if (i->isConstant()) os->setReadOnly(true);
	}
}

MemoryObject *Globals::addExternalObject(
	void *addr, unsigned size, bool isReadOnly)
{
	ObjectState *os;

	os = init_state->allocateFixed((uint64_t) (uintptr_t)addr, size, 0);
	for(unsigned i = 0; i < size; i++)
		init_state->write8(os, i, ((uint8_t*)addr)[i]);

	if (isReadOnly) os->setReadOnly(true);

	return os->getObject();
}


void Globals::allocGlobalVariableDecl(const GlobalVariable& gv)
{
	MemoryObject	*mo;
	ObjectState	*os;
	Type		*ty;
	uint64_t	size;

	assert (gv.isDeclaration());
	// FIXME: We have no general way of handling unknown external
	// symbols. If we really cared about making external stuff work
	// better we could support user definition, or use the EXE style
	// hack where we check the object file information.

	ty = gv.getType()->getElementType();
	size = kmodule->targetData->getTypeStoreSize(ty);

	// XXX - DWD - hardcode some things until we decide how to fix.
#ifndef WINDOWS
	if (gv.getName() == "_ZTVN10__cxxabiv117__class_type_infoE") {
		size = 0x2C;
	} else if (gv.getName() == "_ZTVN10__cxxabiv120__si_class_type_infoE") {
		size = 0x2C;
	} else if (gv.getName() == "_ZTVN10__cxxabiv121__vmi_class_type_infoE") {
		size = 0x2C;
	}
#endif

	if (size == 0) {
		std::cerr << "Unable to find size for global variable: "
		<< gv.getName().data()
		<< " (use will result in out of bounds access)\n";
	}

	os = init_state->allocate(size, false, true, &gv);
	mo = os->getObject();
	globalObjects.insert(std::make_pair(&gv, mo));
	globalAddresses.insert(std::make_pair(&gv, mo->getBaseExpr()));

	// Program already running = object already initialized.  Read
	// concrete value and write it to our copy.
	if (size == 0) return;

	void *addr = NULL;
	if (gv.getName() == "__dso_handle") {
		extern void *__dso_handle __attribute__ ((__weak__));
		addr = &__dso_handle; // wtf ?
	} else if (ed) {
		addr = ed->resolveSymbol(gv.getNameStr());
	}

	if (addr == NULL) {
		klee_warning(
			"ERROR: unable to get symbol(%s) while loading globals.",
			gv.getName().data());
	} else {
		for (unsigned offset=0; offset < mo->size; offset++) {
			//os->write8(offset, ((unsigned char*)addr)[offset]);
			init_state->write8(
				os, offset, ((unsigned char*)addr)[offset]);
		}
	}
}

/* XXX needs a better name */
void Globals::allocGlobalVariableNoDecl(const GlobalVariable& gv)
{
	Type *ty = gv.getType()->getElementType();
	uint64_t size = kmodule->targetData->getTypeStoreSize(ty);
	MemoryObject *mo = 0;
	ObjectState *os = 0;

	if (UseAsmAddresses && gv.getName()[0]=='\01') {
		char *end;
		uint64_t address = ::strtoll(gv.getNameStr().c_str()+1, &end, 0);

		if (end && *end == '\0') {
		// We can't use the PRIu64 macro here for some reason, so we have to
		// cast to long long unsigned int to avoid compiler warnings.
			klee_message(
			"NOTE: allocated global at asm specified address: %#08llx"
			" (%llu bytes)",
			(long long unsigned int) address,
			(long long unsigned int) size);
			os = init_state->allocateFixed(address, size, &gv);
			mo = os->getObject();
			mo->isUserSpecified = true; // XXX hack;
		}
	}

	if (os == NULL) os = init_state->allocate(size, false, true, &gv);
	assert(os && "out of memory");

	mo = os->getObject();
	globalObjects.insert(std::make_pair(&gv, mo));
	globalAddresses.insert(std::make_pair(&gv, mo->getBaseExpr()));

	if (!gv.hasInitializer()) os->initializeToRandom();
}


void Globals::initializeGlobalObject(
	ObjectState *os,
	Constant *c,
 	unsigned offset)
{
	if (ConstantVector *cp = dyn_cast<ConstantVector>(c)) {
		unsigned elementSize;

		elementSize = kmodule->targetData->getTypeStoreSize(
			cp->getType()->getElementType());
		for (unsigned i=0, e=cp->getNumOperands(); i != e; ++i)
			initializeGlobalObject(
				os,
				cp->getOperand(i),
				offset + i*elementSize);
		return;
	}

	if (isa<ConstantAggregateZero>(c)) {
		unsigned size;
		size = kmodule->targetData->getTypeStoreSize(c->getType());
		assert (size + offset <= os->getObject()->size);
		for (unsigned i=0; i<size; i++) {
			init_state->write8(os,offset+i, (uint8_t) 0);
		}
		return;
	}

	if (ConstantArray *ca = dyn_cast<ConstantArray>(c)) {
		unsigned elementSize;
		elementSize = kmodule->targetData->getTypeStoreSize(
			ca->getType()->getElementType());
		for (unsigned i=0, e=ca->getNumOperands(); i != e; ++i)
			initializeGlobalObject(
				os,
				ca->getOperand(i),
				offset + i*elementSize);
		return;
	}

	if (ConstantStruct *cs = dyn_cast<ConstantStruct>(c)) {
		const StructLayout *sl;
		sl = kmodule->targetData->getStructLayout(
			cast<StructType>(cs->getType()));
		for (unsigned i=0, e=cs->getNumOperands(); i != e; ++i)
			initializeGlobalObject(
				os,
				cs->getOperand(i),
				offset + sl->getElementOffset(i));
		return;
	}

	unsigned StoreBits;
	ref<ConstantExpr> C;

	C = Executor::evalConstant(kmodule, this, c);
	StoreBits = kmodule->targetData->getTypeStoreSizeInBits(c->getType());

	// Extend the constant if necessary;
	assert(StoreBits >= C->getWidth() && "Invalid store size!");
	if (StoreBits > C->getWidth())
		C = C->ZExt(StoreBits);

	//os->write(offset, C);
	init_state->write(os, offset, C);
}


MemoryObject* Globals::findObject(const llvm::GlobalValue* gv) const
{
	globalobj_map::const_iterator	it;

	it = globalObjects.find(gv);
	if (it == globalObjects.end())
		return NULL;

	return it->second;
}

ref<klee::ConstantExpr> Globals::findAddress(
	const llvm::GlobalValue* gv) const
{
	globaladdr_map::const_iterator it(globalAddresses.find(gv));

	if (it == globalAddresses.end()) return NULL;

	return it->second;
}
