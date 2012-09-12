//===-- SMTParser.h -------------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//


#ifndef SMT_PARSER_H
#define SMT_PARSER_H

#include "expr/Parser.h"

#include <cassert>
#include <iostream>
#include <map>
#include <stack>
#include <string>

namespace klee {
  class ExprBuilder;
  
namespace expr {

class SMTParser : public klee::expr::Parser
{
private:
	void *buf;

	typedef std::map<const std::string, ExprHandle> VarEnv;
	typedef std::map<const std::string, ExprHandle> FVarEnv;
	std::stack<VarEnv> varEnvs;
	std::stack<FVarEnv> fvarEnvs;

	typedef std::map<const std::string, ref<Array> >	arrmap_ty;
	arrmap_ty	arrmap;
public:
	/* For interacting w/ the actual parser, should make this nicer */
	static SMTParser* parserTemp;
	std::string fileName;
	std::istream* is;
	int lineNum;
	bool done;
	bool arraysEnabled;

	std::vector<ExprHandle> assumptions;
	klee::expr::ExprHandle satQuery;

	int bvSize;
	bool queryParsed;

	klee::ExprBuilder *builder;
    
	static SMTParser* Parse(
		std::istream* ifs, ExprBuilder* _builder = NULL)
	{
		SMTParser	*smtp = new SMTParser(ifs, _builder);
		if (smtp->Parse() == false) {
			delete smtp;
			return NULL;
		}
		return smtp;
	}

	static SMTParser* Parse(
		const std::string& filename, ExprBuilder *builder = NULL)
	{
		SMTParser	*smtp = new SMTParser(filename, builder);

		if (smtp->Parse() == false || !smtp->queryParsed) {
			delete smtp;
			return NULL;
		}
		return smtp;
	}
  
	virtual klee::expr::Decl *ParseTopLevelDecl();
  
	virtual void SetMaxErrors(unsigned N) { }
	virtual unsigned GetNumErrors() const { return (queryParsed) ? 0 : 1; }
	virtual ~SMTParser();
  
	static int Error(const std::string& s);
	static int StringToInt(const std::string& s);

	ExprHandle GetConstExpr(std::string val, uint8_t base, klee::Expr::Width w);
  
	void DeclareExpr(std::string name, Expr::Width w);
	void DeclareArray(const std::string& name);
	ExprHandle ParseStore(ref<Expr> arr, ref<Expr> idx, ref<Expr> val);

	ExprHandle CreateAnd(std::vector<ExprHandle>);
	ExprHandle CreateOr(std::vector<ExprHandle>);
	ExprHandle CreateXor(std::vector<ExprHandle>);
  

	void PushVarEnv(void);
	void PopVarEnv(void);
	void AddVar(std::string name, ExprHandle val); // to current var env
	ExprHandle GetVar(std::string name); // from current var env

	void PushFVarEnv(void);
	void PopFVarEnv(void);
	void AddFVar(std::string name, ExprHandle val); // to current fvar env
	ExprHandle GetFVar(std::string name); // from current fvar env

	void setBadRead(void) { bad_read = true; }
protected:
	SMTParser(std::istream* ifs, ExprBuilder* _builder = NULL);
	SMTParser(const std::string& filename, ExprBuilder *builder = NULL);

private:
	bool Parse(void);

	bool parsedTopLevel;
	bool bad_read;
	bool is_parsing;
};

}
}

#endif

