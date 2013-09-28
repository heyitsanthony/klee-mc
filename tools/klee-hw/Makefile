#===-- tools/solverd/Makefile ------------------------------*- Makefile -*--===#
#
#                     The KLEE Symbolic Virtual Machine
#
# This file is distributed under the University of Illinois Open Source
# License. See LICENSE.TXT for details.
#
#===------------------------------------------------------------------------===#

LEVEL=../..
TOOLNAME = klee-hw

include $(LEVEL)/Makefile.common

LIBS += -lssl
LIBS += -lrt
RDYNAMIC=
LDFLAGS += -Wl,-Ttext-segment=0x50000000 $(STATIC_FLAGS)
