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
LIBRARYNAME=kleeRuntimeIntrinsic
DONT_BUILD_RELINKED=1
BUILD_ARCHIVE=1
BYTECODE_LIBRARY=1
# Don't strip debug info from the module.
DEBUG_RUNTIME=1
NO_PEDANTIC=1

include $(LEVEL)/Makefile.common
LIBBCA:=$(LibDir)/lib$(LIBRARYNAME).bca
LIBTXT:=$(LibDir)/$(LIBRARYNAME)-fns.txt
OPTFLAGS:="-disable-opt"

# memset intrinsics have been causing some issues
CFLAGS=-fno-builtin -nodefaultlibs -O0
CXXFLAGS=-fno-builtin -nodefaultlibs -O0

all:: $(LIBTXT)

$(LIBTXT): $(LIBBCA) $(LEVEL)/scripts/llvm-list-functions
	$(Echo) "Updating $(LIBRARYNAME) function list for $(BuildMode) build"
	$(Verb) $(LEVEL)/scripts/llvm-list-functions $(LIBBCA) > $@

