#ifndef COVSET_H
#define COVSET_H

#include <memory>
#include <vector>
#include <set>
#include <map>

namespace klee
{

class KFunction;

// fast set for tracking function coverage among states
class CovSet
{
public:
	typedef std::set<const KFunction*>  covset_t;

	CovSet();
	CovSet(const CovSet& cs);
	~CovSet() { garbageCollect(covered); }

	void insert(const KFunction* kf);

	const covset_t& getCovered() const {
		flushPending();
		return *covered;
	}

	bool isShared() const {
		flushPending();
		return covered.use_count() > 2;
	}

	bool isCommitted() const { return numUncommitted() == 0; }
	unsigned numUncommitted(void) const;
	void commit(void) const;

private:
	void flushPending(void) const;
	static void garbageCollect(const std::shared_ptr<covset_t>& cs);

	mutable std::shared_ptr<covset_t>		covered;
	mutable std::vector<const KFunction*>		pending;
	mutable unsigned				committed_c;
	mutable unsigned				uncommitted_c;
	mutable unsigned				local_epoch;

	// used for global caching
	static std::map<unsigned /* size*/, 
			std::set<std::shared_ptr<covset_t>>> all_sets;
	static unsigned commit_epoch;
};

}
#endif