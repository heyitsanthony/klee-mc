%{
/*****************************************************************************/
/*!
 * \file smtlib.y
 *
 * Author: Clark Barrett
 *
 * Created: Apr 30 2005
 * Hacked to death by Anthony: Feb 7, 2012
 * <hr>
 *
 * License to use, copy, modify, sell and/or distribute this software
 * and its documentation for any purpose is hereby granted without
 * royalty, subject to the terms and conditions defined in the \ref
 * LICENSE file provided with this distribution.
 *
 * <hr>
 *
 */
/*****************************************************************************/
/*
   This file contains the bison code for the parser that reads in CVC
   commands in SMT-LIB language.
*/

#include "SMTParser.h"
#include "klee/Expr.h"
#include "klee/ExprBuilder.h"

#include <sstream>

using namespace klee;
using namespace klee::expr;

// Define shortcuts for various things
#define PARSER SMTParser::parserTemp
#define BUILDER SMTParser::parserTemp->builder
#define DONE SMTParser::parserTemp->done
#define ASSUMPTIONS SMTParser::parserTemp->assumptions
#define QUERY SMTParser::parserTemp->satQuery

#define ARRAYSENABLED (SMTParser::parserTemp->arraysEnabled)
#define BVSIZE (SMTParser::parserTemp->bvSize)
#define QUERYPARSED SMTParser::parserTemp->queryParsed

// Suppress the bogus warning suppression in bison (it generates
// compile error)
#undef __GNUC_MINOR__

/* stuff that lives in smtlib.lex */
int smtliberror0(const char *s, void *sem_type, void *loc) 
{ return SMTParser::parserTemp->Error(s); }
int smtliberror(const char *s) 
{ return smtliberror0(s, 0, 0); }

#define YYMAXDEPTH 10485760


#include "smtlib_parser.hpp"
//extern int smtliblex \
//               (YYSTYPE * yylval_param,YYLTYPE * yylloc_param );

void smtlib::parser::error (const location_type& loc, const std::string& msg)
{ smtliberror(msg.c_str()); }

extern int smtliblex(struct smtlib::parser::semantic_type*, smtlib::location*);

%}

%locations

%union {
#define SMT_STR_LEN	128
#define SMT_CPY_TXT(x)	yylval_param->str = x;
	std::string				str;
	klee::expr::ExprHandle			node;
	std::vector<klee::expr::ExprHandle>	*vec;
};


%start cmd

/* strings are for better error messages.
   "_TOK" is so macros don't conflict with kind names */
/*
%type <vec> bench_attributes sort_symbs fun_symb_decls pred_symb_decls
%type <vec> an_formulas quant_vars an_terms fun_symb patterns
%type <node> pattern
%type <node> benchmark bench_name bench_attribute
%type <node> status fun_symb_decl fun_sig pred_symb_decl pred_sig
%type <node> an_formula quant_var an_atom prop_atom
%type <node> an_term basic_term sort_symb pred_symb
%type <node> var fvar
%type <str> logic_name quant_symb connective user_value attribute annotation
%type <strvec> annotations
%type <pat_ann> patterns_annotations
*/

%type <node> an_formula an_logical_formula an_atom prop_atom
%type <vec> an_formulas
%type <node> an_term basic_term constant
%type <node> an_fun an_arithmetic_fun an_bitwise_fun
%type <node> an_pred
%type <str> logic_name status attribute user_value annotation annotations
%type <str> var fvar symb

%token <str> NUMERAL_TOK
%token <str> SYM_TOK
%token <str> STRING_TOK
%token <str> AR_SYMB
%token <str> USER_VAL_TOK

%token <str> BV_TOK
%token <str> BVBIN_TOK
%token <str> BVHEX_TOK

%token BITVEC_TOK
%token ARRAY_TOK

%token TRUE_TOK FALSE_TOK
%token ITE_TOK
%token NOT_TOK IMPLIES_TOK IF_THEN_ELSE_TOK AND_TOK OR_TOK XOR_TOK IFF_TOK
%token EXISTS_TOK
%token FORALL_TOK
%token LET_TOK FLET_TOK
%token STORE_TOK
%token NOTES_TOK
%token CVC_COMMAND_TOK
%token SORTS_TOK
%token FUNS_TOK
%token PREDS_TOK
%token EXTENSIONS_TOK
%token DEFINITION_TOK
%token AXIOMS_TOK
%token LOGIC_TOK
%token COLON_TOK
%token	LBRACKET_TOK RBRACKET_TOK
	LCURBRACK_TOK RCURBRACK_TOK LPAREN_TOK RPAREN_TOK
