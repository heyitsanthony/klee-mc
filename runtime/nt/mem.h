#ifndef NT_MEM_H
#define NT_MEM_H
typedef struct _MEMORY_BASIC_INFORMATION {
uint32_t BaseAddress;
uint32_t AllocationBase;
uint32_t AllocationProtect;
uint32_t RegionSize;
uint32_t State;
uint32_t Protect;
uint32_t Type;
} MEMORY_BASIC_INFORMATION, *PMEMORY_BASIC_INFORMATION;

#define PAGE_NOACCESS	0x01
#define PAGE_READONLY	0x02
#define PAGE_READWRITE	0x04
#define PAGE_EXECUTE_READ       0x20
#define PAGE_EXECUTE_READWRITE  0x40
#define PAGE_EXECUTE_WRITECOPY  0x80
#define PAGE_GUARD              0x100
#define PAGE_NOCACHE            0x200
#define PAGE_WRITECOMBINE       0x400
#define MEM_COMMIT		0x1000
#define MEM_PRIVATE		0x20000
#define MEM_MAPPED		0x40000
#define MEM_FREE		0x10000

struct UNICODE_STRING
{
uint16_t Length;
uint16_t MaximumLength;
uint32_t Buffer;
};

enum {
MemoryBasicInformation,
MemoryWorkingSetList,
MemorySectionName,
MemoryBasicVlmInformation
} MEMORY_INFORMATION_CLASS;

int nt_qvm(void* base_addr, PMEMORY_BASIC_INFORMATION mb);

void* nt_avm(
	void* prefer,
	uint32_t sz,
	uint32_t alloc_ty,	
	uint32_t prot);

#endif
