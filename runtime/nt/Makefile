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
LIBRARYNAME=kleeRuntimeMC-$(GUEST_WIN)
DONT_BUILD_RELINKED=1
BUILD_ARCHIVE=1
BYTECODE_LIBRARY=1
# Don't strip debug info from the module.
DEBUG_RUNTIME=1
NO_PEDANTIC=1
C.Flags += -I/usr/include/wine/windows -Wno-attributes -D__builtin_ms_va_list=__builtin_va_list

include $(LEVEL)/Makefile.common

ifeq ($(GUEST_WIN), xp)
C.Flags += -DGUEST_WINXP=1
CXX.Flags += -DGUEST_WINXP=1
else ifeq ($(GUEST_WIN), win7)
C.Flags += -DGUEST_WIN7=1
CXX.Flags += -DGUEST_WIN7=1
else
$(error Unknown arch-- '$(GUEST_WIN)')
endif
LibName.BCA := $(LibDir)/lib$(LIBRARYNAME).bca
LibName.BC := $(LibDir)/lib$(LIBRARYNAME).bc


LIBBCA:=$(LibDir)/lib$(LIBRARYNAME).bca

all :: $(LibDir)/lib$(LIBRARYNAME).bc

$(LibDir)/lib$(LIBRARYNAME).bc: $(LibDir)/lib$(LIBRARYNAME).bca
	../../scripts/link-bca $^ $@
