int errno __attribute__((weak)) = 0;

int* __errno_location(void) { return &errno; }