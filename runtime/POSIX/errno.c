int errno = 0;

int* __errno_location(void) { return &errno; }