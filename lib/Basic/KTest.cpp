//===-- KTest.cpp ---------------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "klee/Internal/ADT/KTest.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <zlib.h>

#define KTEST_VERSION 3
#define KTEST_MAGIC_SIZE 5
#define KTEST_MAGIC "KTEST"

// for compatibility reasons
#define BOUT_MAGIC "BOUT\n"

/***/

static int read_uint32(FILE *f, unsigned *value_out) {
  unsigned char data[4];
  if (fread(data, 4, 1, f)!=1)
    return 0;
  *value_out = (((((data[0]<<8) + data[1])<<8) + data[2])<<8) + data[3];
  return 1;
}

static int write_uint32(FILE *f, unsigned value) {
  unsigned char data[4];
  data[0] = value>>24;
  data[1] = value>>16;
  data[2] = value>> 8;
  data[3] = value>> 0;
  return fwrite(data, 1, 4, f)==4;
}

static int read_string(FILE *f, char **value_out) {
  unsigned len;
  if (!read_uint32(f, &len))
    return 0;
  *value_out = (char*) malloc(len+1);
  if (!*value_out)
    return 0;
  if (fread(*value_out, len, 1, f)!=1)
    return 0;
  (*value_out)[len] = 0;
  return 1;
}

static int write_string(FILE *f, const char *value) {
  unsigned len = strlen(value);
  if (!write_uint32(f, len))
    return 0;
  if (fwrite(value, len, 1, f)!=1)
    return 0;
  return 1;
}

/***/


unsigned kTest_getCurrentVersion() {
  return KTEST_VERSION;
}


static int kTest_checkHeader(FILE *f) {
  char header[KTEST_MAGIC_SIZE];
  if (fread(header, KTEST_MAGIC_SIZE, 1, f)!=1)
    return 0;
  if (memcmp(header, KTEST_MAGIC, KTEST_MAGIC_SIZE) &&
      memcmp(header, BOUT_MAGIC, KTEST_MAGIC_SIZE))
    return 0;
  return 1;
}

int kTest_isKTestFile(const char *path) {
  FILE *f = fopen(path, "rb");
  int res;

  if (!f)
    return 0;
  res = kTest_checkHeader(f);
  fclose(f);
  
  return res;
}

static KTest* kTest_fromUncompressedFile(const char* path)
{
  FILE *f;
  KTest *res = 0;
  unsigned i, version;

  f = fopen(path, "rb");
  if (!f) goto error;
  if (!kTest_checkHeader(f)) goto error;

  res = (KTest*) calloc(1, sizeof(*res));
  if (!res) goto error;

  if (!read_uint32(f, &version)) goto error_res;
  if (version > kTest_getCurrentVersion()) goto error_res;

  res->version = version;

  if (!read_uint32(f, &res->numArgs)) goto error;

  res->args = (char**) calloc(res->numArgs, sizeof(*res->args));
  if (!res->args) goto error_res;
  
  for (i=0; i<res->numArgs; i++)
    if (!read_string(f, &res->args[i]))
      goto error_args;

  if (version >= 2) {
    if (!read_uint32(f, &res->symArgvs)) goto error_args;
    if (!read_uint32(f, &res->symArgvLen)) goto error_args;
  }

  if (!read_uint32(f, &res->numObjects)) goto error_args;

  res->objects = (KTestObject*) calloc(res->numObjects, sizeof(*res->objects));
  if (!res->objects) goto error_args;

  for (i=0; i<res->numObjects; i++) {
    KTestObject *o = &res->objects[i];
    if (!read_string(f, &o->name)) goto error_objs;
    if (!read_uint32(f, &o->numBytes)) goto error_objs;

    o->bytes = (unsigned char*) malloc(o->numBytes);
    if (fread(o->bytes, o->numBytes, 1, f)!=1) goto error_objs;
  }

  fclose(f);

  return res;

error_objs:
  assert (res->objects != 0);
  for (i=0; i<res->numObjects; i++) {
    KTestObject *bo = &res->objects[i];
    if (bo->name)  free(bo->name);
    if (bo->bytes) free(bo->bytes);
  }
  free(res->objects);

error_args:
  assert (res->args != 0);
  for (i=0; i<res->numArgs; i++) {
      if (res->args[i])
        free(res->args[i]);
  }
  free(res->args);

error_res:
  assert (res != 0);
  free(res);
  res = 0;

error:
  assert (res == 0);
  if (f) fclose(f);

  return 0;

}

static bool kTest_uncompress(const char* path, const char* new_path)
{
	char 	buf[4096];
	ssize_t	br, bw;
	int	ktest_fd;
	gzFile	gzF;

	/* create open gz file */
	gzF = gzopen(path, "rb");
	if (gzF == NULL) return false;

	/* copy gz file to normal file  */
	ktest_fd = open(new_path, O_WRONLY|O_CREAT, 0600);
	if (ktest_fd < 0) {
		gzclose(gzF);
		return false;
	}

	while ((br = gzread(gzF, buf, 4096)) > 0) {
		bw = write(ktest_fd, buf, br);
		assert (bw == br);
	}

	close(ktest_fd);
	gzclose(gzF);

	return true;
}

KTest *kTest_fromFile(const char *path)
{
	KTest		*ret;
	char		*tmp_path = 0;
	const char	*real_path;
	int		path_len;

	path_len = strlen(path);
	if (path_len > 3 && strcmp(path+path_len-3, ".gz") == 0) {
		/* if it's a gz, unzip it */
		tmp_path = strdup(path);
		real_path = tmp_path;
		tmp_path[path_len-3] = '\0';
		if (kTest_uncompress(path, tmp_path) == false) {
			free(tmp_path);
			return NULL;
		}
	} else
		real_path = path;

	ret = kTest_fromUncompressedFile(real_path);

	if (tmp_path) free(tmp_path);

	return ret;
}

int kTest_toFile(KTest *bo, const char *path) {
  FILE *f = fopen(path, "wb");
  unsigned i;

  if (!f) 
    goto error;
  if (fwrite(KTEST_MAGIC, strlen(KTEST_MAGIC), 1, f)!=1)
    goto error;
  if (!write_uint32(f, KTEST_VERSION))
    goto error;
      
  if (!write_uint32(f, bo->numArgs))
    goto error;
  for (i=0; i<bo->numArgs; i++) {
    if (!write_string(f, bo->args[i]))
      goto error;
  }

  if (!write_uint32(f, bo->symArgvs))
    goto error;
  if (!write_uint32(f, bo->symArgvLen))
    goto error;
  
  if (!write_uint32(f, bo->numObjects))
    goto error;
  for (i=0; i<bo->numObjects; i++) {
    KTestObject *o = &bo->objects[i];
    if (!write_string(f, o->name))
      goto error;
    if (!write_uint32(f, o->numBytes))
      goto error;
    if (fwrite(o->bytes, o->numBytes, 1, f)!=1)
      goto error;
  }

  fclose(f);

  return 1;
 error:
  if (f) {
    int e = errno;
    fclose(f);
    errno = e;
  }
  return 0;
}

unsigned kTest_numBytes(KTest *bo) {
  unsigned i, res = 0;
  for (i=0; i<bo->numObjects; i++)
    res += bo->objects[i].numBytes;
  return res;
}

void kTest_free(KTest *bo) {
  unsigned i;
  for (i=0; i<bo->numArgs; i++)
    free(bo->args[i]);
  free(bo->args);
  for (i=0; i<bo->numObjects; i++) {
    free(bo->objects[i].name);
    free(bo->objects[i].bytes);
  }
  free(bo->objects);
  free(bo);
}
