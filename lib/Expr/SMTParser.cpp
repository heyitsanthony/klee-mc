//===-- SMTParser.cpp -----------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "SMTParser.h"
#include "static/Sugar.h"
#include "klee/ExprBuilder.h"
#include "klee/Solver.h"
#include "klee/Constraints.h"
#include "expr/Parser.h"
#include "smtlib_parser.hpp"

#include <llvm/ADT/StringRef.h>
#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <cassert>
#include <stack>

using namespace std;
using namespace klee;
using namespace klee::expr;

extern void *smtlib_createBuffer(int);
extern void smtlibrestart(FILE *);
extern void smtlib_flushBuffer(void *);
extern void smtlib_deleteBuffer(void *);
extern void smtlib_switchToBuffer(void *);
extern int smtlib_bufSize(void);
extern void smtlib_setInteractive(bool);

SMTParser* SMTParser::parserTemp = NULL;

SMTParser::SMTParser(const std::string& _filename, ExprBuilder* _builder)
: fileName(_filename)
, lineNum(1)
, done(false)
, satQuery(NULL)
, bvSize(0)
, queryParsed(false)
, builder(_builder)
, parsedTopLevel(false)
{
	is = new ifstream(fileName.c_str());

	// Initial empty environments
	varEnvs.push(VarEnv());
	fvarEnvs.push(FVarEnv());
}

SMTParser::SMTParser(std::istream* ifs, ExprBuilder* _builder)
: fileName("<istream>")
, is(ifs)
, lineNum(1)
, done(false)
, satQuery(NULL)
, bvSize(0)
, queryParsed(false)
, builder(_builder)
, parsedTopLevel(false)
{
	// Initial empty environments
	varEnvs.push(VarEnv());
	fvarEnvs.push(FVarEnv());
}

SMTParser::~SMTParser(void)
{
	foreach (it, arrmap.begin(), arrmap.end())
		it->second->decRefIfCared();
	delete is;
	is = NULL;
}

bool SMTParser::Parse(void)
{
	smtlib::parser	p;
	int		rc;

	parserTemp = this;
	lineNum = 1;
	std::cerr << "IT IS PARSE TIME!!!!F=" << fileName <<"\n";
#if 0
	void *buf;
	buf = smtlib_createBuffer(smtlib_bufSize());


	smtlib_switchToBuffer(buf);
	smtlib_flushBuffer(buf);
#endif
	smtlibrestart(NULL);
	smtlib_setInteractive(false);
	rc = p.parse();
#if 0
	smtlib_switchToBuffer(NULL);

	smtlib_deleteBuffer(buf);
#endif

	return (rc == 0);
}

Decl* SMTParser::ParseTopLevelDecl()
{
	if (parsedTopLevel)
		return NULL;

	parsedTopLevel = true;
	return new QueryCommand(
		assumptions,
		builder->Not(satQuery),
		std::vector<ExprHandle>(),
		std::vector<const Array*>());
}

static Solver* getSMTParserSolver(void)
{
	// FIXME: Support choice of solver.
	bool	UseDummySolver = false,
		UseFastCexSolver = true,
		UseSTPQueryPCLog = true;
	Solver	*S, *STP;

	S = UseDummySolver ? createDummySolver() : new STPSolver(true);
	STP = S;

	if (UseSTPQueryPCLog)
		S = createPCLoggingSolver(S, "stp-queries.pc");
	if (UseFastCexSolver)
		S = createFastCexSolver(S);
	S = createCexCachingSolver(S);
	S = createCachingSolver(S);
	S = createIndependentSolver(S);
	if (0)
		S = createValidatingSolver(S, STP);

	return S;
}

bool SMTParser::Solve()
{
	bool result;
	Solver* S  = getSMTParserSolver();
	Decl *D = this->ParseTopLevelDecl();
	QueryCommand *QC = dyn_cast<QueryCommand>(D);

	if (QC == NULL)
		goto error;

	//llvm::cout << "Query " << ":\t";

	assert("FIXME: Support counterexample query commands!");
	if (!QC->Values.empty() || !QC->Objects.empty())
		goto error;

	if (!S->mustBeTrue(
		Query(ConstraintManager(QC->Constraints), QC->Query), result))
		goto error;

	//std::cout << (result ? "VALID" : "INVALID") << "\n";
	return result;

error:
	std::cout << "FAIL";
	exit(1);

	return false;
}

extern char* smtlibtext;

