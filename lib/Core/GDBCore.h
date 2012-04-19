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
	bool isStopped(void) const { return run_state == Stopped; }

	void setSingleStep(void) { run_state = Single; }
	void setStopped(void) { run_state = Stopped; }
	void setRunning(void) { run_state = Cont; }
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
	GDBCore		*gc;
	Executor	*exe;
};

}

#endif
