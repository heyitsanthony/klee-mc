#include "klee/Constraints.h"
#include "klee/Query.h"
#include "QueryHash.h"
#include "static/Sugar.h"

using namespace klee;

ConstraintManager Query::dummyConstraints;

Expr::Hash Query::hash(void) const
{
	QHDefault	qh;
	return qh.hash(*this);
}

void Query::print(std::ostream& os) const
{
	os << "Constraints {\n";
	foreach (it, constraints.begin(), constraints.end()) {
		(*it)->print(os);
		os << std::endl;
	}
	os << "}\n";
	expr->print(os);
	os << std::endl;
}
