#ifndef KLEE_PARTSEED_H
#define KLEE_PARTSEED_H

#ifdef __cplusplus
extern "C" {
#endif
typedef uint64_t psid_t;
void klee_partseed_end(psid_t);
psid_t klee_partseed_begin(const char* name);
#ifdef __cplusplus
}
#endif

#endif
