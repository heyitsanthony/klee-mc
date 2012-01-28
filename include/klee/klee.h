//===-- klee.h --------------------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef __KLEE_H__
#define __KLEE_H__

#include <stdint.h>
#include <stddef.h>

#define SYS_klee		0x12345678
#define KLEE_SYS_REPORT_ERROR	0
#define KLEE_SYS_KMC_SYMRANGE	1	/* kmc_make_range_symbolic(addr, len, name) */
#define KLEE_SYS_ASSUME		2	/* klee_assume(<bool expr>) */
#define KLEE_SYS_IS_SYM		3	/* klee_is_sym(<expr>)	*/
#define KLEE_SYS_NE		4	/* klee_force_ne */
#define KLEE_SYS_PRINT_EXPR	5	/* klee_print_expr */
#define KLEE_SYS_SILENT_EXIT	6	/* klee_silent_exit */
#define KLEE_SYS_SYM_RANGE_BYTES	7 /* klee_sym_range_bytes */

#define ksys_report_error(x,y,z,w)	\
	syscall(SYS_klee, KLEE_SYS_REPORT_ERROR, x, y, z, w)
#define ksys_error(x,y)		ksys_report_error(__FILE__, __LINE__, x, y)

#define ksys_sym(x,y) ksys_kmc_symrange(x, y, "")
#define ksys_kmc_symrange(x,y,z)	\
	syscall(SYS_klee, KLEE_SYS_KMC_SYMRANGE, x, y, z)
#define ksys_assume(x)		syscall(SYS_klee, KLEE_SYS_ASSUME, x)
#define ksys_is_sym(x)		syscall(SYS_klee, KLEE_SYS_IS_SYM, x)
#define ksys_force_ne(x,y)	syscall(SYS_klee, KLEE_SYS_NE, x, y)
#define ksys_print_expr(x,y)	syscall(SYS_klee, KLEE_SYS_PRINT_EXPR, (uint64_t)x, y)
#define ksys_silent_exit(x)	syscall(SYS_klee, KLEE_SYS_SILENT_EXIT, (uint64_t)x)
#define ksys_sym_range_bytes(x,y)	\
	syscall(SYS_klee, KLEE_SYS_SYM_RANGE_BYTES, (uint64_t)x, y)


#ifdef __cplusplus
extern "C" {
#endif

  /* Add an accesible memory object at a user specified location. It
     is the users responsibility to make sure that these memory
     objects do not overlap. These memory objects will also
     (obviously) not correctly interact with external function
     calls. */
  void klee_define_fixed_object(void *addr, size_t nbytes);

  /// klee_make_symbolic - Make the contents of the object pointer to by \arg
  /// addr symbolic.
  ///
  /// \arg addr - The start of the object.
  /// \arg nbytes - The number of bytes to make symbolic; currently this *must*
  /// be the entire contents of the object.
  /// \arg name - An optional name, used for identifying the object in messages,
  /// output files, etc.
  void klee_make_symbolic(void *addr, size_t nbytes, const char *name);

  /// klee_range - Construct a symbolic value in the signed interval
  /// [begin,end).
  ///
  /// \arg name - An optional name, used for identifying the object in messages,
  /// output files, etc.
  int64_t klee_range(int64_t begin, int64_t end, const char *name);

  /// klee_int - Construct an unconstrained symbolic integer.
  ///
  /// \arg name - An optional name, used for identifying the object in messages,
  /// output files, etc.
  int klee_int(const char *name);

  /// klee_silent_exit - Terminate the current KLEE process without generating a
  /// test file.
  __attribute__((noreturn))
  void klee_silent_exit(int status);

  /// klee_abort - Abort the current KLEE process.
  __attribute__((noreturn))
  void klee_abort(void);

  /// klee_report_error - Report a user defined error and terminate the current
  /// KLEE process.
  ///
  /// \arg file - The filename to report in the error message.
  /// \arg line - The line number to report in the error message.
  /// \arg message - A string to include in the error message.
  /// \arg suffix - The suffix to use for error files.
  __attribute__((noreturn))
  void klee_report_error(const char *file,
			 int line,
			 const char *message,
			 const char *suffix);

  /* called by checking code to get size of memory. */
  unsigned klee_get_obj_size(void *ptr);

  /* print the tree associated w/ a given expression. */
  void klee_print_expr(const char *msg, ...);

  /* NB: this *does not* fork n times and return [0,n) in children.
   * It makes n be symbolic and returns: caller must compare N times.
   */
  unsigned klee_choose(unsigned n);

  /* special klee assert macro. this assert should be used when path consistency
   * across platforms is desired (e.g., in tests).
   * NB: __assert_fail is a klee "special" function
   */
# define klee_assert(expr)                                              \
  ((expr)                                                               \
   ? (void) (0)                                                         \
   : __assert_fail (#expr, __FILE__, __LINE__, __PRETTY_FUNCTION__))    \

  /* Return true if the given value is symbolic (represented by an
   * expression) in the current state. This is primarily for debugging
   * and writing tests but can also be used to enable prints in replay
   * mode.
   */
  unsigned klee_is_symbolic(uint64_t n);

  /* returns number of bytes starting at 'ptr' that are symbolic */
  /* max_bytes is the numebr of bytes to check before terminating early */
  unsigned klee_sym_range_bytes(void* ptr, unsigned max_bytes);


  /* The following intrinsics are primarily intended for internal use
     and may have peculiar semantics. */

  void klee_assume(uint64_t condition);
  void klee_warning(const char *message);
  void klee_warning_once(const char *message);
  void klee_prefer_cex(void *object, uint64_t condition);
  void klee_mark_global(void *object);

  /* Return a possible constant value for the input expression. This
     allows programs to forcibly concretize values on their own. */
  uint64_t klee_get_value(uint64_t expr);

  /* Ensure that memory in the range [address, address+size) is
     accessible to the program. If some byte in the range is not
     accessible an error will be generated and the state
     terminated.

     The current implementation requires both address and size to be
     constants and that the range lie within a single object. */
  void klee_check_memory_access(const void *address, size_t size);

  /* Enable/disable forking. */
  void klee_set_forking(unsigned enable);

  void klee_stack_trace(void);

  void klee_force_ne(uint64_t expr_lhs, uint64_t expr_rhs);

  void klee_yield(void);
#ifdef __cplusplus
}
#endif

#endif /* __KLEE_H__ */
