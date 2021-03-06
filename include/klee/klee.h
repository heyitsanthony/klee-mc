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
/* OBSOLETE / UNSAFE */
//#define KLEE_SYS_ASSUME		2	/* klee_assume(<bool expr>) */
#define KLEE_SYS_IS_SYM		3	/* klee_is_sym(<expr>)	*/
#define KLEE_SYS_NE		4	/* klee_force_ne */
#define KLEE_SYS_PRINT_EXPR	5	/* klee_print_expr */
#define KLEE_SYS_SILENT_EXIT	6	/* klee_silent_exit */
#define KLEE_SYS_SYM_RANGE_BYTES	7 /* klee_sym_range_bytes */
#define KLEE_SYS_VALID_ADDR	8	/* klee_is_valid_addr */
#define KLEE_SYS_IS_SHADOWED	9	/* klee_is_shadowed */
#define KLEE_SYS_INDIRECT0	10	/* klee_indirect */
#define KLEE_SYS_INDIRECT1	11
#define KLEE_SYS_INDIRECT2	12
#define KLEE_SYS_INDIRECT3	13
#define KLEE_SYS_INDIRECT4	14

#define ksys_report_error(x,y,z,w)	\
	syscall(SYS_klee, KLEE_SYS_REPORT_ERROR, x, y, z, w)
#define ksys_error(x,y)		ksys_report_error(__FILE__, __LINE__, x, y)

#define ksys_sym(x,y) ksys_kmc_symrange(x, y, "")
#define ksys_is_shadowed(x)	syscall(SYS_klee, KLEE_SYS_IS_SHADOWED, x)
#define ksys_kmc_symrange(x,y,z)	\
	syscall(SYS_klee, KLEE_SYS_KMC_SYMRANGE, x, y, z)
#define ksys_is_sym(x)		syscall(SYS_klee, KLEE_SYS_IS_SYM, x)
#define ksys_print_expr(x,y)	syscall(SYS_klee, KLEE_SYS_PRINT_EXPR, (uint64_t)x, y)
#define ksys_silent_exit(x)	syscall(SYS_klee, KLEE_SYS_SILENT_EXIT, (uint64_t)x)
#define ksys_sym_range_bytes(x,y)	\
	syscall(SYS_klee, KLEE_SYS_SYM_RANGE_BYTES, (uint64_t)x, y)
#define ksys_is_valid_addr(x)	syscall(SYS_klee, KLEE_SYS_VALID_ADDR, x)

#define ksys_indirect0(x)	syscall(SYS_klee, KLEE_SYS_INDIRECT0, x)
#define ksys_indirect1(x,y)	syscall(SYS_klee, KLEE_SYS_INDIRECT1, x, y)
#define ksys_indirect2(x,y,z)	syscall(SYS_klee, KLEE_SYS_INDIRECT2, x, y, z)
#define ksys_indirect3(x,y,z,w)	syscall(SYS_klee, KLEE_SYS_INDIRECT3, x, y, z , w)
#define ksys_indirect4(x,y,z,w,q)	syscall(SYS_klee, KLEE_SYS_INDIRECT4, x, y, z , w, q)
#define ksys_assume_eq(x,y)	do {	\
	uint64_t	pred;	\
	pred = ksys_indirect4("__klee_mk_expr", KLEE_CMP_OP_EQ, x, y, 0); \
	ksys_indirect1("__klee_assume", pred); } while (0)

#define ksys_assume_ne(x,y)	do {	\
	uint64_t	pred;	\
	pred = ksys_indirect4("__klee_mk_expr", KLEE_CMP_OP_NE, x, y, 0); \
	ksys_indirect1("__klee_assume", pred); } while (0)


#define ksys_get_value(n)	n
#define ksys_is_active()	(ksys_is_sym(0) != -1)

