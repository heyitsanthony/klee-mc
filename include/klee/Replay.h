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
	static bool isFasterReplay(void);
	static bool isReplayingKTest(void);
	static unsigned getMaxSuppressInst(void);

	static std::vector<KTest*> loadKTests(void);
	static std::list<ReplayPath> loadReplayPaths(void);


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
	bool replay(Executor* exe, ExecutionState* initSt) override;

private:
	void eagerReplayPathsIntoStates();
	void fastEagerReplay(void);
	void slowEagerReplay(void);
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
	bool replay(Executor* exe, ExecutionState* initSt) override;
private:
	std::vector<Replay*>	rps;
};

class ReplayKTests : public Replay
{
public:
	typedef const std::vector<KTest*> ktest_list_t;

	static ReplayKTests* create(const ktest_list_t& _kts);
	bool replay(Executor* exe, ExecutionState* initSt) override;

protected:
	ReplayKTests(const ktest_list_t& _kts) : kts(_kts) {}
	virtual ExecutionState* replayKTest(
		Executor& exe, ExecutionState&, const KTest* kt) = 0;
	virtual bool replayKTests(Executor&, ExecutionState&);

	const ktest_list_t kts;
};

class ReplayKTestsFast : public ReplayKTests
{
public:
	ReplayKTestsFast(const ktest_list_t& _kts)
		: ReplayKTests(_kts)
		, near_c(0)
	{}

protected:
	ExecutionState* replayKTest(
		Executor& exe, ExecutionState&, const KTest* kt) override;
	bool replayKTests(Executor&, ExecutionState&) override;

	unsigned near_c;
};

class ReplayKTestsSlow : public ReplayKTests
{
public:
	ReplayKTestsSlow(const ktest_list_t& _kts)
		: ReplayKTests(_kts)
	{}

protected:
	ExecutionState* replayKTest(
		Executor& exe, ExecutionState&, const KTest* kt) override;
	bool replayKTests(Executor&, ExecutionState&) override;

};

}
#endif
