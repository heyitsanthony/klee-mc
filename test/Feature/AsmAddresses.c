// RUN: %llvmgcc -g -c -o %t.bc %s
// RUN: %klee --exit-on-error --use-asm-addresses %t.bc
//
// RUN: %llvmgcc -DOVERLAP -g -c -o %t.bc %s
//
// Previously this was a 'not'. I don't see any issue with permitting
// overlapping asm entries. Let me know if this is super-critical and
// I ruined it.
// -AJR
// RUN: %klee --exit-on-error --use-asm-addresses %t.bc

#include <assert.h>


volatile unsigned char x0 __asm ("\0010x0021");
volatile unsigned char whee __asm ("\0010x0WHEE");

#ifdef OVERLAP
volatile unsigned int y0 __asm ("\0010x0030");
volatile unsigned int y1 __asm ("\0010x0032");
#endif

int main() {
  assert(&x0 == (void*) 0x0021);
  assert(&whee != (void*) 0x0);

  return 0;
}
