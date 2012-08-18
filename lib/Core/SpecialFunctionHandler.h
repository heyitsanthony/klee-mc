//===-- SpecialFunctionHandler.h --------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_SPECIALFUNCTIONHANDLER_H
#define KLEE_SPECIALFUNCTIONHANDLER_H

#include <map>
#include <vector>
#include <string>

namespace llvm {
  class Function;
}

#define SFH_CHK_ARGS(x,y)	\
	assert (arguments.size()==x && "invalid number of arguments to "y)
#define SFH_DEF_HANDLER(x)		\
void Handler##x::handle(		\
	ExecutionState	&state,		\
	KInstruction	*target,	\
	std::vector<ref<Expr> >& arguments)

namespace klee {
class ExecutorBC;
class Expr;
class ExecutionState;
class KInstruction;
class SFHandler;
class SpecialFunctionHandler;
template<typename T> class ref;

class SFHandler
{
public:
	virtual void handle(
		ExecutionState &state,
		KInstruction* target,
		std::vector<ref<Expr> > &arguments) = 0;
	virtual ~SFHandler(void) {}
protected:
	SFHandler(SpecialFunctionHandler* _sfh) : sfh(_sfh) {}
	SpecialFunctionHandler	*sfh;
};

class SpecialFunctionHandler
{
public:
	typedef std::map<const llvm::Function*, std::pair<SFHandler*,bool> >
		handlers_ty;


	typedef SFHandler*(HandlerInit)(SpecialFunctionHandler*);
	struct HandlerInfo {
	  const char *name;
	  HandlerInit* handler_init;
	  bool doesNotReturn; /// Intrinsic terminates the process
	  bool hasReturnValue; /// Intrinsic has a return value
	  bool doNotOverride; /// Intrinsic should not be used if already defined
	};


    handlers_ty		handlers;
    class Executor* executor;

protected:
	void bind(const HandlerInfo* hinfo, unsigned int N);
	void prepare(HandlerInfo* hinfo, unsigned int N);

private:
	bool lateBind(const llvm::Function *f);
	typedef std::map<std::string, const HandlerInfo*> latebindings_ty;
	latebindings_ty lateBindings;

public:
    SpecialFunctionHandler(Executor* _executor);
    virtual ~SpecialFunctionHandler();

    /// Perform any modifications on the LLVM module before it is
    /// prepared for execution. At the moment this involves deleting
    /// unused function bodies and marking intrinsics with appropriate
    /// flags for use in optimizations.
    virtual void prepare();

    /// Initialize the internal handler map after the module has been
    /// prepared for execution.
    virtual void bind();

    bool handle(ExecutionState &state,
                llvm::Function *f,
                KInstruction *target,
                std::vector< ref<Expr> > &arguments);

	void handleByName(
		ExecutionState		&state,
		const std::string	&fname,
		KInstruction		*target);

    SFHandler* addHandler(const struct HandlerInfo& hi);

    /* Convenience routines */

    std::string readStringAtAddress(ExecutionState &state, ref<Expr> address);
    unsigned char* readBytesAtAddress(
		ExecutionState &state,
		ref<Expr> addressExpr,
		unsigned int maxlen,
		unsigned int& len,
		int terminator = -1);
    unsigned char* readBytesAtAddressNoBound(
		ExecutionState &state,
		ref<Expr> addressExpr,
		unsigned int& len,
		int terminator = -1);

  };

/* Handlers */
#define SFH_HANDLER2(name,x) 				\
	class Handler##name : public SFHandler {	\
	public:	\
		Handler##name(SpecialFunctionHandler* sfh)	\
		: SFHandler(sfh) {}	\
		virtual ~Handler##name() {}	\
		static SFHandler* create(SpecialFunctionHandler* sfh) \
		{ return new Handler##name(sfh); }	\
	  	virtual void handle(	\
			ExecutionState &state,	\
			KInstruction* target,	\
			std::vector<ref<Expr> > &arguments);	\
		x;	\
	};

#define SFH_HANDLER(name)	SFH_HANDLER2(name,;)

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
    SFH_HANDLER(Indirect)
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
} // End klee namespace

#endif
