%{
/*****************************************************************************/
/*!
 * \file smtlib.lex
 *
 * Author: Clark Barrett
 *
 * Created: 2005
 *
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

#include <iostream>
#include "SMTParser.h"
#include "smtlib_parser.hpp"

using namespace klee;
using namespace klee::expr;

#define YYSTYPE	smtlib::parser::semantic_type
#define YYLTYPE smtlib::location

extern int smtlib_inputLine;
extern char *smtlibtext;

extern int smtliberror (const char *msg);

static int smtlibinput(std::istream& is, char* buf, int size)
{
	int res;

	if (!is)
		return YY_NULL;

	// Set the terminator char to 0
	buf[0] = '\0';
	is.getline(buf, size-1, 0);

	// If failbit is set, but eof is not, it means the line simply
	// didn't fit; so we clear the state and keep on reading.
	bool partialStr = is.fail() && !is.eof();
	if(partialStr)
		is.clear();

	res = strnlen(buf, size);
	if(res == size)
		smtliberror("Lexer bug: overfilled the buffer");

	return res;
}

// Redefine the input buffer function to read from an istream
#define YY_INPUT(buf,result,max_size) \
  result = smtlibinput(*SMTParser::parserTemp->is, buf, max_size);

int smtlib_bufSize() { return YY_BUF_SIZE; }
YY_BUFFER_STATE smtlib_buf_state() { return YY_CURRENT_BUFFER; }

/* some wrappers for methods that need to refer to a struct.
   These are used by SMTParser. */
void *smtlib_createBuffer(int sz)
{ return smtlib_create_buffer(NULL, sz); }
void smtlib_deleteBuffer(void *buf_state)
{ smtlib_delete_buffer((struct yy_buffer_state *)buf_state); }
void smtlib_flushBuffer(void *buf_state)
{ smtlib_flush_buffer((struct yy_buffer_state *)buf_state); }
void smtlib_switchToBuffer(void *buf_state)
{ smtlib_switch_to_buffer((struct yy_buffer_state *)buf_state); }
void *smtlib_bufState()
{ return (void *)smtlib_buf_state(); }
void smtlib_setInteractive(bool is_interactive)
{ yy_set_interactive(is_interactive); }

// File-static (local to this file) variables and functions
static std::string _string_lit;

 static char escapeChar(char c) {
   switch(c) {
   case 'n': return '\n';
   case 't': return '\t';
   default: return c;
   }
 }

// for now, we don't have subranges.
//
// ".."		{ return DOTDOT_TOK; }
/*OPCHAR	(['!#?\_$&\|\\@])*/

%}

%option bison-bridge bison-locations

%option noyywrap
%option nounput
%option noreject
%option noyymore
%option yylineno

%x	COMMENT
%x	STRING_LITERAL
%x      USER_VALUE
%s      PAT_MODE

