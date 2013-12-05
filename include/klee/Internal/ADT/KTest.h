//===-- KTest.h --------------------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef __COMMON_KTEST_H__
#define __COMMON_KTEST_H__


#define KTEST_FL_ERROR		(1 << 0)	/* did it raise an error? */
#define KTEST_FL_UNTIL_RETURN	(1 << 1)
#define KTEST_FL_UC		(1 << 2)	/* maybe? */

#ifdef __cplusplus
extern "C" {
#endif

	typedef struct KTestObject KTestObject;
	struct KTestObject {
		char *name;
		unsigned numBytes;
		unsigned char *bytes;
	};

	typedef struct KTest KTest;
	struct KTest {
		unsigned	version; 
		unsigned	numArgs;
		char		**args;

		unsigned	symArgvs;
		unsigned	symArgvLen;

		/****** version 4 *****/
		/* snapshot path / name */
		char		*imagePath;
		char		*initialFunc;
		unsigned	flags;
		/* either a .bc file or a .txt file with
		 * list of .bc files */
		char		*runtimePath;
		/***********/

		unsigned	numObjects;
		KTestObject	*objects;
	};


	/* returns the current .ktest file format version */
	unsigned kTest_getCurrentVersion();

	/* return true iff file at path matches KTest header */
	int   kTest_isKTestFile(const char *path);

	/* returns NULL on (unspecified) error */
	KTest* kTest_fromFile(const char *path);

	/* returns 1 on success, 0 on (unspecified) error */
	int   kTest_toFile(KTest *, const char *path);

	/* returns total number of object bytes */
	unsigned kTest_numBytes(KTest *);

	void  kTest_free(KTest *);

#ifdef __cplusplus
}

#include <iostream>
int kTest_toStream(KTest* bo, std::ostream& os);
#endif


#endif
