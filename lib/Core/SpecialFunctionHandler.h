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
#include <stdint.h>

namespace llvm { class Function; }

#define SFH_CHK_ARGS(x,y)	\
	assert (args.size()==x && "invalid number of args to "y)
#define SFH_DEF_HANDLER(x)		\
void Handler##x::handle(		\
	ExecutionState	&state,		\
	KInstruction	*target,	\
	std::vector<ref<Expr> >& args)

namespace klee
{
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
		std::vector<ref<Expr> > &args) = 0;
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

	bool handle(
		ExecutionState	&state,
		llvm::Function	*f,
		KInstruction	*target,
		std::vector< ref<Expr> > &args,
		bool insert_ret_vals = false);

	void handleByName(
		ExecutionState		&state,
		const std::string	&fname,
		KInstruction		*target,
		std::vector< ref<Expr> >& args);

	SFHandler* addHandler(const struct HandlerInfo& hi);

	/* Convenience routines */

    std::string readStringAtAddress(
    	ExecutionState &state,
	const ref<Expr> &address);
    unsigned char* readBytesAtAddress(
		ExecutionState &state,
		const ref<Expr> &addressExpr,
		unsigned int maxlen,
		unsigned int& len,
		int terminator = -1);
    unsigned char* readBytesAtAddressNoBound(
		ExecutionState &state,
		const ref<Expr> &addressExpr,
		unsigned int& len,
		int terminator = -1);

};

/* Handler macros */
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
			std::vector<ref<Expr> > &args);	\
		x;	\
	};

#define SFH_HANDLER(name)	SFH_HANDLER2(name,;)

#define SFH_ADD_REG(x,y) HandlerReadReg::vars.insert(std::make_pair(x, y))
typedef std::map<std::string, uint64_t>	readreg_map_ty;
SFH_HANDLER2(ReadReg, static readreg_map_ty vars)
}

#endif
