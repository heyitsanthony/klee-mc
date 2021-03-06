#===-- runtime/klee-libc/Makefile --------------------------*- Makefile -*--===#
#
#                     The KLEE Symbolic Virtual Machine
#
# This file is distributed under the University of Illinois Open Source
# License. See LICENSE.TXT for details.
#
#===------------------------------------------------------------------------===#

LEVEL=../..

LIBRARYNAME=klee-libc
DONT_BUILD_RELINKED=1
BYTECODE_LIBRARY=1
# Don't strip debug info from the module.
DEBUG_RUNTIME=1
NO_PEDANTIC=1

# Add __NO_INLINE__ to prevent glibc from using inline definitions of some
# builtins.
C.Flags += -D__NO_INLINE__

include $(LEVEL)/Makefile.common

LIBBCA:=$(LibDir)/lib$(LIBRARYNAME).bca
LIBBC:=$(LibDir)/lib$(LIBRARYNAME).bc
LIBTXT:=$(LibDir)/$(LIBRARYNAME)-fns.txt

all:: $(LIBTXT)

$(LIBTXT): $(LIBBC) $(LIBBCA) $(LEVEL)/scripts/llvm-list-functions
	$(Echo) "Updating $(LIBRARYNAME) function list for $(BuildMode) build"
	$(Verb) $(LEVEL)/scripts/llvm-list-functions $(LIBBCA) > $@

$(LIBBC): $(LIBBCA)
	llvm-link $(BuildMode)/*.bc -o $@
