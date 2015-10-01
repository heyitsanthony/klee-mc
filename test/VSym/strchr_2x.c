// RUN: gcc %s -O0 -o %t1
// RUN: rm -rf guest-vstrchr
// RUN: ../../../scripts/save_guest.sh ./%t1 guest-vstrchr
// RUN: klee-mc -stop-after-n-tests=100 -use-hookpass -guest-sshot=guest-vstrchr -guest-type=sshot -hookpass-lib=libvirtsyms.bc - ./%t1 2>%t1.err >%t1.out
// RUN: ../../../scripts/get_all_returns.sh >%t1-rets 
// RUN: ls klee-last | not grep .err
// RUN: not grep 0xffffff %t1-rets
// RUN: not grep 0x5 %t1-rets
// RUN: not grep 0x7 %t1-rets
// RUN: grep 0x2 %t1-rets
// RUN: grep 0x1 %t1-rets
// RUN: grep 0x3 %t1-rets
// RUN: grep 0x4 %t1-rets
// RUN: grep 0x8 %t1-rets
// RUN: not grep 0x9 %t1-rets
// RUN: ls klee-last | grep ktest | wc -l | grep 5

#define USE_2X
#include "strchr_.h"