%token SAT_TOK
%token UNSAT_TOK
%token UNKNOWN_TOK
%token ASSUMPTION_TOK
%token FORMULA_TOK
%token STATUS_TOK
%token BENCHMARK_TOK
%token EXTRASORTS_TOK
%token EXTRAFUNS_TOK
%token EXTRAPREDS_TOK
%token LANGUAGE_TOK
%token DOLLAR_TOK
%token QUESTION_TOK
%token DISTINCT_TOK
%token SEMICOLON_TOK
%token EOF_TOK
%token PAT_TOK


%token BIT0_TOK BIT1_TOK

%token BVCONCAT_TOK BVEXTRACT_TOK BVSELECT_TOK

%token	BVNOT_TOK BVAND_TOK BVOR_TOK BVNEG_TOK
	BVNAND_TOK BVNOR_TOK BVXOR_TOK BVXNOR_TOK

%token EQ_TOK
%token BVCOMP_TOK
%token	BVULT_TOK BVULE_TOK BVUGT_TOK BVUGE_TOK
	BVSLT_TOK BVSLE_TOK BVSGT_TOK BVSGE_TOK

%token	BVADD_TOK BVSUB_TOK
	BVMUL_TOK BVUDIV_TOK BVSDIV_TOK
	BVUREM_TOK BVSREM_TOK BVSMOD_TOK
	BVSHL_TOK BVLSHR_TOK BVASHR_TOK

%token REPEAT_TOK
%token ZEXT_TOK SEXT_TOK
%token ROL_TOK ROR_TOK

%%

cmd:
	benchmark { YYACCEPT; }
	;

benchmark:
	LPAREN_TOK BENCHMARK_TOK bench_name bench_attributes RPAREN_TOK { }
	| EOF_TOK { DONE = true; }
	;

bench_name:
	SYM_TOK { }
	;

bench_attributes:
	bench_attribute { }
	| bench_attributes bench_attribute { }
	;

bench_attribute:
	COLON_TOK ASSUMPTION_TOK an_formula { ASSUMPTIONS.push_back($3); }
	| COLON_TOK FORMULA_TOK an_formula
	{
		QUERYPARSED = true;
		QUERY = $3;
	}
	| COLON_TOK STATUS_TOK status { }
	| COLON_TOK LOGIC_TOK logic_name
	{
		if ($3 !=  "QF_BV" && $3 != "QF_AUFBV" && $3 != "QF_UFBV")
		{
			std::cerr << "ERROR: Logic " << $3 << " not supported.";
			exit(1);
		}

		if ($3 == "QF_AUFBV")
			ARRAYSENABLED = true;
	}
	/*
	| COLON_TOK EXTRASORTS_TOK LPAREN_TOK sort_symbs RPAREN_TOK
	{
	// XXX?
	}
	*/

	| COLON_TOK EXTRAFUNS_TOK
		LPAREN_TOK LPAREN_TOK
			SYM_TOK
			BITVEC_TOK LBRACKET_TOK NUMERAL_TOK RBRACKET_TOK
		RPAREN_TOK RPAREN_TOK
	{ PARSER->DeclareExpr($5, atoi($8.c_str())); }

	| COLON_TOK EXTRAFUNS_TOK
	LPAREN_TOK LPAREN_TOK
		SYM_TOK
		ARRAY_TOK LBRACKET_TOK NUMERAL_TOK COLON_TOK NUMERAL_TOK RBRACKET_TOK
	RPAREN_TOK RPAREN_TOK
	{
		/* SymName Array[AddrWidth : DataWidth ] */
		assert (atoi($8.c_str()) == 32);
		assert (atoi($10.c_str()) == 8);
		PARSER->DeclareArray($5);
	}

	/*
	| COLON_TOK EXTRAPREDS_TOK LPAREN_TOK pred_symb_decls RPAREN_TOK
	{
	//$$ = new CVC3::Expr(VC->listExpr("_SEQ", *$4));
	//delete $4;
	}
	*/
	| COLON_TOK NOTES_TOK STRING_TOK {  }
	| COLON_TOK CVC_COMMAND_TOK STRING_TOK {  }
	| annotation {  }
	;


