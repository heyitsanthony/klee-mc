#ifndef GDBCORE_H
#define GDBCORE_H

#include <vector>
#include <string>

namespace klee
{
class Executor;

class GDBCmd
{
public:
	GDBCmd(const std::string& _s);
	virtual ~GDBCmd(void) {}
	const std::string& getStr(void) { return s; }
private:
	void tokenize(void);
	std::string			s;
	std::vector<std::string>	tokens;
};

class GDBCmdHandler
{
public:
	GDBCmdHandler(void) {}
	virtual ~GDBCmdHandler() {}
	virtual bool handle(GDBCmd* gcmd) = 0;
private:
};

class GDBCore
{
public:
	GDBCore(Executor* _exe, unsigned port=55555);
	virtual ~GDBCore();
	GDBCmd* getNextCmd(void);
	GDBCmd* waitNextCmd(void);
	void addHandler(GDBCmdHandler* h) { handlers.push_back(h); }
	void writePkt(const char* dat);
	void writeStateRead(
		const std::vector<uint8_t>& v,
		const std::vector<bool>& is_conc);
	void ack(bool ok_ack = true);
	bool isSingleStep(void) const { return run_state == Single; }
	bool isRunning(void) const
	{ return run_state == Cont || run_state == Single; }
	bool isCont(void) const { return run_state == Cont; }
	bool isStopped(void) const { return run_state == Stopped; }

	void setSingleStep(void) { run_state = Single; }
	void setStopped(void) { run_state = Stopped; }
	void setRunning(void) { run_state = Cont; }
	void setPendingBreak(void) { pending_break = true; }
	void setSelThread(ExecutionState* es) { sel_thread = es; }
	void overrideThread(ExecutionState* es);
	ExecutionState* getSelThread(void);

	void setWatchForkBranch(bool v) { watch_fork_br = v; }
	bool watchForkBranch(void) const { return watch_fork_br; }

	void handleForkBranch(void);
private:
	void handleCmd(GDBCmd* gcmd);
	int opensock(void);
	int acceptsock(void);

	short	port;
	int	fd_listen;
	int	fd_client;

	std::vector<char>		incoming_buf;
	std::vector<GDBCmdHandler*>	handlers;
	Executor			*exe; /* owner */
	enum RunState { Stopped, Single, Cont } run_state;
	bool				pending_break;

	ExecutionState			*sel_thread;
	bool				watch_fork_br;
};

class GDBQueryPkt : public GDBCmdHandler
{
public:
	GDBQueryPkt(GDBCore* _gc, Executor* _exe) : gc(_gc), exe(_exe) {}
	virtual ~GDBQueryPkt(void) {}
	virtual bool handle(GDBCmd* gcmd);
private:
	GDBCore		*gc;
	Executor	*exe;
};


class GDBPkt : public GDBCmdHandler
{
public:
	GDBPkt(GDBCore* _gc, Executor* _exe) : gc(_gc), exe(_exe) {}
	virtual ~GDBPkt(void) {}
	virtual bool handle(GDBCmd* gcmd);
private:
	void vcont(const char* fields);
	bool checkSignal(const char* sigstr);

	GDBCore		*gc;
	Executor	*exe;
};

}

#endif
