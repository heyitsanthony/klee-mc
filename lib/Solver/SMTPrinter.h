#ifndef SMTPRINTER_H

#include <string>
#include <map>
#include <set>

#include "klee/Expr.h"
#include "klee/util/ExprVisitor.h"

namespace klee {

class SMTPrinter : public ExprVisitor
{
public:
	struct SMTArrays {
		std::map<const Array*, std::string>		a_initial;
		std::map<const Array*, std::string>		a_assumptions;
		std::map<const UpdateNode*, std::string>	a_updates;
	};

	virtual ~SMTPrinter() {}
	static void print(std::ostream& os, const Query& q);
	Action visitExpr(const Expr &e);
  	Action visitExprPost(const Expr &e);

protected:
	std::string expr2str(const ref<Expr>& expr);
private:
	SMTPrinter(std::ostream& in_os, SMTArrays* in_arr) 
	: ExprVisitor(false, true)
	, os(in_os)
	, arr(in_arr)
	{
		use_hashcons = false;
	}

	void printArrayDecls(void) const;
	void printConstant(const ConstantExpr* ce);
	static std::string arr2name(const Array* arr);

	const std::string& getArrayForUpdate(
		const Array* arr, const UpdateNode *un);
	const std::string& getInitialArray(const Array* root);

	std::ostream	&os;
	// allocated/freed by print. 
	// Used to pass array data for nested expr2str calls.
	SMTArrays	*arr;
};

}
#endif
