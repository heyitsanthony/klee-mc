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

class Replay
{
public:
	virtual ~Replay() {}
	static void checkPC(const KInstIterator& ki, const ReplayNode& rn);

	// load a .path file
	static void loadPathFile(const std::string& name, ReplayPath &buffer);
	static void writePathFile(const ExecutionState& es, std::ostream& os);
protected:
	Replay() {}

private:
};
}

#endif