logic_name :
	SYM_TOK LBRACKET_TOK NUMERAL_TOK RBRACKET_TOK
	{
		BVSIZE = atoi($3.c_str());
		$$ = $1;
	}
	| SYM_TOK { $$ = $1; }
	;

status:
	SAT_TOK { }
	| UNSAT_TOK { }
	| UNKNOWN_TOK { }
	;


an_formulas:
	an_formula
	{
		$$ = new std::vector<klee::expr::ExprHandle>;
		$$->push_back($1);
	}
	|
	an_formulas an_formula
	{
		$1->push_back($2);
		$$ = $1;
	}
	;


an_logical_formula:
	LPAREN_TOK NOT_TOK an_formula annotations { $$ = BUILDER->Not($3); }
	| LPAREN_TOK IMPLIES_TOK an_formula an_formula annotations
	{ $$ = Expr::createImplies($3, $4); } // XXX: move to builder?
	| LPAREN_TOK IF_THEN_ELSE_TOK an_formula an_formula an_formula annotations
	{ $$ = BUILDER->Select($3, $4, $5); }
	| LPAREN_TOK AND_TOK an_formulas annotations
	{ $$ = PARSER->CreateAnd(*$3); }
	| LPAREN_TOK OR_TOK an_formulas annotations
	{ $$ = PARSER->CreateOr(*$3); }
	| LPAREN_TOK XOR_TOK an_formulas annotations
	{ $$ = PARSER->CreateXor(*$3); }
	| LPAREN_TOK IFF_TOK an_formula an_formula annotations
	{ $$ = BUILDER->Eq($3, $4); }
	;


an_formula:
	an_atom { $$ = $1; }
	| an_logical_formula { $$ = $1; }
	| LPAREN_TOK LET_TOK LPAREN_TOK var an_term RPAREN_TOK
	{
		PARSER->PushVarEnv();
		PARSER->AddVar($4, $5);
	} an_formula annotations
	{
		$$ = $8;
		PARSER->PopVarEnv();
	}
	| LPAREN_TOK FLET_TOK LPAREN_TOK fvar an_formula RPAREN_TOK
	{
		PARSER->PushFVarEnv();
		PARSER->AddFVar($4, $5);
	} an_formula annotations
	{
		$$ = $8;
		PARSER->PopFVarEnv();
	}
	;



an_atom:
	prop_atom { $$ = $1; }
	| LPAREN_TOK prop_atom annotations { $$ = $2; }
	| an_pred { $$ = $1; }

/*
  | LPAREN_TOK DISTINCT_TOK an_terms annotations
    {
      $$ = new CVC3::Expr(VC->listExpr("_DISTINCT", *$3));

//       std::vector<CVC3::Expr> tmp;
//       tmp.push_back(*$2);
//       tmp.insert(tmp.end(), $3->begin(), $3->end());
//       $$ = new CVC3::Expr(VC->listExpr(tmp));
//       for (unsigned i = 0; i < (*$3).size(); ++i) {
//         tmp.push_back(($3)[i])
// 	for (unsigned j = i+1; j < (*$3).size(); ++j) {
// 	  tmp.push_back(VC->listExpr("_NEQ", (*$3)[i], (*$3)[j]));
// 	}
//       }
//       $$ = new CVC3::Expr(VC->listExpr("_AND", tmp));
      delete $3;
      delete $4;
    }
  | LPAREN_TOK DISTINCT_TOK an_terms RPAREN_TOK
    {
      $$ = new CVC3::Expr(VC->listExpr("_DISTINCT", *$3));
//       std::vector<CVC3::Expr> tmp;
//       for (unsigned i = 0; i < (*$3).size(); ++i) {
// 	for (unsigned j = i+1; j < (*$3).size(); ++j) {
// 	  tmp.push_back(VC->listExpr("_NEQ", (*$3)[i], (*$3)[j]));
// 	}
//       }
//       $$ = new CVC3::Expr(VC->listExpr("_AND", tmp));
      delete $3;
    }
*/
	;


prop_atom:
	TRUE_TOK { $$ = BUILDER->Constant(1, 1); }
	| FALSE_TOK { $$ = BUILDER->Constant(0, 1);; }
	| fvar { $$ = PARSER->GetFVar($1);}
	;


