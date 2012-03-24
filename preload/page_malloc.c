#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#define GET_BASE(x)	((void*)(((size_t*)x) - 1))
#define GET_SZ(x)	*(((size_t*)x) - 1)
#define SET_SZ(x,y)	((size_t*)x)[-1] = y

void *malloc(size_t size)
{
	void	*ret;

	size += sizeof(size);
	ret = mmap(
		NULL,
		size,
		PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON,
		-1,
		0);
	
	if (ret == MAP_FAILED)
		return NULL;

	return ret;
}

void free(void *ptr)
{
	size_t	sz;
	
	if (ptr == NULL) return;

	sz = GET_SZ(ptr);
	sz = 4096*((sz + 4095)/4096);
	munmap(GET_BASE(ptr), sz);
}

void *calloc(size_t nmemb, size_t size)
{
	void*	ret;
	size_t	byte_c;

	byte_c = nmemb * size;
	ret = malloc(byte_c);
	memset(ret, 0, byte_c);

	return ret;
}

void *realloc(void *ptr, size_t size)
{
	void*	ret;

	if (ptr == NULL) return NULL;

	ret = malloc(size);
	if (ret == NULL) goto done;

	memcpy(ret, ptr, (GET_SZ(ptr) > size) ? size : GET_SZ(ptr));
done:
	free(ptr);
	return ret;
}
