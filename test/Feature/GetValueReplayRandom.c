// RUN: %llvmgcc -c -I../../../include -o %t1.bc %s
// RUN: %klee -randomize-getvalue -write-paths -pipe-solver %t1.bc 2>%t1.1.err
// RUN: %klee -randomize-getvalue -replay-path-dir=klee-last -exit-on-error %t1.bc 2>%t1.2.err
// RUN: ls klee-last/ | not grep .err
//
#include "GetValueReplay.c"
