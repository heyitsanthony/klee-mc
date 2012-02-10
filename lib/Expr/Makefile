#===-- lib/Expr/Makefile -----------------------------------*- Makefile -*--===#
#
#                     The KLEE Symbolic Virtual Machine
#
# This file is distributed under the University of Illinois Open Source
# License. See LICENSE.TXT for details.
#
#===------------------------------------------------------------------------===#

LEVEL=../..

LIBRARYNAME=kleaverExpr
DONT_BUILD_RELINKED=1
BUILD_ARCHIVE=1

include $(LEVEL)/Makefile.common

# Gross, but I don't want to build proper rules for this, and I don't want users
# to have to have bison/flex, so for now require developers to make these
# manually (at least initially).

smtlib_parser.cpp smtlib_parser.hpp: smtlib.y
	bison --skeleton=lalr1.cc -d -o smtlib_parser.cpp -p smtlib smtlib.y
	perl -pi -e 's/union/struct/g' smtlib_parser.cpp
	perl -pi -e 's/union/struct/g' smtlib_parser.hpp


smtlib_lexer.cpp: smtlib.lex smtlib_parser.hpp
	flex -I -Psmtlib -osmtlib_lexer.cpp smtlib.lex
	perl -pi -e 's/union/struct/g' smtlib_lexer.cpp

.PHONY: regen
regen: smtlib_lexer.cpp smtlib_parser.cpp smtlib_parser.h
