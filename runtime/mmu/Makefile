#===-- runtime/Intrinsic/Makefile --------------------------*- Makefile -*--===#
#
#                     The KLEE Symbolic Virtual Machine
#
# This file is distributed under the University of Illinois Open Source
# License. See LICENSE.TXT for details.
#
#===------------------------------------------------------------------------===#

LEVEL=../..

RUNTIME_ENABLE_OPTIMIZED=1
ENABLE_OPTIMIZED=1
LIBRARYNAME=kleeRuntimeMMU
BUILD_ARCHIVE=1
BYTECODE_LIBRARY=1
# Don't strip debug info from the module.
DEBUG_RUNTIME=1

include $(LEVEL)/Makefile.common

LIBBCA:=$(LibDir)/lib$(LIBRARYNAME).bca
LIBTXT:=$(LibDir)/$(LIBRARYNAME)-fns.txt
#OPTFLAGS:="-disable-opt"
# memset intrinsics have been causing some issues
#CFLAGS=-fno-builtin -nodefaultlibs -O0 -emit-llvm
#CXXFLAGS=-fno-builtin -nodefaultlibs -O0 -emit-llvm
