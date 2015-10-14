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
#include <memory>

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

	*value_out = new char[len+1];
	if ((*value_out) == NULL) return 0;

	is.read(*value_out, len);
	if (is.fail() || is.bad())
		return 0;

	(*value_out)[len] = 0;
	return 1;
}

static int write_uint32(std::ostream& os, unsigned value)
{
	uint8_t data[4] = {
		(uint8_t)(value>>24), (uint8_t)(value>>16),
		(uint8_t)(value>> 8), (uint8_t)(value>> 0)};
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


KTest *kTest_fromFile(const char *path) { return KTest::create(path); }

static bool is_compressed_path(const char* path)
{
	int path_len = strlen(path);
	if (path_len <= 3 || strcmp(path+path_len-3, ".gz") != 0)
		return false;

	return true;
}

#define IO_OUT_MODE	std::ios::out | std::ios::trunc | std::ios::binary
int kTest_toFile(const KTest *bo, const char *path) { return bo->toFile(path); }

int KTest::toFile(const char* path) const
{
	if (is_compressed_path(path)) {
		gzofstream	ofs(path, IO_OUT_MODE);
		return toStream(ofs);
	} else {
		std::ofstream	ofs(path, std::fstream::out | std::fstream::binary);
		return toStream(ofs);
	}
}

static std::unique_ptr<std::istream> istreamFromPath(const char* path)
{
	std::unique_ptr<std::istream>	ifs;

	if (is_compressed_path(path)) {
		ifs = std::make_unique<gzifstream>(
			path,
			std::ios::in | std::ios::binary);
	} else {
		ifs = std::make_unique<std::ifstream>(
			path,
			std::ios::in | std::ios::binary);
	}

	return (ifs->bad() || ifs->fail() || ifs->eof())
		? nullptr
		: std::move(ifs);
}

KTest* KTest::create(const char* path)
{
	auto ifs = istreamFromPath(path);
	return (ifs != nullptr)
		? KTest::create(*ifs)
		: nullptr;
}

int KTest::toStream(std::ostream& os) const
{
	os.write(KTEST_MAGIC, KTEST_MAGIC_SIZE);
	if (os.bad())  return 0;

	if (!write_uint32(os, KTEST_VERSION))
		return 0;

	if (!write_uint32(os, numArgs))
		return 0;

	for (unsigned i = 0; i < numArgs; i++) {
		if (!write_string(os, args[i]))
			return 0;
	}

	if (!write_uint32(os, symArgvs))	return 0;
	if (!write_uint32(os, symArgvLen))	return 0;
	if (!write_uint32(os, numObjects))	return 0;

	for (unsigned i = 0; i < numObjects; i++) {
		KTestObject *o = &objects[i];
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

KTest* KTest::create(std::istream& is)
{
	KTest		*res = NULL;
	unsigned	version;

	if (!kTest_checkHeader(is)) goto error;

	res = new KTest();

	if (!read_uint32(is, &version)) goto error_res;
	if (version > kTest_getCurrentVersion()) goto error_res;

	res->version = version;

	if (!read_uint32(is, &res->numArgs)) goto error;

	res->args = new char*[res->numArgs];
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
		if (!read_string(is, &res->initialFunc)) goto error_ver4;
		if (!read_uint32(is, &res->syscall_c)) goto error_ver4;
		if (!read_uint32(is, &res->flags)) goto error_ver4;
		if (!read_string(is, &res->runtimePath)) goto error_ver4;
		assert (0 == 1 && "STUB: UNTESTED!!!");
	}

	if (!read_uint32(is, &res->numObjects)) goto error_args;

	res->objects = new KTestObject[res->numObjects];
	if (!res->objects) goto error_args;

	for (unsigned i = 0; i < res->numObjects; i++) {
		KTestObject *o = &res->objects[i];
		if (!read_string(is, &o->name)) goto error_objs;
		if (!read_uint32(is, &o->numBytes)) goto error_objs;

		o->bytes = new unsigned char[o->numBytes];
		is.read((char*)o->bytes, o->numBytes);
		if (is.fail() || is.bad())
			goto error_objs;
	}

	return res;

error_objs:
	assert (res->objects != 0);
error_ver4:
error_args:
	assert (res->args != 0);
error_res:
	assert (res != 0);
	delete res;
	res = 0;

error:
	assert (res == 0);
	return 0;
}


unsigned kTest_numBytes(KTest *bo) { return bo->numBytes(); }

unsigned KTest::numBytes(void) const
{
	unsigned res = 0;
	for (unsigned i = 0; i < numObjects; i++)
		res += objects[i].numBytes;
	return res;
}

void kTest_free(KTest *bo) { delete bo; }

KTest::~KTest()
{
	for (unsigned i = 0; i < numArgs; i++)
		delete [] args[i];
	delete [] args;
	delete [] objects;
	delete [] imagePath;
	delete [] initialFunc;
	delete [] runtimePath;
}

KTestObject::KTestObject(const KTestObject& kto)
	: name(0)
	, numBytes(kto.numBytes)
	, bytes(0)
{
	name = strdup(kto.name);
	if (numBytes) {
		bytes = new unsigned char[numBytes];
		memcpy(bytes, kto.bytes, numBytes);
	}
}

KTestObject& KTestObject::operator=(const KTestObject& kto)
{
	if (this == &kto) return *this;

	delete [] name;
	name = nullptr;

	delete [] bytes;
	bytes = nullptr;

	name = strdup(kto.name);

	numBytes = kto.numBytes;
	if (numBytes) {
		bytes = new unsigned char[numBytes];
		memcpy(bytes, kto.bytes, numBytes);
	}

	return *this;
}


KTestObject::~KTestObject()
{
	delete [] name;
	delete [] bytes;
}

bool operator<(const KTest& k1, const KTest& k2)
{
	int		diff;
	unsigned	min_v;

	min_v = (k2.numObjects > k1.numObjects)
		? k1.numObjects
		: k2.numObjects;

	for (unsigned i = 0; i < min_v; i++) {
		const KTestObject	&k1_obj = k1.objects[i],
					&k2_obj = k2.objects[i];
		diff = strcmp(k1_obj.name, k2_obj.name);
		if (diff) return (diff < 0);
		diff = k2_obj.numBytes - k1_obj.numBytes;
		if (diff) return (diff < 0);
		diff = memcmp(k1_obj.bytes, k2_obj.bytes, k1_obj.numBytes);
		if (diff) return (diff < 0);
	}

	if ((diff = (k2.numObjects - k1.numObjects)) != 0)
		return (diff > 0);

	return false;
}


KTest::KTest(const KTest& kts)
	: version(kts.version)
	, numArgs(kts.numArgs)
	, symArgvs(kts.symArgvs)
	, symArgvLen(kts.symArgvLen)
	, imagePath(kts.imagePath ? strdup(kts.imagePath) : nullptr)
	, initialFunc(kts.initialFunc ? strdup(kts.initialFunc) : nullptr)
	, syscall_c(kts.syscall_c)
	, flags(kts.flags)
	, runtimePath(kts.runtimePath ? strdup(kts.runtimePath) : nullptr)
	, numObjects(kts.numObjects)
{
	args = new char*[numArgs];
	for (unsigned i = 0; i < numArgs; i++) {
		args[i] = strdup(kts.args[i]);
	}

	objects = new KTestObject[numObjects];
	for (unsigned i = 0; i < numObjects; i++) {
		objects[i] = kts.objects[i];
	}
}

void KTest::newPrefix(
	const KTest& new_ktest,
	unsigned pfx_this,
	unsigned pfx_new)
{
	assert (pfx_this < numObjects);
	assert (pfx_new < new_ktest.numObjects);

	int	obj_change = pfx_new - pfx_this;

	if (obj_change) {
		KTestObject	*new_objs = new KTestObject[numObjects + obj_change];
		for (unsigned i = pfx_this; i < numObjects; i++) {
			new_objs[i + obj_change] = objects[i];
		}
		delete [] objects;
		objects = new_objs;
		numObjects += obj_change;
	}

	for (unsigned i = 0; i < pfx_new; i++) {
		objects[i] = new_ktest.objects[i];
	}
}
