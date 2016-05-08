#include "klee/klee.h"
#include "virtmem.h"


void virtsym_mem_free(struct virt_mem* vm)
{
	free(vm->vm_buf);
	free(vm);
}

struct virt_mem* virtsym_safe_memcopy_all(
	const void *buf, unsigned len, int copy_conc)
{
	struct virt_mem	*vm;
	unsigned	i;
	const uint8_t	*buf8;

	/* concretize because we're dum  (XXX smarter way to do this?) */
	klee_assume_eq((uint64_t)buf, klee_get_value((uint64_t)buf));
	if (!klee_is_valid_addr(buf))
		return NULL;

	// XXX expensive
	klee_assume_eq(len, klee_max_value(len));

	buf8 = (const uint8_t*)buf;
	for (i = 0; i < len; i++) {
		if (!klee_is_valid_addr(&buf8[i]))
			return NULL;
		if (klee_is_symbolic(buf8[i]))
			break;
	}

	if (i == len) {
		// buffer is completely concrete
		if (!copy_conc) {
			// not copying; bail
			return NULL;
		}
		// no symbolic index
		i = ~0U;
	}

	vm = malloc(sizeof(struct virt_mem));
	vm->vm_first_sym_idx = i;
	if (len > 0) {
		vm->vm_buf = malloc(len);
		for (i = 0; i < len; i++) {
			vm->vm_buf[i] = ((const uint8_t*)buf8)[i];
		}
		vm->vm_len_min = i;
		vm->vm_len_max = len;
	} else {
		vm->vm_buf = NULL;
		vm->vm_len_min = 0;
		vm->vm_len_max = 0;
	}

	return vm;
}
