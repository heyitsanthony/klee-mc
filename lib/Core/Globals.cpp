#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/IR/DataLayout.h>
#include <assert.h>

#include "klee/Expr.h"
#include "klee/Common.h"
#include "klee/ExecutionState.h"
#include "klee/Internal/Module/KModule.h"
#include "static/Sugar.h"

#include "ExternalDispatcher.h"
#include "Globals.h"
#include "Executor.h"

using namespace klee;
using namespace llvm;

namespace { cl::opt<bool> UseAsmAddresses("use-asm-addresses"); }

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

	setupCTypes();
	updateModule();
}

void Globals::updateModule(void)
{
	Module	*m = kmodule->module;

	setupFuncAddrs(m);
	setupGlobalObjects(m);
	setupAliases(m);
	setupGlobalData(m);
}

MemoryObject *Globals::addExternalObject(
	void *addr, unsigned size, bool isReadOnly)
{
	const ObjectState 	*os_c;
	const MemoryObject	*mo;
	ObjectState		*os;

	os_c = init_state->allocateFixed((uint64_t) (uintptr_t)addr, size, 0);
	mo = init_state->addressSpace.resolveOneMO((uintptr_t)addr);
	os = init_state->addressSpace.getWriteable(mo, os_c);

	for(unsigned i = 0; i < size; i++)
		init_state->write8(os, i, ((uint8_t*)addr)[i]);

	if (isReadOnly) os->setReadOnly(true);

	return const_cast<MemoryObject*>(mo);
}


void Globals::allocGlobalVariableDecl(const GlobalVariable& gv)
{
	ObjectState	*os;
	ObjectPair	op;
	Type		*ty;
	uint64_t	size;

	assert (gv.isDeclaration());
	// FIXME: We have no general way of handling unknown external
	// symbols. If we really cared about making external stuff work
	// better we could support user definition, or use the EXE style
	// hack where we check the object file information.

	ty = gv.getType()->getElementType();
	size = kmodule->dataLayout->getTypeStoreSize(ty);

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

	op = init_state->allocate(size, false, true, &gv);
	globalObjects.insert(std::make_pair(
		&gv, const_cast<MemoryObject*>(op_mo(op))));
	globalAddresses.insert(std::make_pair(&gv, op_mo(op)->getBaseExpr()));

	// Program already running = object already initialized.  Read
	// concrete value and write it to our copy.
	if (size == 0) return;

	void *addr = NULL;
	if (gv.getName() == "__dso_handle") {
		extern void *__dso_handle __attribute__ ((__weak__));
		addr = &__dso_handle; // wtf ?
	} else if (ed) {
		addr = ed->resolveSymbol(gv.getName().str());
	}

	if (addr == NULL) {
		klee_warning(
			"ERROR: unable to get symbol(%s) while loading globals.",
			gv.getName().data());
		return;
	}

	os = init_state->addressSpace.getWriteable(op);
	for (unsigned offset=0; offset < op_mo(op)->size; offset++) {
		//os->write8(offset, ((unsigned char*)addr)[offset]);
		init_state->write8(os, offset, ((unsigned char*)addr)[offset]);
	}
}

/* XXX needs a better name */
void Globals::allocGlobalVariableNoDecl(const GlobalVariable& gv)
{
	ObjectPair	op(NULL,NULL);
	Type *ty = gv.getType()->getElementType();
	uint64_t size = kmodule->dataLayout->getTypeStoreSize(ty);

	if (UseAsmAddresses && gv.getName()[0]=='\01') {
		char *end;
		uint64_t address = ::strtoll(gv.getName().str().c_str()+1, &end, 0);

		if (end && *end == '\0') {
		// We can't use the PRIu64 macro here for some reason, so we have to
		// cast to long long unsigned int to avoid compiler warnings.
			klee_message(
			"NOTE: allocated global at asm specified address: %#08llx"
			" (%llu bytes)",
			(long long unsigned int) address,
			(long long unsigned int) size);
			op.second = init_state->allocateFixed(address, size, &gv);
			op.first = init_state->addressSpace.resolveOneMO(address);
			// XXX hack;
			((MemoryObject*)op_mo(op))->isUserSpecified = true;
		}
	}

	if (op_mo(op) == NULL)
		op = init_state->allocate(size, false, true, &gv);
	assert(op_os(op) && "out of memory");

	globalObjects.insert(std::make_pair(
		&gv, const_cast<MemoryObject*>(op_mo(op))));
	globalAddresses.insert(std::make_pair(&gv, op_mo(op)->getBaseExpr()));

	if (!gv.hasInitializer()) {
		ObjectState	*os;
		os = init_state->addressSpace.getWriteable(op);
		os->initializeToRandom();
	}
}

static void writeConstDataSeq(
	ExecutionState		*init_state,
	ObjectState		*os,
	ConstantDataSequential	*csq,
	unsigned		offset)
{
	unsigned	bytes_per_elem;
	unsigned	elem_c;
	Type		*t;

	bytes_per_elem = csq->getElementByteSize();
	elem_c = csq->getNumElements();
	assert (bytes_per_elem <= 8);

	t = csq->getElementType();

	for (unsigned i = 0; i < elem_c; i++) {
		ref<klee::ConstantExpr>	ce;
		unsigned		bits = bytes_per_elem*8;

		if (t->isFloatTy()) {
			float	f = csq->getElementAsFloat(i);
			ce = klee::MK_CONST(*((uint32_t*)&f), bits);
		} else if (t->isDoubleTy()) {
			double d = csq->getElementAsDouble(i);
			ce = klee::MK_CONST(*((uint64_t*)&d), bits);
		} else if (t->isIntegerTy()) {
			ce = klee::MK_CONST(csq->getElementAsInteger(i), bits);
		} else {
			std::cerr << "Weird type: ";
			t->dump();
			abort();
		}

		init_state->write(os, offset+(i*bytes_per_elem), ce);
	}
}


