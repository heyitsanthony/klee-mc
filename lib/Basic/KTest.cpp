//===-- KTest.cpp ---------------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "klee/Internal/ADT/KTest.h"
#include "klee/Internal/ADT/zfstream.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <fstream>

#define KTEST_VERSION 3
#define KTEST_MAGIC_SIZE 5
#define KTEST_MAGIC "KTEST"

/***/

static int read_uint32(std::istream& is, unsigned *value_out)
{
	unsigned char data[4];

	is.read((char*)data, 4);
	if (is.fail() || is.bad())
		return 0;

	*value_out = (((((data[0]<<8) + data[1])<<8) + data[2])<<8) + data[3];
	return 1;
}

static int read_string(std::istream& is, char **value_out)
{
	unsigned len;

	if (!read_uint32(is, &len)) return 0;

	*value_out = (char*) malloc(len+1);
	if ((*value_out) == NULL) return 0;

	is.read(*value_out, len);
	if (is.fail() || is.bad())
		return 0;

	(*value_out)[len] = 0;
	return 1;
}

static int write_uint32(std::ostream& os, unsigned value)
{
	unsigned char data[4] = { value>>24, value>>16, value>> 8, value>> 0};
	os.write((char*)data, 4);
	return !os.bad();
}

static int write_string(std::ostream& os, const char *value)
{
	int len = strlen(value);
	if (!write_uint32(os, len))
		return 0;

	os.write(value, len);
	return !os.bad();
}

unsigned kTest_getCurrentVersion() {return KTEST_VERSION;}

static bool kTest_checkHeader(std::istream& is)
{
	char header[KTEST_MAGIC_SIZE];

	is.read(header, KTEST_MAGIC_SIZE);
	if (is.fail() || is.bad())
		return false;

	if (memcmp(header, KTEST_MAGIC, KTEST_MAGIC_SIZE))
		return false;

	return true;
}

int kTest_isKTestFile(const char *path)
{
	std::ifstream	is(path, std::ios::in | std::ios::binary);

	if (is.bad() || is.fail() || is.eof())
		return 0;

	return kTest_checkHeader(is);
}

static KTest* kTest_fromStream(std::istream& is)
{
	KTest		*res = NULL;
	unsigned	version;

	if (!kTest_checkHeader(is)) goto error;

	res = (KTest*) calloc(1, sizeof(*res));
	if (!res) goto error;

	if (!read_uint32(is, &version)) goto error_res;
	if (version > kTest_getCurrentVersion()) goto error_res;

	res->version = version;

	if (!read_uint32(is, &res->numArgs)) goto error;

	res->args = (char**) calloc(res->numArgs, sizeof(*res->args));
	if (!res->args) goto error_res;

	for (unsigned i=0; i<res->numArgs; i++)
		if (!read_string(is, &res->args[i]))
			goto error_args;

	if (version >= 2) {
		if (!read_uint32(is, &res->symArgvs)) goto error_args;
		if (!read_uint32(is, &res->symArgvLen)) goto error_args;
	}

	if (version >= 4) {
		if (!read_string(is, &res->imagePath)) goto error_ver4;
		if (!read_string(is, &res->initalFunc)) goto error_ver4;
		if (!read_uint32(is, &res->flags)) goto error_ver4;
		if (!read_string(is, &res->runtimePath) goto error_ver4;
		assert (0 == 1 && "STUB: UNTESTED!!!");
	}

	if (!read_uint32(is, &res->numObjects)) goto error_args;

	res->objects = (KTestObject*) calloc(
		res->numObjects, sizeof(*res->objects));
	if (!res->objects) goto error_args;

	for (unsigned i=0; i<res->numObjects; i++) {
		KTestObject *o = &res->objects[i];
		if (!read_string(is, &o->name)) goto error_objs;
		if (!read_uint32(is, &o->numBytes)) goto error_objs;

		o->bytes = (unsigned char*) malloc(o->numBytes);
		is.read((char*)o->bytes, o->numBytes);
		if (is.fail() || is.bad())
			goto error_objs;
	}

	return res;

error_objs:
	assert (res->objects != 0);
	for (unsigned i=0; i<res->numObjects; i++) {
		KTestObject *bo = &res->objects[i];
		if (bo->name)  free(bo->name);
		if (bo->bytes) free(bo->bytes);
	}
	free(res->objects);

error_ver4:
	if (res->imagePath) free(res->imagePath);
	if (res->initialFunc) free(res->initialFunc);
	if (res->runtimePath) free(res->runtimePath);

error_args:
	assert (res->args != 0);
	for (unsigned i=0; i<res->numArgs; i++) {
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
	return 0;
}

static KTest* kTest_fromUncompressedPath(const char* path)
{
	std::ifstream	is(path, std::ios::in | std::ios::binary);

	if (is.bad() || is.fail() || is.eof())
		return NULL;

	return kTest_fromStream(is);
}

KTest *kTest_fromFile(const char *path)
{
	int	path_len;

	path_len = strlen(path);
	if (path_len <= 3 || strcmp(path+path_len-3, ".gz") != 0)
		return kTest_fromUncompressedPath(path);

	/* if it's a gz, unzip it */
	gzifstream	gis(path, std::ios::in | std::ios::binary);
	if (gis.bad() || gis.fail() || gis.eof())
		return NULL;

	return kTest_fromStream(gis);
}

int kTest_toStream(KTest* bo, std::ostream& os)
{
	os.write(KTEST_MAGIC, KTEST_MAGIC_SIZE);
	if (os.bad())  return 0;

	if (!write_uint32(os, KTEST_VERSION))
		return 0;

	if (!write_uint32(os, bo->numArgs))
		return 0;

	for (unsigned i = 0; i < bo->numArgs; i++) {
		if (!write_string(os, bo->args[i]))
			return 0;
	}

	if (!write_uint32(os, bo->symArgvs))	return 0;
	if (!write_uint32(os, bo->symArgvLen))	return 0;
	if (!write_uint32(os, bo->numObjects))	return 0;

	for (unsigned i = 0; i < bo->numObjects; i++) {
		KTestObject *o = &bo->objects[i];
		if (!write_string(os, o->name))
			return 0;
		if (!write_uint32(os, o->numBytes))
			return 0;
		os.write((const char*)o->bytes, o->numBytes);
		if (os.bad())
			return 0;
	}

	return 1;
}

int kTest_toFile(KTest *bo, const char *path)
{
	std::ofstream	ofs(path, std::fstream::out | std::fstream::binary);
	return kTest_toStream(bo, ofs);
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
