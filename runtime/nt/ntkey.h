#ifndef NTKEY_H
#define NTKEY_H
#include <stdint.h>

typedef enum _KEY_INFORMATION_CLASS { 
KeyBasicInformation           = 0,
KeyNodeInformation            = 1,
KeyFullInformation            = 2,
KeyNameInformation            = 3,
KeyCachedInformation          = 4,
KeyFlagsInformation           = 5,
KeyVirtualizationInformation  = 6,
KeyHandleTagsInformation      = 7,
MaxKeyInfoClass               = 8
} KEY_INFORMATION_CLASS;


typedef struct _KEY_NAME_INFORMATION {
	uint32_t NameLength;
	uint16_t Name[1];
} KEY_NAME_INFORMATION, *PKEY_NAME_INFORMATION;



#endif
