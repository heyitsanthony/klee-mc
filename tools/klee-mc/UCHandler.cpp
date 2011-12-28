#include "UCHandler.h"
#include "ExeUC.h"
#include "static/Sugar.h"

using namespace klee;

void UCHandler::processSuccessfulTest(
	const char	*name,
	unsigned	id,
	out_objs	&out)
{

	out_objs			ktest_objs, uc_objs;
	out_objs::const_iterator	begin_it;

	begin_it = out.begin();
	assert (begin_it->first == "regctx1" && "Expected first to be regctx");
	uc_objs.push_back(*begin_it);

	begin_it++;
	assert (begin_it->first == "lentab_mo");
	uc_objs.push_back(*begin_it);

	begin_it++;
	foreach (it, begin_it, out.end()) {
		if (it->first.substr(0, 3) == "uc_")
			uc_objs.push_back(*it);
		else
			ktest_objs.push_back(*it);
	}

	KleeHandler::processSuccessfulTest("ucktest", id, uc_objs);
	KleeHandler::processSuccessfulTest("ktest", id, ktest_objs);
}

bool UCHandler::getStateSymObjs(
	const ExecutionState& state, out_objs& out)
{
	ExeUC*	exe_uc = static_cast<ExeUC*>(m_interpreter);
	exe_uc->finalizeBuffers(const_cast<ExecutionState&>(state));
	return m_interpreter->getSymbolicSolution(state, out);
}