void Globals::initializeGlobalObject(
	ObjectState *os,
	Constant *c,
 	unsigned offset)
{
	if (isa<ConstantAggregateZero>(c)) {
		unsigned size;
		size = kmodule->dataLayout->getTypeStoreSize(c->getType());
		assert (size + offset <= os->size);
		for (unsigned i=0; i<size; i++) {
			init_state->write8(os,offset+i, (uint8_t) 0);
		}
		return;
	}

	if (ConstantArray *ca = dyn_cast<ConstantArray>(c)) {
		unsigned elementSize;
		elementSize = kmodule->dataLayout->getTypeStoreSize(
			ca->getType()->getElementType());
		for (unsigned i=0, e=ca->getNumOperands(); i != e; ++i)
			initializeGlobalObject(
				os,
				ca->getOperand(i),
				offset + i*elementSize);
		return;
	}

	if (ConstantDataSequential *csq=dyn_cast<ConstantDataSequential>(c)) {
		writeConstDataSeq(init_state, os, csq, offset);
		return;
	}

	if (ConstantStruct *cs = dyn_cast<ConstantStruct>(c)) {
		const StructLayout *sl;
		sl = kmodule->dataLayout->getStructLayout(
			cast<StructType>(cs->getType()));
		for (unsigned i=0, e=cs->getNumOperands(); i != e; ++i)
			initializeGlobalObject(
				os,
				cs->getOperand(i),
				offset + sl->getElementOffset(i));
		return;
	}

	if (ConstantVector *cp = dyn_cast<ConstantVector>(c)) {
		unsigned elementSize;

		elementSize = kmodule->dataLayout->getTypeStoreSize(
			cp->getType()->getElementType());
		for (unsigned i=0, e=cp->getNumOperands(); i != e; ++i)
			initializeGlobalObject(
				os,
				cp->getOperand(i),
				offset + i*elementSize);
		return;
	}

	if (isa<UndefValue>(c))
		return;

	unsigned StoreBits;
	ref<ConstantExpr> C;

	C = Executor::evalConstant(kmodule, this, c);
	StoreBits = kmodule->dataLayout->getTypeStoreSizeInBits(c->getType());

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
	Function	*f;

	if (it != globalAddresses.end()) return it->second;

	/* this is stupid, but it shouldn't happen often */
	foreach (it, kmodule->module->begin(), kmodule->module->end()) {
		f = it;
		if (f == gv) break;
		f = NULL;
	}
	if (f == NULL) return NULL;

	ref<ConstantExpr> addr(Expr::createPointer((uint64_t) (void*) f));

	globalAddresses.insert(std::make_pair(f, addr));
	legalFunctions.insert((uint64_t) (void*) f);

	return addr;
}

// represent function globals using the address of the actual llvm function
// object. given that we use malloc to allocate memory in states this also
// ensures that we won't conflict. we don't need to allocate a memory object
// since reading/writing via a function pointer is unsupported anyway.
void Globals::setupFuncAddrs(llvm::Module* m)
{
	foreach (i, m->begin(), m->end()) {
		Function		*f = i;
		ref<ConstantExpr>	addr(0);

		// If the symbol has external weak linkage then it is implicitly
		// not defined in this module; if it isn't resolvable then it
		// should be null.
		if (	f->hasExternalWeakLinkage() &&
			(ed == NULL || !ed->resolveSymbol(f->getName().str())))
		{
			addr = Expr::createPointer(0);
			std::cerr
				<< "KLEE:ERROR: "
				"global weak function linkage of is missing? "
				<< f->getName().str() << std::endl;
		} else {
			addr = Expr::createPointer((uint64_t) (void*) f);
			legalFunctions.insert((uint64_t) (void*) f);
		}

		globalAddresses.insert(std::make_pair(f, addr));
	}
}

// Disabled, we don't want to promote use of live externals.
void Globals::setupCTypes(void)
{
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
}

// allocate and initialize globals, done in two passes since we may
// need address of a global in order to initialize some other one.
void Globals::setupGlobalObjects(llvm::Module* m)
{
	// allocate memory objects for all globals
	foreach (i, m->global_begin(), m->global_end()) {
		if (globalAddresses.count(&(*i)))
			continue;

		if (i->isDeclaration())
			allocGlobalVariableDecl(*i);
		else
			allocGlobalVariableNoDecl(*i);
	}
}

void Globals::setupAliases(llvm::Module* m)
{
	// link aliases to their definitions (if bound)
	foreach (i, m->alias_begin(), m->alias_end()) {
		// Map the alias to its aliasee's address.
		// This works because we have addresses for everything,
		// even undefined functions.
		if (globalAddresses.count(i))
			continue;
		globalAddresses.insert(
			std::make_pair(i,
				Executor::evalConstant(
					kmodule, this, i->getAliasee())));
	}
}

void Globals::setupGlobalData(llvm::Module* m)
{
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
