#ifndef KLEE_REPLAY_H
#define KLEE_REPLAY_H

#include <vector>
#include <list>

struct KTest;
namespace klee
{
class KInstruction;

typedef std::pair<unsigned, const KInstruction*> ReplayNode;
typedef std::vector<ReplayNode> ReplayPath;
typedef std::list<ReplayPath> ReplayPaths;

class KInstIterator;
class ExecutionState;
class Executor;
class ExeStateManager;

class Replay
{
public:
	virtual ~Replay() {}
	virtual bool replay(Executor* exe, ExecutionState* initSt) = 0;

	// load a .path file
	static void loadPathFile(const std::string& name, ReplayPath &buffer);
	static void loadPathStream(std::istream& is, ReplayPath& buffer);
	static void writePathFile(
		const Executor& exe,
		const ExecutionState& es,
		std::ostream& os);

	static bool isSuppressForks(void);
	static bool isReplayOnly(void);
	static unsigned getMaxSuppressInst(void);

	static bool isCommitted(const Executor& exe, const ExecutionState& es);
	static bool verifyPath(Executor* exe, const ExecutionState& es);

	static void checkPC(const KInstIterator& ki, const ReplayNode& rn);
protected:
	Replay() {}
};

class ReplayBrPaths : public Replay
{
public:
	ReplayBrPaths(const ReplayPaths& paths)
	: rps(paths) {}
	virtual ~ReplayBrPaths() {}
	bool replay(Executor* exe, ExecutionState* initSt);
private:
	void eagerReplayPathsIntoStates();
	void fastEagerReplay(void);
	void delayedReplayPathsIntoStates();
	void incompleteReplay(void);

	const ReplayPaths&	rps;

	Executor		*exe;
	ExecutionState		*initState;
	ExeStateManager		*esm;

};

class ReplayList : public Replay
{
public:
	ReplayList(void) {}
	virtual ~ReplayList();
	void addReplay(Replay* rp) { rps.push_back(rp); }
	bool replay(Executor* exe, ExecutionState* initSt);
private:
	std::vector<Replay*>	rps;
};

class ReplayKTests : public Replay
{
public:
	ReplayKTests(const std::vector<KTest*>& _kts) : kts(_kts) {}
	virtual ~ReplayKTests() {}

	bool replay(Executor* exe, ExecutionState* initSt);
private:
	bool replayFast(Executor* exe, ExecutionState* initSt);
	void replayFast(
		Executor* exe,
		ExecutionState* initSt,
		const std::vector<KTest*>& in_kts);
	bool replaySlow(Executor* exe, ExecutionState* initSt);

	const std::vector<KTest*>& kts;
};

}

#endif