an_pred:
	LPAREN_TOK EQ_TOK an_term an_term annotations
	{ $$ = BUILDER->Eq($3, $4); }
	| LPAREN_TOK BVULT_TOK an_term an_term annotations
	{ $$ = BUILDER->Ult($3, $4); }
	| LPAREN_TOK BVULE_TOK an_term an_term annotations
	{ $$ = BUILDER->Ule($3, $4); }
	| LPAREN_TOK BVUGT_TOK an_term an_term annotations
	{ $$ = BUILDER->Ugt($3, $4); }
	| LPAREN_TOK BVUGE_TOK an_term an_term annotations
	{ $$ = BUILDER->Uge($3, $4); }
	| LPAREN_TOK BVSLT_TOK an_term an_term annotations
	{ $$ = BUILDER->Slt($3, $4); }
	| LPAREN_TOK BVSLE_TOK an_term an_term annotations
	{ $$ = BUILDER->Sle($3, $4); }
	| LPAREN_TOK BVSGT_TOK an_term an_term annotations
	{ $$ = BUILDER->Sgt($3, $4); }
	| LPAREN_TOK BVSGE_TOK an_term an_term annotations
	{ $$ = BUILDER->Sge($3, $4); }
	;

an_arithmetic_fun:
	LPAREN_TOK BVNEG_TOK an_term annotations
	{
	smtliberror("bvneg not supported yet");
	$$ = NULL; // TODO
	}
	| LPAREN_TOK BVADD_TOK an_term an_term annotations
	{ $$ = BUILDER->Add($3, $4); }
	| LPAREN_TOK BVSUB_TOK an_term an_term annotations
	{ $$ = BUILDER->Sub($3, $4); }
	| LPAREN_TOK BVMUL_TOK an_term an_term annotations
	{ $$ = BUILDER->Mul($3, $4); }
	| LPAREN_TOK BVUDIV_TOK an_term an_term annotations
	{ $$ = BUILDER->UDiv($3, $4); }
	| LPAREN_TOK BVUREM_TOK an_term an_term annotations
	{ $$ = BUILDER->URem($3, $4); }
	| LPAREN_TOK BVSDIV_TOK an_term an_term annotations
	{ $$ = BUILDER->SDiv($3, $4); }
	| LPAREN_TOK BVSREM_TOK an_term an_term annotations
	{ $$ = BUILDER->SRem($3, $4); }
	| LPAREN_TOK BVSMOD_TOK an_term an_term annotations
	{
		smtliberror("bvsmod not supported yet");
		$$ = NULL; // TODO
	}
	;


an_bitwise_fun:
	LPAREN_TOK BVNOT_TOK an_term annotations
	{ $$ = BUILDER->Not($3); }
	| LPAREN_TOK BVAND_TOK an_term an_term annotations
	{ $$ = BUILDER->And($3, $4); }
	| LPAREN_TOK BVOR_TOK an_term an_term annotations
	{ $$ = BUILDER->Or($3, $4); }
	| LPAREN_TOK BVXOR_TOK an_term an_term annotations
	{ $$ = BUILDER->Xor($3, $4); }
	| LPAREN_TOK BVXNOR_TOK an_term an_term annotations
	{
		smtliberror("bvxnor not supported yet");
		$$ = NULL;  // TODO
	}
	| LPAREN_TOK BVSHL_TOK an_term an_term annotations
	{ $$ = BUILDER->Shl($3, $4); }
	| LPAREN_TOK BVLSHR_TOK an_term an_term annotations
	{ $$ = BUILDER->LShr($3, $4); }
	| LPAREN_TOK BVASHR_TOK an_term an_term annotations
	{ $$ = BUILDER->AShr($3, $4); }
	| LPAREN_TOK ROL_TOK LBRACKET_TOK NUMERAL_TOK RBRACKET_TOK an_term annotations
	{
		smtliberror("rotate_left not supported yet");
		$$ = NULL; // TODO
	}
	| LPAREN_TOK ROR_TOK LBRACKET_TOK NUMERAL_TOK RBRACKET_TOK an_term annotations
	{
		smtliberror("rotate_right not supported yet");
		$$ = NULL; // TODO
	}
	| LPAREN_TOK ZEXT_TOK LBRACKET_TOK NUMERAL_TOK RBRACKET_TOK an_term annotations
	{ $$ = BUILDER->ZExt($6, $6->getWidth() + atoi($4.c_str())); }
	| LPAREN_TOK SEXT_TOK LBRACKET_TOK NUMERAL_TOK RBRACKET_TOK an_term annotations
	{ $$ = BUILDER->SExt($6, $6->getWidth() + atoi($4.c_str())); }
	| LPAREN_TOK BVSELECT_TOK an_term an_term annotations
	{
		ref<Expr>	arr_expr($3);
		ref<Expr>	idx_expr($4);
		const NotOptimizedExpr	*no;
		const ReadExpr		*re;
		bool			is_oob;

		no = dyn_cast<NotOptimizedExpr>(arr_expr);
		assert (no);

		re = dyn_cast<ReadExpr>(no->getKid(0));
		if (re == NULL) {
			SMTParser::parserTemp->setBadRead();
			$$ = BUILDER->Constant(0, 8);
			YYACCEPT;
		}

		assert (re);

		is_oob = false;
		if (const ConstantExpr* idx_ce = dyn_cast<ConstantExpr>(idx_expr)) {
			if (re->getArray()->getSize() <= idx_ce->getZExtValue())
				is_oob = true;
		}

		if (is_oob) {
			SMTParser::parserTemp->setBadRead();
			$$ = BUILDER->Constant(0, 8);
			YYACCEPT;
		} else {
			$$ = BUILDER->Read(re->updates, idx_expr);
		}
	}
	| LPAREN_TOK BVCONCAT_TOK an_term an_term annotations
	{ $$ = BUILDER->Concat($3, $4); }
	| LPAREN_TOK REPEAT_TOK LBRACKET_TOK NUMERAL_TOK RBRACKET_TOK an_term annotations
	{
		smtliberror("repeat not supported yet");
		$$ = NULL; // TODO
	}
	| LPAREN_TOK
		BVEXTRACT_TOK
			LBRACKET_TOK NUMERAL_TOK COLON_TOK NUMERAL_TOK RBRACKET_TOK
		an_term annotations
	{
		int off = atoi($6.c_str());
		$$ = BUILDER->Extract($8, off, atoi($4.c_str()) - off + 1);
	}
	;


