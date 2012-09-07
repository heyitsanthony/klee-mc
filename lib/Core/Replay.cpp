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
		unsigned value, id;

		/* get the value */
		*is >> value;
		if (!is->good()) break;
		
		/* eat the comma */
		is->get();

		/* get the location */
		*is >> id;
		assert (id == 0);

		/* skip the newline */
		is->get();

		/* XXX: need to get format working right for this. */
		id = 0;
		buffer.push_back(ReplayNode(value,(const KInstruction*)0));
	}

	delete is;
}


void Replay::writePathFile(const ExecutionState& st, std::ostream& os)
{
	foreach(bit, st.branchesBegin(), st.branchesEnd()) {
		os	<< (*bit).first
			<< ",0\n"; // (*bit).second
	}
}
