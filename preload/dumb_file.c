#include "../include/klee/klee.h"
#include <mntent.h>
#include <stdio.h>

char* fgets(char *s, int size, FILE* f)
{
	int	i;

	for (i = 0; i < size-1; i++) {
		int	c;
		c = fgetc(f);
		if (c == EOF) {
			/* EOF and no chars read => NULL */
			if (i == 0)
				return NULL;
			break;
		}
		s[i] = c;
	}

	s[i] = '\0';
	
	return s;
}

#define ME_BUF_LEN	4*(8+1)	/* 4 x strlen=8 strings */
static struct mntent	me;
static char	 	me_buf[ME_BUF_LEN];

struct mntent *getmntent(FILE *fp)
{
	return getmntent_r(fp, &me, me_buf, ME_BUF_LEN);
}


struct mntent *getmntent_r(
	FILE *fp, struct mntent *mntbuf, char *buf, int buflen)
{
	int	i;

	if (fp == NULL) return NULL;
	if (mntbuf == NULL) return NULL;
	if (buf == 0 || buflen < 8)
		return NULL;

	/* too lazy to implement proper make sym */
	ssize_t sz = read(0, buf, buflen);
	if (sz != buflen)
		ksys_silent_exit(0);
	// ksys_assume(sz == buflen);

	/* mark buf symbolic, assign pointers in buf to mntbuf */
	mntbuf->mnt_fsname = buf;
	mntbuf->mnt_dir = &(buf[buflen/4]);
	mntbuf->mnt_dir[-1] = '\0';
	mntbuf->mnt_type = &(buf[2*(buflen/4)]);
	mntbuf->mnt_type[-1] = '\0';
	mntbuf->mnt_opts = &(buf[3*(buflen/4)]);
	mntbuf->mnt_opts[-1] = '\0';
	mntbuf->mnt_opts[buflen-1] = '\0';

	return mntbuf;
}
