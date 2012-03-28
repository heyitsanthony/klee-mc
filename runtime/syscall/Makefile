#===-- runtime/POSIX/Makefile ------------------------------*- Makefile -*--===#
#
#                     The KLEE Symbolic Virtual Machine
#
# This file is distributed under the University of Illinois Open Source
# License. See LICENSE.TXT for details.
#
#===------------------------------------------------------------------------===#

LEVEL=../..

DISABLE_ASSERTIONS=1
LIBRARYNAME=kleeRuntimeMC
DONT_BUILD_RELINKED=1
BUILD_ARCHIVE=1
BYTECODE_LIBRARY=1
# Don't strip debug info from the module.
DEBUG_RUNTIME=1
NO_PEDANTIC=1

include $(LEVEL)/Makefile.common

ifeq ($(GUEST_ARCH), amd64)
C.Flags += -DGUEST_ARCH_AMD64=1
CXX.Flags += -DGUEST_ARCH_AMD64=1
LibName.BCA := $(LibDir)/lib$(LIBRARYNAME)-amd64.bca
else ifeq ($(GUEST_ARCH), arm)
C.Flags += -DGUEST_ARCH_ARM=1
CXX.Flags += -DGUEST_ARCH_ARM=1
LibName.BCA := $(LibDir)/lib$(LIBRARYNAME)-arm.bca
else ifeq ($(GUEST_ARCH), x86)
C.Flags += -DGUEST_ARCH_X86=1
CXX.Flags += -DGUEST_ARCH_X86=1
LibName.BCA := $(LibDir)/lib$(LIBRARYNAME)-x86.bca
else
$(error Unknown arch-- '$(GUEST_ARCH)')
endif

$(LibName.BCA): $(LibDir)/lib$(LIBRARYNAME).bca
	cp $< $@
	rm $^
	touch $^

all:: $(LibName.BCA)
	echo $<
