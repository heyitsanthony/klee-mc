#ifndef KLEE_SC_FILE_H
#define KLEE_SC_FILE_H

/* 0 => already_logged,
 * 1 => break */
int file_sc(unsigned int pure_sysnr, unsigned int sys_nr, void* regfile);

#endif
