#include <iostream>
#include <fstream>
#include "static/Sugar.h"

#include "klee/ExecutionState.h"
#include "klee/Internal/Module/KInstIterator.h"
#include "klee/Internal/Module/KInstruction.h"
#include "klee/Internal/ADT/zfstream.h"
#include "klee/Internal/Module/InstructionInfoTable.h"
#include "klee/Replay.h"

using namespace klee;

// load a .path file
#define IFSMODE	std::ios::in | std::ios::binary
void Replay::loadPathFile(const std::string& name, ReplayPath& buffer)
{
	std::istream	*is;

	if (name.substr(name.size() - 3) == ".gz") {
		std::string new_name = name.substr(0, name.size() - 3);
		is = new gzifstream(name.c_str(), IFSMODE);
	} else
		is = new std::ifstream(name.c_str(), IFSMODE);

	if (is == NULL || !is->good()) {
		assert(0 && "unable to open path file");
		if (is) delete is;
		return;
	}

	while (is->good()) {
		uint64_t	value, id;

		/* get the value */
		*is >> value;
		if (!is->good()) break;
		
		/* eat the comma */
		is->get();

		/* get the location */
		*is >> id;

		/* but for now, ignore it */
		id = 0;

		/* XXX: need to get format working right for this. */
		buffer.push_back(ReplayNode(value,(const KInstruction*)id));
	}

	delete is;
}


typedef std::map<const llvm::Function*, uint64_t> f2p_ty;

void Replay::writePathFile(const ExecutionState& st, std::ostream& os)
{
	f2p_ty		f2ptr;
	std::string	fstr;

	foreach(bit, st.branchesBegin(), st.branchesEnd()) {
		const KInstruction	*ki;
		const llvm::Function	*f;
		f2p_ty::iterator	fit;
		uint64_t		v;

		os << (*bit).first;

		ki = (*bit).second;
		if (ki == NULL)
			continue;

		/* this is klee-mc specific-- should probably support
		 * llvm bitcode here eventually too */
		f = ki->getFunction();
		fit = f2ptr.find(f);
		if (fit != f2ptr.end()) {
			os << ',' << (void*)fit->second  << '\n';
			continue;
		}

		fstr = f->getName().str();
		if (fstr.substr(0, 3) != "sb_") {
			v = 0;
		} else {
			v = strtoul(fstr.substr(3).c_str(), NULL, 16);
			assert (v != 0);
		}

		f2ptr[f] = v;
		os << ',' << ((void*)v) << '\n';
	}
}
