#ifndef TERMINATOR_H
#define TERMINATOR_H

#include "klee/CallStack.h"

// these classes control the way a function is terminated
// Why would you want to do this?
// 	* Terminate in Exit case
// 		maybe fork some stuff?
// 	* Terminate in Early case
// 		concretize?
//
// What sort of messages, etc. When to emit errors, etc.
// This is important for the klee_resume_exit call. 
//

namespace klee
{
class Executor;
class ExecutionState;

class Terminator
{
public:
	virtual ~Terminator(void) = default;
	// state has already been processed and is about to be
	// deleted. Last chance to play with the state; possibly
	// forks off new states.
	virtual bool terminate(ExecutionState& es) = 0;
	// process into a test case
	virtual void process(ExecutionState& es) = 0;
	// whether state is worth processing into a test case
	virtual bool isInteresting(ExecutionState& es) const;
	virtual Terminator* copy(void) const = 0;
	Executor* getExe(void) const { return exe; }
protected:
	Terminator(Executor* _exe) : exe(_exe) {}
private:
	Executor	*exe;
};

class TermEarly : public Terminator
{
public:
	TermEarly(Executor* exe, const std::string& _msg)
	: Terminator(exe)
	, message(_msg) {}
	virtual ~TermEarly(void) = default;

	void process(ExecutionState& state) override;
	bool terminate(ExecutionState& state) override;
	bool isInteresting(ExecutionState& es) const override;
	Terminator* copy(void) const override
	{ return new TermEarly(getExe(), message); }

private:
	std::string	message;
};

class TermWrapper : public Terminator
{
public:
	TermWrapper(Terminator* t)
	: Terminator(t->getExe())
	, wrap_t(t) {}
	virtual ~TermWrapper(void) = default;

	Terminator* copy(void) const override = 0;
	void process(ExecutionState& state) override
	{ wrap_t->process(state); }
	bool terminate(ExecutionState& state) override
	{ return wrap_t->terminate(state); }
	bool isInteresting(ExecutionState& es) const override
	{ return wrap_t->isInteresting(es); }
protected:
	std::unique_ptr<Terminator> wrap_t;
};

class TermExit : public Terminator
{
public:
	TermExit(Executor* exe) : Terminator(exe) {}
	virtual ~TermExit(void) = default;

	void process(ExecutionState& state) override;
	bool terminate(ExecutionState& state) override;
	Terminator* copy(void) const override { return new TermExit(getExe()); }
};

class TermError : public Terminator
{
public:
	TermError(
		Executor* _exe,
		const ExecutionState &es,
		const std::string &_msgt,
		const std::string& _suffix,
		const std::string &_info = "",
		bool _alwaysEmit = false);
	virtual ~TermError(void) = default;

	void process(ExecutionState& state) override;
	bool terminate(ExecutionState& state) override;
	bool isInteresting(ExecutionState& es) const override;
	Terminator* copy(void) const override { return new TermError(*this); }

protected:
	TermError(const TermError& te);

private:
	void printStateErrorMessage(
		ExecutionState& state,
		const std::string& message,
		std::ostream& os);

	const std::string	messaget;
	const std::string	suffix;
	const std::string	info;
	bool			alwaysEmit;
	CallStack::insstack_ty	ins;

	typedef std::pair<CallStack::insstack_ty, std::string> errmsg_ty;
	static std::set<errmsg_ty> emittedErrors;
};
};

#endif
