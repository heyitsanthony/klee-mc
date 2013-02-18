#include "klee/klee.h"
#include "mem.h"
#include "syscalls.h"

int nt_qvm(void* base, PMEMORY_BASIC_INFORMATION mb)
{
	void		*obj, *cur_obj;
	uint64_t	base_c;
	unsigned	sz, cur_sz;

	klee_print_expr("[qvm] sym base", base);

	klee_fork_all(((uintptr_t)base & ~0xfff));

	base_c = klee_get_value((uint64_t)base);
	klee_assume_eq(base, base_c);
	base = (void*)base_c;
	
	obj = klee_get_obj_prev(base);
	if (obj == NULL) {
		klee_print_expr("[qvm] No obj for base", base);
		return 0;
	}

	sz = klee_get_obj_size(obj);
	klee_assert ((uintptr_t)obj <= (uintptr_t)base);
	if (((uintptr_t)obj + sz) < (uintptr_t)base) {
		unsigned	len;

		cur_obj = klee_get_obj_next(base);
		klee_assert (cur_obj != NULL);

		len = (ptrdiff_t)((char*)cur_obj - ((char*)obj+sz));

		mb->BaseAddress = (uint32_t)((char*)obj+sz);
		mb->AllocationBase = 0;
		mb->AllocationProtect = (uintptr_t)PAGE_NOACCESS;
		mb->RegionSize = len;
		mb->State =  MEM_FREE;
		mb->Protect = PAGE_NOACCESS;
		mb->Type = 0;
		return 1;
	}
	
	/* XXX: need way to access read access to obj */

	/* fill out pages going forward */
	cur_sz = 0;
	do {
		sz += cur_sz;
		cur_obj = klee_get_obj_next((char*)obj+sz);
		cur_sz = klee_get_obj_size(cur_obj);
	} while (((char*)obj + sz) == cur_obj);
	
	/* fill out pages going backward */
	cur_obj = obj;
	do {
		sz += (ptrdiff_t)((char*)obj - (char*)cur_obj);
		obj = cur_obj;
		cur_obj = klee_get_obj_prev((char*)obj-1);
		cur_sz = klee_get_obj_size(cur_obj);
	} while (((char*)cur_obj + cur_sz) == obj);

	mb->BaseAddress = (uintptr_t)obj;
	mb->AllocationBase = (uintptr_t)obj;
	mb->AllocationProtect = (uintptr_t)PAGE_READWRITE;
	mb->RegionSize = sz;
	mb->State =  MEM_COMMIT;
	mb->Protect = PAGE_READWRITE;	/* XXX uhhh */ 
	mb->Type = MEM_PRIVATE;		/* XXX uhhh */ 

	return 1;
}

void* nt_avm(
	void* prefer,
	uint32_t len,
	uint32_t alloc_ty,	
	uint32_t prot)
{
	int		is_himem;

	/* XXX: TODO HONOR 'prot' */

	if ((alloc_ty & MEM_COMMIT) == 0) return prefer;

	if (len <= 4096)
		len = concretize_u64(len);
	else if (len <= 16*1024)
		len = concretize_u64(len);
	else
		len = concretize_u64(len);

	/* mapping may be placed anywhere */
	if (prefer == 0)
		return kmc_alloc_aligned(len, "NtAllocateVirtualMemory");


	is_himem = (((intptr_t)prefer & ~0x7fffffffULL) != 0);
	if (!is_himem) {
		/* not highmem, use if we've got it.. */
		unsigned	i;
		prefer = concretize_ptr(prefer);
		// klee_print_expr("non-highmem define fixed addr", addr);
		for (i = 0; i < (len+4095)/4096; i++)
			klee_define_fixed_object(
				(char*)prefer+(i*4096), 4096);
		return prefer;
	}

	/* can never satisfy a hi-mem request */
	//if (flags & MAP_FIXED) return NULL;

	klee_print_expr("[NT] unfixed alloc", prefer);

	/* toss back whatever */
	return kmc_alloc_aligned(len, "NtAllocateVirtualMemory");
}
