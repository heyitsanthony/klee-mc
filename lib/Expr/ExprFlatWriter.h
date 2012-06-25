#include "klee/util/ExprVisitor.h"
#include "klee/util/ExprTag.h"
#include "Pattern.h"

namespace klee
{
typedef std::vector<unsigned>	labellist_ty;
class ExprFlatWriter : public ExprConstVisitor
{
public:
	ExprFlatWriter(std::ostream* _os = NULL) : os(_os) {}
	virtual ~ExprFlatWriter(void) {}
	void setOS(std::ostream* _os) { os = _os; }
	void setLabelMap(const labelmap_ty& lm);
	void getLabelMap(labelmap_ty& lm) const;
	const labellist_ty& getLabelList(void) const { return label_list; }
protected:
	virtual Action visitExpr(const Expr* expr);
	std::ostream		*os;
	ExprHashMap<unsigned>	labels;
	labellist_ty		label_list;
};

class EFWTagged : public ExprVisitorTags<ExprFlatWriter>
{
public:
	EFWTagged(const exprtags_ty& _tags_pre, bool _const_repl=true)
	: ExprVisitorTags<ExprFlatWriter>(_tags_pre, dummy)
	, const_repl(_const_repl) {}

	virtual void apply(const ref<Expr>& e);
	virtual ~EFWTagged() {}

	unsigned getMaskedBase(void) const { return masked_list_base; }
	unsigned getMaskedStartLID(void) const { return masked_start_lid; }
	unsigned getMaskedNew(void) const { return masked_new; }
	unsigned getMaskedReads(void) const { return masked_reads; }
protected:
	virtual ExprFlatWriter::Action preTagVisit(const Expr* e);
	virtual void postTagVisit(const Expr* e) {}
private:
	void visitVar(const Expr* _e);

	bool			const_repl;
	unsigned		tag_c;
	unsigned		masked_list_base;
	unsigned		masked_start_lid;
	unsigned		masked_new;
	unsigned		masked_reads;
	static exprtags_ty	dummy;
};
}