LETTER	([a-zA-Z])
DIGIT	([0-9])
OPCHAR	(['\.\_])
IDCHAR  ({LETTER}|{DIGIT}|{OPCHAR})
%%

[\n]            { SMTParser::parserTemp->lineNum++; }
[ \t\r\f]	{ /* skip whitespace */ }

{DIGIT}+	{	SMT_CPY_TXT(smtlibtext);
			return smtlib::parser::token::NUMERAL_TOK; }

";"		{ BEGIN COMMENT; }
<COMMENT>"\n"	{ BEGIN INITIAL; SMTParser::parserTemp->lineNum++; }
<COMMENT>.	{ /* stay in comment mode */ }

<INITIAL>"\""		{ BEGIN STRING_LITERAL;
                          _string_lit.erase(_string_lit.begin(),
                                            _string_lit.end()); }
<STRING_LITERAL>"\\".	{ /* escape characters (like \n or \") */
                          _string_lit.insert(_string_lit.end(),
                                             escapeChar(smtlibtext[1])); }
<STRING_LITERAL>"\""	{ BEGIN INITIAL; /* return to normal mode */
			  SMT_CPY_TXT(_string_lit.c_str());
                          return smtlib::parser::token::STRING_TOK; }
<STRING_LITERAL>.	{ _string_lit.insert(_string_lit.end(),*smtlibtext); }

<INITIAL>":pat"		{ BEGIN PAT_MODE; return smtlib::parser::token::PAT_TOK;}
<PAT_MODE>"}"	        { BEGIN INITIAL; return smtlib::parser::token::RCURBRACK_TOK; }
<INITIAL>"{"		{ BEGIN USER_VALUE;
                          _string_lit.erase(_string_lit.begin(),
                                            _string_lit.end()); }
<USER_VALUE>"\\"[{}] { /* escape characters */
                          _string_lit.insert(_string_lit.end(),smtlibtext[1]); }

<USER_VALUE>"}"	        {BEGIN INITIAL; /* return to normal mode */
			SMT_CPY_TXT(_string_lit.c_str());
                        return smtlib::parser::token::USER_VAL_TOK; }

<USER_VALUE>"\n"        { _string_lit.insert(_string_lit.end(),'\n');
                          SMTParser::parserTemp->lineNum++; }
<USER_VALUE>.	        { _string_lit.insert(_string_lit.end(),*smtlibtext); }

"BitVec"        { return smtlib::parser::token::BITVEC_TOK; }
"Array"		{ return smtlib::parser::token::ARRAY_TOK; }

"true"          { return smtlib::parser::token::TRUE_TOK; }
"false"         { return smtlib::parser::token::FALSE_TOK; }
"ite"           { return smtlib::parser::token::ITE_TOK; }
"not"           { return smtlib::parser::token::NOT_TOK; }
"implies"       { return smtlib::parser::token::IMPLIES_TOK; }
"if_then_else"  { return smtlib::parser::token::IF_THEN_ELSE_TOK; }
"and"           { return smtlib::parser::token::AND_TOK; }
"or"            { return smtlib::parser::token::OR_TOK; }
"xor"           { return smtlib::parser::token::XOR_TOK; }
"iff"           { return smtlib::parser::token::IFF_TOK; }
"exists"        { return smtlib::parser::token::EXISTS_TOK; }
"forall"        { return smtlib::parser::token::FORALL_TOK; }
"store"		{ return smtlib::parser::token::STORE_TOK; }
"let"           { return smtlib::parser::token::LET_TOK; }
"flet"          { return smtlib::parser::token::FLET_TOK; }
"notes"         { return smtlib::parser::token::NOTES_TOK; }
"cvc_command"   { return smtlib::parser::token::CVC_COMMAND_TOK; }
"sorts"         { return smtlib::parser::token::SORTS_TOK; }
"funs"          { return smtlib::parser::token::FUNS_TOK; }
"preds"         { return smtlib::parser::token::PREDS_TOK; }
"extensions"    { return smtlib::parser::token::EXTENSIONS_TOK; }
"definition"    { return smtlib::parser::token::DEFINITION_TOK; }
"axioms"        { return smtlib::parser::token::AXIOMS_TOK; }
"logic"         { return smtlib::parser::token::LOGIC_TOK; }
"sat"           { return smtlib::parser::token::SAT_TOK; }
"unsat"         { return smtlib::parser::token::UNSAT_TOK; }
"unknown"       { return smtlib::parser::token::UNKNOWN_TOK; }
"assumption"    { return smtlib::parser::token::ASSUMPTION_TOK; }
"formula"       { return smtlib::parser::token::FORMULA_TOK; }
"status"        { return smtlib::parser::token::STATUS_TOK; }
"benchmark"     { return smtlib::parser::token::BENCHMARK_TOK; }
"extrasorts"    { return smtlib::parser::token::EXTRASORTS_TOK; }
"extrafuns"     { return smtlib::parser::token::EXTRAFUNS_TOK; }
"extrapreds"    { return smtlib::parser::token::EXTRAPREDS_TOK; }
"language"      { return smtlib::parser::token::LANGUAGE_TOK; }
"distinct"      { return smtlib::parser::token::DISTINCT_TOK; }
":pattern"      { return smtlib::parser::token::PAT_TOK; }
":"             { return smtlib::parser::token::COLON_TOK; }
"\["            { return smtlib::parser::token::LBRACKET_TOK; }
"\]"            { return smtlib::parser::token::RBRACKET_TOK; }
"{"             { return smtlib::parser::token::LCURBRACK_TOK;}
"}"             { return smtlib::parser::token::RCURBRACK_TOK;}
"("             { return smtlib::parser::token::LPAREN_TOK; }
")"             { return smtlib::parser::token::RPAREN_TOK; }
"$"             { return smtlib::parser::token::DOLLAR_TOK; }
"?"             { return smtlib::parser::token::QUESTION_TOK; }


"bit0"          { return smtlib::parser::token::BIT0_TOK; }
"bit1"          { return smtlib::parser::token::BIT1_TOK; }

"concat"        { return smtlib::parser::token::BVCONCAT_TOK; }
"extract"       { return smtlib::parser::token::BVEXTRACT_TOK; }
"select"	{ return smtlib::parser::token::BVSELECT_TOK; }

"bvnot"         { return smtlib::parser::token::BVNOT_TOK; }
"bvand"         { return smtlib::parser::token::BVAND_TOK; }
"bvor"          { return smtlib::parser::token::BVOR_TOK; }
"bvneg"         { return smtlib::parser::token::BVNEG_TOK; }
"bvnand"        { return smtlib::parser::token::BVNAND_TOK; }
"bvnor"         { return smtlib::parser::token::BVNOR_TOK; }
"bvxor"         { return smtlib::parser::token::BVXOR_TOK; }
"bvxnor"        { return smtlib::parser::token::BVXNOR_TOK; }

"="             { return smtlib::parser::token::EQ_TOK; }
"bvcomp"        { return smtlib::parser::token::BVCOMP_TOK; }
"bvult"         { return smtlib::parser::token::BVULT_TOK; }
"bvule"         { return smtlib::parser::token::BVULE_TOK; }
"bvugt"         { return smtlib::parser::token::BVUGT_TOK; }
"bvuge"         { return smtlib::parser::token::BVUGE_TOK; }
"bvslt"         { return smtlib::parser::token::BVSLT_TOK; }
"bvsle"         { return smtlib::parser::token::BVSLE_TOK; }
"bvsgt"         { return smtlib::parser::token::BVSGT_TOK; }
"bvsge"         { return smtlib::parser::token::BVSGE_TOK; }

"bvadd"         { return smtlib::parser::token::BVADD_TOK; }
"bvsub"         { return smtlib::parser::token::BVSUB_TOK; }
"bvmul"         { return smtlib::parser::token::BVMUL_TOK; }
"bvudiv"        { return smtlib::parser::token::BVUDIV_TOK; }
"bvurem"        { return smtlib::parser::parser::token::BVUREM_TOK; }
"bvsdiv"        { return smtlib::parser::token::BVSDIV_TOK; }
"bvsrem"        { return smtlib::parser::token::BVSREM_TOK; }
"bvsmod"        { return smtlib::parser::token::BVSMOD_TOK; }

"bvshl"         { return smtlib::parser::token::BVSHL_TOK; }
"bvlshr"        { return smtlib::parser::token::BVLSHR_TOK; }
"bvashr"        { return smtlib::parser::token::BVASHR_TOK; }

"repeat"        { return smtlib::parser::token::REPEAT_TOK; }
"zero_extend"   { return smtlib::parser::token::ZEXT_TOK; }
"sign_extend"   { return smtlib::parser::token::SEXT_TOK; }
"rotate_left"   { return smtlib::parser::token::ROL_TOK; }
"rotate_right"  { return smtlib::parser::token::ROR_TOK; }


"bv"[0-9]+              { SMT_CPY_TXT(smtlibtext); return smtlib::parser::token::BV_TOK; }
"bvbin"[0-1]+	        { SMT_CPY_TXT(smtlibtext); return smtlib::parser::token::BVBIN_TOK; }
"bvhex"[0-9,A-F,a-f]+	{ SMT_CPY_TXT(smtlibtext); return smtlib::parser::token::BVHEX_TOK; }


({LETTER})({IDCHAR})* { SMT_CPY_TXT(smtlibtext); return smtlib::parser::token::SYM_TOK; }

<<EOF>>         { return smtlib::parser::token::EOF_TOK; }

. { smtliberror("Illegal input character."); }
%%

