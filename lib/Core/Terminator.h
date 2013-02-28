#ifndef TERMINATOR_H
#define TERMINATOR_H

namespace klee
{
class Executor;
class ExecutionState;

class Terminator
{
public:
	virtual ~Terminator(void) {}
	virtual bool terminate(ExecutionState& es) = 0;
	virtual void process(ExecutionState& es) = 0;
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
	virtual ~TermEarly(void) {}

	virtual void process(ExecutionState& state);
	virtual bool terminate(ExecutionState& state);
	virtual Terminator* copy(void) const
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

	virtual ~TermWrapper(void);
	virtual Terminator* copy(void) const = 0;
	virtual void process(ExecutionState& state)
	{ wrap_t->process(state); }
	virtual bool terminate(ExecutionState& state)
	{ return wrap_t->terminate(state); }
	virtual bool isInteresting(ExecutionState& es) const
	{ return wrap_t->isInteresting(es); }
protected:
	Terminator	*wrap_t;
};

class TermExit : public Terminator
{
public:
	TermExit(Executor* exe) : Terminator(exe) {}
	virtual ~TermExit(void) {}

	virtual void process(ExecutionState& state);
	virtual bool terminate(ExecutionState& state);
	virtual Terminator* copy(void) const { return new TermExit(getExe()); }
};

class TermError : public Terminator
{
public:
	TermError(
		Executor* _exe,
		const std::string &_msgt,
		const std::string& _suffix,
		const std::string &_info = "",
		bool _alwaysEmit = false)
	: Terminator(_exe)
	, messaget(_msgt)
	, suffix(_suffix)
	, info(_info)
	, alwaysEmit(_alwaysEmit)
	{}
	virtual ~TermError(void);

	virtual void process(ExecutionState& state);
	virtual bool terminate(ExecutionState& state);
	virtual bool isInteresting(ExecutionState& es) const;
	virtual Terminator* copy(void) const 
	{ return new TermError(getExe(), messaget, suffix, info, alwaysEmit); }
private:
	void printStateErrorMessage(
		ExecutionState& state,
		const std::string& message,
		std::ostream& os);

	const std::string	messaget;
	const std::string	suffix;
	const std::string	info;
	bool			alwaysEmit;
};
};

#endif
