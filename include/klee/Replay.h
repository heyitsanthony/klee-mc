#ifndef KLEE_REPLAY_H
#define KLEE_REPLAY_H

#include <vector>
#include <list>

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
	static void checkPC(const KInstIterator& ki, const ReplayNode& rn);

	// load a .path file
	static void loadPathFile(const std::string& name, ReplayPath &buffer);
	static void writePathFile(const ExecutionState& es, std::ostream& os);

	static void replayPathsIntoStates(
		Executor* exe,
		ExecutionState* initialState,
		const ReplayPaths& replayPaths);

	static bool verifyPath(
		Executor* exe,
		const ExecutionState& es);
protected:
	Replay(	Executor* _exe,
		ExecutionState* _initState,
		const ReplayPaths& _rps);

	void eagerReplayPathsIntoStates();
	void fastEagerReplay(void);
	void delayedReplayPathsIntoStates();
	void incompleteReplay(void);
private:
	Executor		*exe;
	ExecutionState		*initState;
	const ReplayPaths	&replayPaths;
	ExeStateManager		*esm;
};
}

#endif
