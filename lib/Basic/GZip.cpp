#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <zlib.h>
#include <assert.h>
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