#ifdef __cplusplus
extern "C" {
#endif

  /* Add an accesible memory object at a user specified location. It
     is the users responsibility to make sure that these memory
     objects do not overlap. These memory objects will also
     (obviously) not correctly interact with external function
     calls. */
  void klee_define_fixed_object(void *addr, size_t nbytes);

  int klee_is_shadowed(uint64_t v);
  /// klee_make_symbolic - Make the contents of the object pointer to by \arg
  /// addr symbolic.
  ///
  /// \arg addr - The start of the object.
  /// \arg nbytes - The number of bytes to make symbolic; currently this *must*
  /// be the entire contents of the object.
  /// \arg name - An optional name, used for identifying the object in messages,
  /// output files, etc.
  void klee_make_symbolic(void *addr, size_t nbytes, const char *name);

  /* returns nonzero if successful */
  uint32_t klee_make_vsym(void *addr, size_t nbytes, const char *name);

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
  __attribute__((noreturn)) void klee_silent_exit(int status);

  /// klee_abort - Abort the current KLEE process.
  __attribute__((noreturn)) void klee_abort(void);

  /// klee_report_error - Report a user defined error and terminate the current
  /// KLEE process.
  ///
  /// \arg file - The filename to report in the error message.
  /// \arg line - The line number to report in the error message.
  /// \arg message - A string to include in the error message.
  /// \arg suffix - The suffix to use for error files.
#define klee_uerror(x, y) klee_report_error(__FILE__, __LINE__, x, y, NULL)
#define klee_uerror_details(x, y, z) klee_report_error(__FILE__, __LINE__, x, y, z)
  __attribute__((noreturn))
  void klee_report_error(const char *file,
			 int line,
			 const char *message,
			 const char *suffix, const void* v);
#define klee_ureport(x, y) klee_report(__FILE__, __LINE__, x, y, NULL)
#define klee_ureport_details(x, y, z) klee_report(__FILE__, __LINE__, x, y, z)
struct kreport_ent { uint64_t strp; uint64_t v; };
#define SET_KREPORT(x,y)	(x)->v = (uint64_t)(y)
#define MK_KREPORT(x)		{ (uint64_t)(x), 0 }
  void klee_report(const char *file,
			 int line,
			 const char *message,
			 const char *suffix, const void* v);


  /* print the tree associated w/ a given expression. */
  void klee_print_expr(const char *msg, ...);

  /* special klee assert macro. this assert should be used when path consistency
   * across platforms is desired (e.g., in tests).
   * NB: __assert_fail is a klee "special" function */
# define klee_assert(expr)                                              \
  ((expr)                                                               \
   ? (void) (0)                                                         \
   : klee_assert_fail ((void*)#expr, (void*)__FILE__, __LINE__, (void*)__PRETTY_FUNCTION__))
  __attribute__((noreturn)) void klee_assert_fail(void* a, void* b, unsigned c, void* d);

  /* Return true if the given value is symbolic (represented by an
   * expression) in the current state. This is primarily for debugging
   * and writing tests but can also be used to enable prints in replay
   * mode. */
  unsigned klee_is_symbolic(uint64_t n);
#define klee_is_symbolic_addr(x) klee_is_symbolic((uint64_t)(x))

  /* return true if byte at given address may be accessed */
  unsigned klee_is_valid_addr(const void* p);

  /* returns number of bytes starting at 'ptr' that are symbolic */
  /* max_bytes is the numebr of bytes to check before terminating early */
  unsigned klee_sym_range_bytes(const void* ptr, unsigned max_bytes);


	void* kmc_sc_regs(void*);
#define KLEE_CMP_OP_EQ	0
#define KLEE_CMP_OP_NE	1
#define KLEE_CMP_OP_UGT	2
#define KLEE_CMP_OP_UGE	3
#define KLEE_CMP_OP_ULT	4
#define KLEE_CMP_OP_ULE	5
#define KLEE_CMP_OP_SGT	6
#define KLEE_CMP_OP_SGE	7
#define KLEE_CMP_OP_SLT	8
#define KLEE_CMP_OP_SLE	9

#if 0
#ifndef __cplusplus
#define BAD_ASSUMES_V
#else
#define BAD_ASSUMES
#endif
#endif

#ifdef BAD_ASSUMES_V
#define klee_assume_ugt(x,y)	__klee_assume(((volatile)((x) > (y))))
#define klee_assume_ult(x,y)	__klee_assume(((volatile)((x) < (y))))
#define klee_assume_uge(x,y)	__klee_assume(((volatile)((x) >= (y))))
#define klee_assume_ule(x,y)	__klee_assume(((volatile)((x) <= (y))))

#define klee_assume_sgt(x,y)	__klee_assume(((volatile)((x) > (y))))
#define klee_assume_slt(x,y)	__klee_assume(((volatile)((x) < (y))))
#define klee_assume_sge(x,y)	__klee_assume(((volatile)((x) >= (y))))
#define klee_assume_sle(x,y)	__klee_assume(((volatile)((x) <= (y))))

#define klee_assume_eq(x,y)	__klee_assume(((volatile)((x) == (y))))
#define klee_assume_ne(x,y)	__klee_assume(((volatile)((x) != (y))))
#else
#ifdef BAD_ASSUMES
#define klee_assume_ugt(x,y)	__klee_assume((x) > (y))
#define klee_assume_ult(x,y)	__klee_assume((x) < (y))
#define klee_assume_uge(x,y)	__klee_assume((x) >= (y))
#define klee_assume_ule(x,y)	__klee_assume((x) <= (y))

#define klee_assume_sgt(x,y)	__klee_assume((x) > (y))
#define klee_assume_slt(x,y)	__klee_assume((x) < (y))
#define klee_assume_sge(x,y)	__klee_assume((x) >= (y))
#define klee_assume_sle(x,y)	__klee_assume((x) <= (y))

#define klee_assume_eq(x,y)	__klee_assume((x) == (y))
#define klee_assume_ne(x,y)	__klee_assume((x) != (y))
#else
#define klee_assume_ugt(x,y)	__klee_assume(klee_mk_ugt(x,y))
#define klee_assume_ult(x,y)	__klee_assume(klee_mk_ult(x,y))
#define klee_assume_uge(x,y)	__klee_assume(klee_mk_uge(x,y))
#define klee_assume_ule(x,y)	__klee_assume(klee_mk_ule(x,y))

#define klee_assume_sgt(x,y)	__klee_assume(klee_mk_sgt(x, y))
#define klee_assume_slt(x,y)	__klee_assume(klee_mk_slt(x, y))
#define klee_assume_sge(x,y)	__klee_assume(klee_mk_sge(x, y))
#define klee_assume_sle(x,y)	__klee_assume(klee_mk_sle(x, y))

#define klee_assume_eq(x, y)	__klee_assume(klee_mk_eq(x, y))
#define klee_assume_ne(x, y)	__klee_assume(klee_mk_ne(x, y))
#endif
#endif
  void __klee_assume(uint64_t expr);

#define KLEE_MK_OP_ITE	10
#define KLEE_MK_OP_AND	11
#define KLEE_MK_OP_OR	12
#define KLEE_MK_OP_XOR	13
#define KLEE_MK_OP_NOT	14

#define klee_mk_expr_s(o,x,y,z)	__klee_mk_expr(\
	o, (int64_t)x, (int64_t)y, (int64_t)z)
#define klee_mk_expr(o,x,y,z)	__klee_mk_expr(\
	o, (uint64_t)x, (uint64_t)y, (uint64_t)z)

#define klee_mk_ugt(x,y)	klee_mk_expr(KLEE_CMP_OP_UGT,x,y,0)
#define klee_mk_ult(x,y)	klee_mk_expr(KLEE_CMP_OP_ULT,x,y,0)
#define klee_mk_uge(x,y)	klee_mk_expr(KLEE_CMP_OP_UGE, x,y,0)
#define klee_mk_ule(x,y)	klee_mk_expr(KLEE_CMP_OP_ULE, x, y, 0)
#define klee_mk_sgt(x,y)	klee_mk_expr_s(KLEE_CMP_OP_SGT, x, y, 0)
#define klee_mk_slt(x,y)	klee_mk_expr_s(KLEE_CMP_OP_SLT, x, y, 0)
#define klee_mk_sge(x,y)	klee_mk_expr_s(KLEE_CMP_OP_SGE, x, y, 0)
#define klee_mk_sle(x,y)	klee_mk_expr_s(KLEE_CMP_OP_SLE, x, y, 0)
#define klee_mk_eq(x, y)	klee_mk_expr(KLEE_CMP_OP_EQ, x, y, 0)
#define klee_mk_ne(x, y)	klee_mk_expr(KLEE_CMP_OP_NE, x, y, 0)
#define klee_mk_ite(x,y,z)	klee_mk_expr(KLEE_MK_OP_ITE, x, y, z)
#define klee_mk_and(x,y)	klee_mk_expr(KLEE_MK_OP_AND, x, y, 0)
#define klee_mk_or(x,y)		klee_mk_expr(KLEE_MK_OP_OR, x, y, 0)
#define klee_mk_xor(x,y)	klee_mk_expr(KLEE_MK_OP_XOR, x, y, 0)
#define klee_mk_not(x)		klee_mk_expr(KLEE_MK_OP_NOT, x, 0, 0)

  uint64_t __klee_mk_expr(
  	uint8_t op, uint64_t arg1, uint64_t arg2, uint64_t arg3);



#define __klee_prefer_op(x,y,z)	klee_prefer_op(((uint64_t)x), ((uint64_t)y), z)
#define klee_prefer_ugt(x,y)	__klee_prefer_op(x,y,KLEE_CMP_OP_UGT)
#define klee_prefer_uge(x,y)	__klee_prefer_op(x,y,KLEE_CMP_OP_UGE)
#define klee_prefer_ule(x,y)	__klee_prefer_op(x,y,KLEE_CMP_OP_ULE)
#define klee_prefer_eq(x, y)	__klee_prefer_op(x, y, KLEE_CMP_OP_EQ)
#define klee_prefer_ne(x, y)	__klee_prefer_op(x, y, KLEE_CMP_OP_NE)
#define klee_prefer_true(x)	klee_prefer_ne(x, 0)
#define klee_prefer_false(x)	klee_prefer_eq(x, 0)


  uint64_t klee_prefer_op(uint64_t lhs, uint64_t rhs, uint8_t op);

#define klee_feasible(x)	__klee_feasible((uint64_t)(x))
  uint64_t __klee_feasible(uint64_t expr);

#define __klee_feasible_op(x,y,z)	\
	__klee_feasible(klee_mk_expr(z, ((uint64_t)x), ((uint64_t)y), 0))
#define klee_feasible_ugt(x,y)	__klee_feasible_op(x,y,KLEE_CMP_OP_UGT)
#define klee_feasible_uge(x,y)	__klee_feasible_op(x,y,KLEE_CMP_OP_UGE)
#define klee_feasible_ult(x,y)	__klee_feasible_op(x,y,KLEE_CMP_OP_ULT)
#define klee_feasible_ule(x,y)	__klee_feasible_op(x,y,KLEE_CMP_OP_ULE)
#define klee_feasible_eq(x, y)	__klee_feasible_op(x, y, KLEE_CMP_OP_EQ)
#define klee_feasible_ne(x, y)	__klee_feasible_op(x, y, KLEE_CMP_OP_NE)

#define klee_valid_ugt(x,y)	(!__klee_feasible_op(x,y, KLEE_CMP_OP_ULE))
#define klee_valid_uge(x,y)	(!__klee_feasible_op(x,y,KLEE_CMP_OP_ULT))
#define klee_valid_ult(x,y)	(!__klee_feasible_op(x,y,KLEE_CMP_OP_UGE))
#define klee_valid_ule(x,y)	(!__klee_feasible_op(x,y,KLEE_CMP_OP_UGT))
#define klee_valid_eq(x, y)	(!__klee_feasible_op(x, y, KLEE_CMP_OP_NE))
#define klee_valid_ne(x, y)	(!__klee_feasible_op(x, y, KLEE_CMP_OP_EQ))

#define klee_valid(x)		klee_valid_ne(((uint64_t)(x)), 0)

/* these are provided by the intrinsic library */
extern void* malloc(unsigned long n) __THROW;
extern void free(void*) __THROW;

  void* klee_malloc_fixed(uint64_t sz);

  void klee_free_fixed(uint64_t x);

  void klee_warning(const char *message);
  void klee_warning_once(const char *message);
  void klee_prefer_cex(void *object, uint64_t condition);
  void klee_mark_global(void *object);

  /* Return a possible constant value for the input expression. This
     allows programs to forcibly concretize values on their own. */
#define klee_get_ptr(p)		(void*)klee_get_value((uintptr_t)p)
  uint64_t klee_get_value(uint64_t expr);

//#define klee_get_value(e)	klee_get_value_pred(e, 1)
  uint64_t klee_get_value_pred(uint64_t expr, uint64_t pred_expr);

  uint64_t klee_min_value(uint64_t expr);
  uint64_t klee_max_value(uint64_t expr);

  /* stores up to 'n' feasible values for 'expr' to 'buf' */
#define klee_get_values(e,b,n) klee_get_values_pred(e,b,n,1)
  int klee_get_values_pred(uint64_t expr, uint64_t* buf, unsigned n, uint64_t pred);


  /* Ensure that memory in the range [address, address+size) is
     accessible to the program. If some byte in the range is not
     accessible an error will be generated and the state
     terminated.

     The current implementation requires both address and size to be
     constants and that the range lie within a single object. */
  void klee_check_memory_access(const void *address, size_t size);

#define klee_fork_all(x)	__klee_fork_all_n((uint64_t)x, ~0)
#define klee_fork_all_n(x, n)	__klee_fork_all_n((uint64_t)x, n)
  uint64_t __klee_fork_all_n(uint64_t v, unsigned n);

#define klee_fork_eq(x,y)	__klee_fork_eq((uint64_t)x, (uint64_t)y)
  int __klee_fork_eq(uint64_t v, uint64_t v2);

  /* Enable/disable forking. */
  void klee_set_forking(unsigned enable);

  void klee_stack_trace(void);
  void klee_constr_dump(uint64_t v);

  void klee_yield(void);

  /* get a concrete object base from possibly symbolic pointer (forks) */
  void* klee_get_obj(void* p);

  /* get concrete object from concrete pointer */
  unsigned klee_get_obj_size(void *ptr);

  uint32_t klee_stack_depth(void);

	/* object following object containing p */
	void* klee_get_obj_next(void* p);

	/* object >= object containing p */
	void* klee_get_obj_prev(void* p);

	uint64_t klee_indirect0(const char* s);
	uint64_t klee_indirect1(const char* s, uint64_t v0);
	uint64_t klee_indirect2(const char* s, uint64_t v0, uint64_t v1);
	uint64_t klee_indirect3(const char* s,
		uint64_t v0, uint64_t v1, uint64_t v2);
	uint64_t klee_indirect4(const char* s,
	uint64_t v0, uint64_t v1, uint64_t v2, uint64_t v3);

	uint32_t klee_constr_count(void);
	// number of unique arrays referenced in expression
	uint32_t klee_arr_count(uint64_t expr);

	uint64_t klee_read_reg(const char* sp);

	int klee_is_readonly(const void* addr);

	void klee_hook_return(uint64_t stack_idx, void* fn, uint64_t aux);

	void* kmc_regs_get(void);
	void kmc_skip_func(void);
	void __kmc_skip_func(void); // do not call this one

	/* MMU stuff */
	uint64_t klee_sym_corehash(void* addr);
#define klee_expr_hash(x)	__klee_expr_hash(((uint64_t)x))
	uint64_t __klee_expr_hash(uint64_t x);
	void klee_tlb_invalidate(const void* addr, uint64_t len);
	void klee_tlb_insert(const void* addr, uint64_t len);
	void klee_enable_softmmu(void);

void klee_concretize_state(uint64_t pred /* optional, set to 0 */);

void	*kmc_alloc_aligned(uint64_t, const char* name);
void	kmc_exit(uint64_t);
void	kmc_free_run(uint64_t addr, uint64_t num_bytes);
void	kmc_make_range_symbolic(uint64_t addr, uint64_t len, const char* name);

int	kmc_ossfx_load(/*unsigned sc_seq*/);
#if 0
int	kmc_ossfx_regs(unsigned sc_seq, void* buf);
/* is this a good interface? */
void	*kmc_ossfx_next_mem(
	unsigned sc_seq,
	void* buf, unsigned len, unsigned *out_len);
#endif

#ifdef __cplusplus
}
#endif

#endif /* __KLEE_H__ */
