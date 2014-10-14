#===-- runtime/Intrinsic/Makefile --------------------------*- Makefile -*--===#
#
#                     The KLEE Symbolic Virtual Machine
#
# This file is distributed under the University of Illinois Open Source
# License. See LICENSE.TXT for details.
#
#===------------------------------------------------------------------------===#

LEVEL=../..

RUNTIME_ENABLE_OPTIMIZED=0
ENABLE_OPTIMIZED=0
OPTIMIZE_OPTION=-O0
LIBRARYNAME=kleeRuntimeIntrinsic
BUILD_ARCHIVE=1
BYTECODE_LIBRARY=1
# Don't strip debug info from the module.
DEBUG_RUNTIME=1

include $(LEVEL)/Makefile.common

LIBBCA:=$(LibDir)/lib$(LIBRARYNAME).bca
LIBTXT:=$(LibDir)/$(LIBRARYNAME)-fns.txt
OPTFLAGS:="-disable-opt"
# memset intrinsics have been causing some issues
#CFLAGS=-fno-builtin -nodefaultlibs -O0 -emit-llvm
#CXXFLAGS=-fno-builtin -nodefaultlibs -O0 -emit-llvm

LibName.BCA := $(LibDir)/lib$(LIBRARYNAME).bca
LibName.BC := $(LibDir)/lib$(LIBRARYNAME).bc


$(LibName.BC): $(LibName.BCA)
	../../scripts/link-bca $^ $@

all:: $(LIBTXT) $(LibName.BC) $(LibName.BCA)

$(LIBTXT): $(LIBBCA) $(LEVEL)/scripts/llvm-list-functions
	$(Echo) "Updating $(LIBRARYNAME) function list for $(BuildMode) build"
	$(Verb) $(LEVEL)/scripts/llvm-list-functions $(LIBBCA) > $@

