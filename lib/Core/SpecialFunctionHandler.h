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
  class Handler;
  template<typename T> class ref;

  class SpecialFunctionHandler {
  public:
    typedef std::map<const llvm::Function*, std::pair<Handler*,bool> >
    	handlers_ty;

	typedef Handler*(HandlerInit)(SpecialFunctionHandler*);
	struct HandlerInfo {
	  const char *name;
	  HandlerInit* handler_init;
	  bool doesNotReturn; /// Intrinsic terminates the process
	  bool hasReturnValue; /// Intrinsic has a return value
	  bool doNotOverride; /// Intrinsic should not be used if already defined
	};


    handlers_ty handlers;
    class Executor* executor;

  protected:
	void bind(SpecialFunctionHandler::HandlerInfo* hinfo, unsigned int N);
	void prepare(SpecialFunctionHandler::HandlerInfo* hinfo, unsigned int N);

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

  class Handler
  {
  public:
  	virtual void handle(
		ExecutionState &state,
		KInstruction* target,
		std::vector<ref<Expr> > &arguments) = 0;
  	virtual ~Handler(void) {}
  protected:
  	Handler(SpecialFunctionHandler* _sfh) : sfh(_sfh) {}
	SpecialFunctionHandler	*sfh;
  };
    /* Handlers */

#define SFH_HANDLER(name) 				\
	class Handler##name : public Handler {	\
	public:	\
		Handler##name(SpecialFunctionHandler* sfh) : 	\
			Handler(sfh) {}	\
		virtual ~Handler##name() {}	\
		static Handler* create(SpecialFunctionHandler* sfh) { 	\
			return new Handler##name(sfh);		\
		}						\
	  	virtual void handle(	\
		ExecutionState &state,	\
		KInstruction* target,	\
		std::vector<ref<Expr> > &arguments);	\
	};

    SFH_HANDLER(Abort)
    SFH_HANDLER(Alarm)
    SFH_HANDLER(Assert)
    SFH_HANDLER(AssertFail)
    SFH_HANDLER(Assume)
    SFH_HANDLER(Calloc)
    SFH_HANDLER(CheckMemoryAccess)
    SFH_HANDLER(DefineFixedObject)
    SFH_HANDLER(Delete)
    SFH_HANDLER(DeleteArray)
    SFH_HANDLER(Exit)
    SFH_HANDLER(ForceNE)
    SFH_HANDLER(Free)
    SFH_HANDLER(GetPruneID)
    SFH_HANDLER(Prune)
    SFH_HANDLER(GetErrno)
    SFH_HANDLER(GetObjSize)
    SFH_HANDLER(GetValue)
    SFH_HANDLER(IsSymbolic)
    SFH_HANDLER(MakeSymbolic)
    SFH_HANDLER(Malloc)
    SFH_HANDLER(MarkGlobal)
    SFH_HANDLER(Merge)
    SFH_HANDLER(New)
    SFH_HANDLER(NewArray)
    SFH_HANDLER(PreferCex)
    SFH_HANDLER(PrintExpr)
    SFH_HANDLER(PrintRange)
    SFH_HANDLER(Range)
    SFH_HANDLER(Realloc)
    SFH_HANDLER(ReportError)
    SFH_HANDLER(RevirtObjects)
    SFH_HANDLER(SetForking)
    SFH_HANDLER(SilentExit)
    SFH_HANDLER(StackTrace)
    SFH_HANDLER(UnderConstrained)
    SFH_HANDLER(Warning)
    SFH_HANDLER(WarningOnce)
} // End klee namespace

#endif
