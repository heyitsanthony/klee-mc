#ifndef SMTPRINTER_H

#include <string>
#include <map>
#include <set>

#include "klee/Expr.h"
#include "klee/util/ExprVisitor.h"

namespace klee {

typedef std::pair<const Array*, const UpdateNode*> update_pair;

class SMTPrinter : public ExprVisitor
{
public:
	struct SMTArrays {
		const std::string& getInitialArray(const Array* a);
		std::map<const Array*, std::string>		a_initial;
		std::map<const Array*, std::string>		a_const_decls;
		std::map<update_pair, ref<Expr> >		a_updates;
	};

	virtual ~SMTPrinter() {}
	static void print(std::ostream& os, const Query& q);
	Action visitExpr(const Expr &e);
  	Action visitExprPost(const Expr &e);

protected:
	void expr2os(const ref<Expr>& expr, std::ostream& os) const;
private:
	SMTPrinter(std::ostream& in_os, SMTArrays* in_arr) 
	: ExprVisitor(false, true)
	, os(in_os)
	, arr(in_arr)
	{
		// enabling hashcons means we wouldn't print
		// expressions we have already seen!
		use_hashcons = false;
	}

	void printConstraint(
		const ref<Expr>& e,
		const std::set<update_pair>& updates,
		const char* key = ":assumption",
		const char* val = "bv1[1]");

	void printArrayDecls(void) const;
	void printConstant(const ConstantExpr* ce);

	bool printOptMul(const MulExpr* me) const;
	void printOptMul64(const ref<Expr>& rhs, uint64_t lhs) const;

	void writeArrayForUpdate(
		std::ostream& os,
		const ReadExpr* re);

	void writeExpandedArrayForUpdate(
		std::ostream& os,
		const Array* root, const UpdateNode *un);

	const std::string& getInitialArray(const Array* root);

	std::ostream	&os;
	// allocated/freed by print. 
	// Used to pass array data for nested expr2str calls.
	SMTArrays	*arr;
};

}
#endif
