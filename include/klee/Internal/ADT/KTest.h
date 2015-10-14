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

#include <string.h>

#define KTEST_FL_ERROR		(1 << 0)	/* did it raise an error? */
#define KTEST_FL_UNTIL_RETURN	(1 << 1)
#define KTEST_FL_UC		(1 << 2)	/* maybe? */

#ifdef __cplusplus
#include <iostream>
extern "C" {
#endif

	typedef struct KTestObject KTestObject;
	struct KTestObject {
		char *name;
		unsigned numBytes;
		unsigned char *bytes;
#ifdef __cplusplus
		KTestObject() : name(0), numBytes(0), bytes(0) {}
		KTestObject(const KTestObject&);
		~KTestObject();
		KTestObject& operator =(const KTestObject&);

		bool operator==(const KTestObject& ko) const {
			return strcmp(name, ko.name) == 0 &&
				numBytes == ko.numBytes &&
				memcmp(bytes, ko.bytes, numBytes) == 0;
		}

		bool operator!=(const KTestObject& ko) const {
			return !(*this == ko);
		}
#endif
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
		unsigned	syscall_c;
		unsigned	flags;

		/* either a .bc file or a .txt file with
		 * list of .bc files */
		char		*runtimePath;
		/***********/

		unsigned	numObjects;
		KTestObject	*objects;
#ifdef __cplusplus
		KTest()
			: version(0)
			, numArgs(0)
			, args(0)
			, symArgvs(0)
			, symArgvLen(0)
			, imagePath(0)
			, initialFunc(0)
			, syscall_c(0)
			, flags(0)
			, runtimePath(0)
			, numObjects(0)
			, objects(0)
		{}
		KTest(const KTest& kts);
		~KTest();

		static KTest* create(const char* path);
		static KTest* create(std::istream& is);
		unsigned numBytes(void) const;
		int toStream(std::ostream& os) const;
		int toFile(const char *path) const;

		void newPrefix(
			const KTest& new_ktest,
			unsigned pfx_this_end,
			unsigned pfx_new_ktest_end);
#endif
	};


	/* returns the current .ktest file format version */
	unsigned kTest_getCurrentVersion();

	/* return true iff file at path matches KTest header */
	int   kTest_isKTestFile(const char *path);

	/* returns NULL on (unspecified) error */
	KTest* kTest_fromFile(const char *path);

	/* returns 1 on success, 0 on (unspecified) error */
	int   kTest_toFile(const KTest *, const char *path);

	/* returns total number of object bytes */
	unsigned kTest_numBytes(KTest *);

	void  kTest_free(KTest *);

#ifdef __cplusplus
}

// comparison on prefixes
bool operator<(const KTest& lhs, const KTest& rhs);
#endif


#endif
