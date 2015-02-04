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
ifndef GUEST_ARCH
export GUEST_ARCH=amd64
endif
ifndef GUEST_WIN
export GUEST_WIN=xp
endif

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

mc-std: mc-std-amd64 mc-std-arm mc-std-x86

all:: $(LibDir)/libkleeRuntimeMMU.bc $(LibDir)/libkleeRuntimeIntrinsic.bc

$(LibDir)/libkleeRuntimeIntrinsic.bc: $(LibDir)/libkleeRuntimeIntrinsic.bca
	mkdir -p intrin_tmp-$@
	cd intrin_tmp-$@ && ar x $^ && cd ..
	llvm-link -f -o `echo $^ | sed "s/\.bca/\.bc/"` intrin_tmp-$@/*.bc
	rm -rf intrin_tmp-$@

mc-fdt: $(LibDir)/libkleeRuntimeMC-fdt.bca
	mkdir -p mcfdt_tmp
	cd mcfdt_tmp && ar x $(LibDir)/libkleeRuntimeMC-fdt.bca && cd ..
	llvm-link -f -o $(LibDir)/libkleeRuntimeMC-fdt.bc mcfdt_tmp/*.bc
	rm -rf mcfdt_tmp
	cp $(LibDir)/libkleeRuntimeMC-fdt.bc $(VEXLLVM_HELPER_PATH)

scan-build:
	mkdir -p scan-out
	scan-build --use-cc="$(LLVMCC)" --use-c++="$(LLVMCXX)" -o `pwd`/scan-out make all

test-all: test test-replay


test-replay: test-replay-path
	cd tests-replay && ./replaytest.sh
test-replay-path:
	cd tests-replay && ./replaypathtest.sh

static-pkg:
	STATIC_BUILD=1 make -j6
	rm -rf kmc-pkg
	mkdir -p kmc-pkg/{lib,bin,scripts}
	cp $(BuildMode)/lib/*.bc kmc-pkg/lib
	cp $(VEXLLVM_HELPER_PATH)/*bc kmc-pkg/lib
	cp $(BuildMode)/bin/*static  kmc-pkg/bin
	cp scripts/static-env kmc-pkg/scripts
	cp ~/bin/stp-static kmc-pkg/bin/stp
	strip kmc-pkg/bin/stp
	tar cvf static-pkg.tar kmc-pkg
	gzip -f static-pkg.tar

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

test-broken-optmul:
	KMC_RNR_FLAGS="-smt-optmul -smt-brokenoptmul -smt-xchkmul  -xchk-expr-builder" ./scripts/kmc-run-n-replay "/usr/bin/unrar x a"


nt-clean:
	rm -rf runtime/nt/*/*.{bc,bca,ll,d,o}

mc-clean:
	rm -rf runtime/syscall/*/*.{bc,bca,ll,d,o}
#	rm -rf $(LibDir)/libkleeRuntimeMC*-*.bc*

#rm -rf guest-*

clean::
	$(MAKE) -C test clean 
	$(MAKE) -C unittests clean
	rm -rf tests-replay/klee-out* tests-replay/guest-*
	rm -rf klee-out-*
	rm -rf docs/doxygen
	rm -rf bintests/out/*
