#ifndef DYNGRAPH_H
#define DYNGRAPH_H

#include "guest.h"
#include "static/Graph.h"

#include <list>

namespace llvm
{
	class Function;
};

class DynGraph
{
public:
	DynGraph(const Guest* _gs) : gs(_gs) {}
	virtual ~DynGraph() {}

	void addFunction(llvm::Function* f, guest_ptr base);
	void addXfer(guest_ptr src_addr, guest_ptr dst_addr)
	{ dyn_xfers.addEdge(src_addr, dst_addr); }

	void dumpStatic(std::ostream& os) const;
private:
	std::list<guest_ptr> getStaticReturnAddresses(llvm::Function* f);
	GenericGraph<guest_ptr>		static_xfers;
	GenericGraph<guest_ptr>		dyn_xfers;
	const Guest			*gs;
};

#endif
