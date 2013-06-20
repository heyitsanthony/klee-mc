#include <llvm/IR/Instructions.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Constants.h>
#include "static/Sugar.h"
#include "DynGraph.h"

using namespace llvm;

void DynGraph::addFunction(llvm::Function* f, guest_ptr base)
{
	std::list<guest_ptr>	l(getStaticReturnAddresses(f));
	foreach (it, l.begin(), l.end()) {
		static_xfers.addEdge(base, guest_ptr(*it));
	}
}

std::list<guest_ptr> DynGraph::getStaticReturnAddresses(llvm::Function* f)
{
	std::list<guest_ptr>	l;

	foreach (it, f->begin(), f->end()) {
		foreach (iit, (*it).begin(), (*it).end()) {
			const Instruction	*i = &(*iit);
			const ReturnInst	*ret;
			const ConstantInt	*c;

			ret = dyn_cast<ReturnInst>(i);
			if (ret == NULL)
				continue;

			c = dyn_cast<ConstantInt>(ret->getReturnValue());
			if (c == NULL)
				continue;

			l.push_back(guest_ptr(c->getZExtValue()));
		}
	}

	return l;
}

static const char* colors[] = {
	"blue", "green", "olivedrab", "bisque", "yellow",
	"orange", "purple", "cyan", "aquamarine", "pink", "gray"};

void DynGraph::dumpStatic(std::ostream& os) const
{
	os << "digraph static_graph {\n";
	os << "rankdir=LR;\n";
	os << "size=\"1024,1024\";\n";
	foreach (it, static_xfers.nodes.begin(), static_xfers.nodes.end()) {
		const GenericGraphNode<guest_ptr>	*gn;
		std::string			src_name;
		const char			*color;
		
		gn = *it;
		src_name = gs->getName(gn->value);
		os << '"' << src_name << '"' << " [style=filled,fillcolor=";
		if (gn->value.o > 0x80000000) {
			color = colors[(gn->value.o / 0x400000) % 10];
		} else {
			color = "red";
		}
		os << color << "]\n";
		foreach (it2, gn->succs.begin(), gn->succs.end()) {
			os	<< '"' << src_name
				<< "\" -> \"" << gs->getName((*it2)->value)
				<< "\" [penwidth=10,color="<<color<<"];\n";
		}
	}

	os << '}';
}
