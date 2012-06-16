class ConstCounter : public ExprConstVisitor
{
public:
	typedef	std::map<ref<ConstantExpr>, unsigned> constcount_ty;

	ConstCounter(void) : ExprConstVisitor(false) {}
	virtual Action visitExpr(const Expr* expr)
	{
		ref<ConstantExpr>	r_ce;

		if (expr->getKind() != Expr::Constant)
			return Expand;

		r_ce = ref<ConstantExpr>(
			static_cast<ConstantExpr*>(
				const_cast<Expr*>(expr)));
		const_c[r_ce] = const_c[r_ce] + 1;
		return Expand;
	}

	constcount_ty::const_iterator begin(void) const
	{ return const_c.begin(); }

	constcount_ty::const_iterator end(void) const
	{ return const_c.end(); }
private:
	constcount_ty	const_c;
};