an_fun:
	an_logical_formula { $$ = $1; }
	| an_pred { $$ = $1; }
	| LPAREN_TOK BVCOMP_TOK an_term an_term annotations
	{ $$ = BUILDER->Eq($3, $4); }
	| an_arithmetic_fun { $$ = $1; }
	| an_bitwise_fun { $$ = $1; }
	/*
	      else if (ARRAYSENABLED && *$1 == "select") {
		$$->push_back(VC->idExpr("_READ"));
	      }
	      else if (ARRAYSENABLED && *$1 == "store") {
		$$->push_back(VC->idExpr("_WRITE"));
	      }
	*/
	;

constant:
	BIT0_TOK { $$ = PARSER->GetConstExpr("0", 2, 1); }
	| BIT1_TOK { $$ = PARSER->GetConstExpr("1", 2, 1); }
	| BVBIN_TOK
	{ $$ = PARSER->GetConstExpr($1.substr(5), 2, $1.length()-5); }
	| BVHEX_TOK
	{ $$ = PARSER->GetConstExpr($1.substr(5), 16, ($1.length()-5)*4); }
	| BV_TOK LBRACKET_TOK NUMERAL_TOK RBRACKET_TOK
	{ $$ = PARSER->GetConstExpr($1.substr(2), 10, atoi($3.c_str())); }
	;


an_term:
	basic_term { $$ = $1; }
	| LPAREN_TOK STORE_TOK an_term an_term an_term annotations
	{
		/* XXX we don't want an_term for the first param
		 * (it should be an array), but I'd need to tear up the
		 * grammar to get it to accept it precisely */
		$$ = PARSER->ParseStore($3, $4, $5);
	}
	| LPAREN_TOK basic_term annotations
	{ $$ = $2; }
	| an_fun { $$ = $1; }
	| LPAREN_TOK ITE_TOK an_formula an_term an_term annotations
	{ $$ = BUILDER->Select($3, $4, $5); }
	;

basic_term:
	constant { $$ = $1; }
	| var { $$ = PARSER->GetVar($1); }
	| symb { $$ = PARSER->GetVar($1); }
	;



annotations:
	RPAREN_TOK { }
	| annotation annotations { }
	;

annotation:
	attribute { }
	| attribute user_value { }
	;

user_value:	USER_VAL_TOK { } ;
attribute:	COLON_TOK SYM_TOK { $$ = $2; } ;
var:		QUESTION_TOK SYM_TOK { $$ = $2; } ;
fvar:		DOLLAR_TOK SYM_TOK { $$ = $2; } ;
symb:		SYM_TOK { $$ = $1; } ;

%%
