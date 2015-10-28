#ifndef INTERLEAVEDSEARCHER_H
#define INTERLEAVEDSEARCHER_H

#include "../Core/Searcher.h"

namespace klee
{
  class InterleavedSearcher : public Searcher
  {
    typedef std::vector<Searcher*> searchers_ty;

    searchers_ty searchers;
    unsigned index;
    
  public:
    explicit InterleavedSearcher(const searchers_ty &_searchers);
    virtual ~InterleavedSearcher();

    virtual Searcher* createEmpty(void) const;

    ExecutionState *selectState(bool allowCompact);
    void update(ExecutionState *current, const States s);
    bool empty() const { return searchers[0]->empty(); }
    void printName(std::ostream &os) const
    {
      os << "<InterleavedSearcher> containing "
         << searchers.size() << " searchers:\n";
      for (searchers_ty::const_iterator
        it = searchers.begin(), ie = searchers.end();
        it != ie; ++it)
        (*it)->printName(os);
      os << "</InterleavedSearcher>\n";
    }
  };
}

#endif
