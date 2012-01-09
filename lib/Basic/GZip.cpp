#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <zlib.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include "klee/klee.h"
#include "klee/util/gzip.h"

using namespace klee;

bool GZip::gzipFile(const char* src, const char* dst)
{
	char 	buf[4096];
	ssize_t	br;
	int	ktest_fd, err;
	gzFile	gzF;

	/* create fresh gz file */
	gzF = gzopen(dst, "w");
	if (gzF == NULL) return false;

	/* copy file to gz file */
	ktest_fd  = open(src, O_RDONLY);
	if (ktest_fd < 0) {
		gzclose(gzF);
		return false;
	}

	while ((br = read(ktest_fd, buf, 4096)) > 0) {
		gzwrite(gzF, buf, br);
		if (br < 4096)
			break;
	}
	close(ktest_fd);
	gzclose(gzF);

	/* get rid of old file */
	err = unlink(src);
	if (err != 0) return false;

	return true;
}

bool GZip::gunzipFile(const char* src, const char* dst)
{
	char 	buf[4096];
	ssize_t	br, bw;
	int	ktest_fd;
	gzFile	gzF;

	/* create fresh gz file */
	gzF = gzopen(src, "r");
	if (gzF == NULL) return false;

	ktest_fd  = open(dst, O_WRONLY | O_CREAT, 0660);
	if (ktest_fd < 0) {
		gzclose(gzF);
		return false;
	}

	while ((br = gzread(gzF, buf, 4096)) > 0) {
		bw = write(ktest_fd, buf, br);
		assert (bw == br && "Leaky pen");
		if (br < 4096)
			break;
	}
	close(ktest_fd);
	gzclose(gzF);

	if (br < 0) return false;

	/* don't remove src file! */
	return true;
}

FILE* GZip::gunzipTempFile(const char* src)
{
	char 	buf[4096];
	char	tempname[16];
	ssize_t	br;
	int	ktest_fd;
	FILE	*ftest_f;
	gzFile	gzF;

	/* create fresh gz file */
	gzF = gzopen(src, "r");
	if (gzF == NULL) return NULL;

	strcpy(tempname, "kleegzXXXXXX");
	ktest_fd = mkstemp(tempname);
	if (ktest_fd < 0) {
		gzclose(gzF);
		return NULL;
	}

	unlink(tempname);

	ftest_f = fdopen(ktest_fd, "w+");
	assert (ftest_f != NULL);

	while ((br = gzread(gzF, buf, 4096)) > 0) {
		size_t bw;
		bw = fwrite(buf, br, 1, ftest_f);
		assert (bw == 1 && "Leaky pen");
		if (br < 4096)
			break;
	}
	gzclose(gzF);

	if (br < 0) {
		fclose(ftest_f);
		return NULL;
	}

	rewind(ftest_f); // my favorite libc function-- like a cassette!
	return ftest_f;
}