// XXX: give more info
int SMTParser::Error(const string& msg)
{
	std::cerr
		<< SMTParser::parserTemp->fileName << ":"
		<< SMTParser::parserTemp->lineNum << ": "
		<< msg << "\n"
		<< smtlibtext << '\n';
	exit(1);
	return 0;
}


int SMTParser::StringToInt(const std::string& s)
{
	std::stringstream str(s);
	int x;
	str >> x;
	assert(str);
	return x;
}

#define CREATE_MULTI(x)	\
ExprHandle SMTParser::Create##x(std::vector<ExprHandle> kids)	\
{	\
	unsigned n_kids = kids.size();	\
\
	assert(n_kids);	\
	if (n_kids == 1) return kids[0];	\
\
	ExprHandle r = builder->x(kids[n_kids-2], kids[n_kids-1]);	\
	for (int i=n_kids-3; i>=0; i--)	\
		r = builder->x(kids[i], r);	\
\
	std::cerr << "CREATED "#x":\n" << r << '\n';	\
\
	return r;	\
}

CREATE_MULTI(And)
CREATE_MULTI(Or)
CREATE_MULTI(Xor)

void SMTParser::DeclareExpr(std::string name, Expr::Width w)
{
	// for now, only allow variables which are multiples of 8
	if (w % 8 != 0) {
		cout	<< "BitVec not multiple of 8 ("
			<< w
			<< ").  Need to update code.\n";
		exit(1);
	}

	Array *arr = Array::create(name, w / 8);

	ref<Expr> *kids = new ref<Expr>[w/8];
	for (unsigned i=0; i < w/8; i++)
		kids[i] = builder->Read(
			UpdateList(arr, NULL), builder->Constant(i, 32));

	 // XXX: move to builder?
	ref<Expr> var = ConcatExpr::createN(w/8, kids);
	delete [] kids;

	AddVar(name, var);
}

#define DEFAULT_ARR_SZ	4096
void SMTParser::DeclareArray(const std::string& name)
{
	Array	*arr;

	if (arrmap.count(name))
		return;

	arr = Array::get(name);
	if (arr == NULL)
		arr = Array::create(name, DEFAULT_ARR_SZ);
	arr->incRefIfCared();
	arrmap[name] = arr;
	AddVar(
		name,
		builder->NotOptimized(
			builder->Read(
				UpdateList(arr, NULL),
				builder->Constant(0, 32))));
}

ExprHandle SMTParser::ParseStore(ref<Expr> arr, ref<Expr> idx, ref<Expr> val)
{
	const NotOptimizedExpr	*no;
	const ReadExpr		*re;

	no = dyn_cast<NotOptimizedExpr>(arr);
	assert (no != NULL);

	re = dyn_cast<ReadExpr>(no->getKid(0));
	assert (re != NULL);

	UpdateList	up(re->updates);
	up.extend(idx, val);

	return builder->NotOptimized(
		builder->Read(
			up,
			builder->Constant(0, 32)));
}

ExprHandle SMTParser::GetConstExpr(
	std::string val, uint8_t base, klee::Expr::Width w)
{
	assert (base == 2 || base == 10 || base == 16);

	llvm::APInt ap(w, llvm::StringRef(val), base);
	return klee::ConstantExpr::alloc(ap);
}


void SMTParser::PushVarEnv() { varEnvs.push(VarEnv(varEnvs.top())); }
void SMTParser::PopVarEnv() { varEnvs.pop(); }

void SMTParser::AddVar(std::string name, ExprHandle val)
{ varEnvs.top()[name] = val; }

ExprHandle SMTParser::GetVar(std::string name)
{
	VarEnv top = varEnvs.top();
	if (top.find(name) == top.end()) {
		std::cerr << "Cannot find variable ?" << name << "\n";
		Error("missing variable name");
		assert (0 == 1 && "Expected variable name");
		exit(1);
	}
	return top[name];
}


void SMTParser::PushFVarEnv() { fvarEnvs.push(FVarEnv(fvarEnvs.top())); }
void SMTParser::PopFVarEnv(void) { fvarEnvs.pop(); }

void SMTParser::AddFVar(std::string name, ExprHandle val)
{ fvarEnvs.top()[name] = val; }

ExprHandle SMTParser::GetFVar(std::string name)
{
	FVarEnv top = fvarEnvs.top();
	if (top.find(name) == top.end()) {
		std::cerr << "Cannot find fvar $" << name << "\n";
		exit(1);
	}
	return top[name];
}
