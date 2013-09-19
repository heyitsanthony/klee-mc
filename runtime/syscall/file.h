#ifndef KLEE_SC_FILE_H
#define KLEE_SC_FILE_H

struct sc_pkt;

/* 0 => already_logged,
 * 1 => break */
int file_sc(struct sc_pkt* sc);
int file_path_has_sym(const char* s);

#endif
