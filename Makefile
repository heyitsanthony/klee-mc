#===-- klee/Makefile ---------------------------------------*- Makefile -*--===#
#
#                     The KLEE Symbolic Virtual Machine
#
# This file is distributed under the University of Illinois Open Source
# License. See LICENSE.TXT for details.
#
#===------------------------------------------------------------------------===#

#
# Indicates our relative path to the top of the project's root directory.
#
LEVEL = .

ENABLE_RTTI=1
REQUIRES_RTTI=1
export REQUIRES_RTTI=1
export ENABLE_RTTI=1

include $(LEVEL)/Makefile.config

DIRS = lib
ifeq ($(ENABLE_EXT_STP),0)
  DIRS += stp
endif
DIRS += tools runtime
EXTRA_DIST = include

# Only build support directories when building unittests.
ifeq ($(MAKECMDGOALS),unittests)
  DIRS := $(filter-out tools runtime, $(DIRS)) unittests
  OPTIONAL_DIRS :=
endif

#
# Include the Master Makefile that knows how to build all.
#
include $(LEVEL)/Makefile.common

UCLIBCA:=$(KLEE_UCLIBC)/lib/libc.a
UCLIBCTXT:=$(LibDir)/uclibc-fns.txt

ifeq ($(ENABLE_UCLIBC),1)
all:: $(UCLIBCTXT)
endif

$(UCLIBCTXT): $(UCLIBCA) scripts/llvm-list-functions
	$(Echo) "Updating uclibc function list for $(BuildMode) build"
	$(Verb) $(LEVEL)/scripts/llvm-list-functions $(UCLIBCA) > $(UCLIBCTXT)

.PHONY: doxygen
doxygen:
	doxygen docs/doxygen.cfg

.PHONY: cscope.files
cscope.files:
	find \
          lib include stp tools runtime examples unittests test \
          -name Makefile -or \
          -name \*.in -or \
          -name \*.c -or \
          -name \*.cpp -or \
          -name \*.exp -or \
          -name \*.inc -or \
          -name \*.h | sort > cscope.files

test::
	-(cd test/ && make)

.PHONY: mc
mc: mc-std mc-fdt

mc-std: $(LibDir)/libkleeRuntimeMC.bca
	mkdir -p mc_tmp
	cd mc_tmp && ar x $(LibDir)/libkleeRuntimeMC.bca && cd ..
	llvm-link -f -o $(LibDir)/libkleeRuntimeMC.bc mc_tmp/*.bc
	rm -rf mc_tmp
	cp $(LibDir)/libkleeRuntimeMC.bc $(VEXLLVM_HELPER_PATH)

mc-fdt: $(LibDir)/libkleeRuntimeMC-fdt.bca
	mkdir -p mcfdt_tmp
	cd mcfdt_tmp && ar x $(LibDir)/libkleeRuntimeMC-fdt.bca && cd ..
	llvm-link -f -o $(LibDir)/libkleeRuntimeMC-fdt.bc mcfdt_tmp/*.bc
	rm -rf mcfdt_tmp
	cp $(LibDir)/libkleeRuntimeMC-fdt.bc $(VEXLLVM_HELPER_PATH)


test-all: test test-replay


test-replay: test-replay-path
	cd tests-replay && ./replaytest.sh
test-replay-path:
	cd tests-replay && ./replaypathtest.sh

.PHONY: kmc-bintests
kmc-bintests: all
	bintests/run_bin.sh
	bintests/process.sh
	bintests/mkreport.py

.PHONY: phash-analyze
phash-analyze:
	phashtests/kmc-phash-convergence

.PHONY: klee-cov
klee-cov:
	rm -rf klee-cov
	zcov-scan --look-up-dirs=1 klee.zcov .
	zcov-genhtml --root $$(pwd) klee.zcov klee-cov

clean::
	$(MAKE) -C test clean 
	$(MAKE) -C unittests clean
	rm -rf tests-replay/klee-out* tests-replay/guest-*
	rm -rf klee-out-*
	rm -rf guest-*
	rm -rf docs/doxygen
	rm -rf bintests/out/*
