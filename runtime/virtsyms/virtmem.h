#ifndef VIRTSYM_MEM_H
#define VIRTSYM_MEM_H


#define vm_is_conc(x)	((x)->vm_first_sym_idx == ~0U)
struct virt_mem
{
	uint8_t		*vm_buf; // buffer containing raw buffer
	unsigned	vm_first_sym_idx; // ~0 => conc
	unsigned	vm_len_min, vm_len_max; // lengths of buffer
};

void virtsym_mem_free(struct virt_mem*);
#define virtsym_safe_memcopy(s, len) virtsym_safe_memcopy_all(s, len, 0)
#define virtsym_safe_memcopy_conc(s, len) virtsym_safe_memcopy_all(s, len, 1)
struct virt_mem* virtsym_safe_memcopy_all(const void* s,  unsigned len, int copy_conc);

//int virtsym_mem_eq(const struct virt_mem*, const struct virt_mem*);
//uint64_t virtsym_mem_hash(const struct virt_mem*);


#endif
