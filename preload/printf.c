//#include <stdio.h>
#include <stdarg.h>
#include <wchar.h>

int ___printf_chk(const char *format, ...) { return 1; }
int __printf_chk(const char *format, ...) { return 1; }
int printf(const char *format, ...) { return 1; }
int fprintf(FILE *stream, const char *format, ...) { return 1; }
int vprintf(const char *format, va_list ap) { return 1; }
int vfprintf(FILE *stream, const char *format, va_list ap) { return 1; }
int fputc(int c, FILE *stream) { return 1; }
int fputs(const char *s, FILE *stream) { return 1; }
int putc(int c, FILE *stream) { return 1; }
int putchar(int c) { return 1; }
int puts(const char *s) { return 1; }

int __printf(const char *format, ...) { return 1; }
int __fprintf(FILE *stream, const char *format, ...) { return 1; }
int __vprintf(const char *format, va_list ap) { return 1; }
int __vfprintf(FILE *stream, const char *format, va_list ap) { return 1; }
int __fputc(int c, FILE *stream) { return 1; }
int __fputs(const char *s, FILE *stream) { return 1; }
int __putc(int c, FILE *stream) { return 1; }
int __putchar(int c) { return 1; }
int __puts(const char *s) { return 1; }
int _IO_new_file_overflow(FILE* f, short w) { return 1; }

int wprintf(const wchar_t *format, ...) { return 1; }
int fwprintf(FILE *stream, const wchar_t *format, ...) { return 1; }
int vwprintf(const wchar_t *format, va_list args) { return 1; }
int vfwprintf(FILE *stream, const wchar_t *format, va_list args) { return 1; }

wint_t fputwc(wchar_t wc, FILE *stream) { return 1; }
wint_t putwc(wchar_t wc, FILE *stream) { return 1; }

int putc_unlocked(int c, FILE *stream) { return 1; }
int putchar_unlocked(int c) { return 1; }

int feof_unlocked(FILE *stream) { return 1; }
int ferror_unlocked(FILE *stream) { return 1; }
int fileno_unlocked(FILE *stream) { return 1; }
int fflush_unlocked(FILE *stream) { return 1; }
int fputc_unlocked(int c, FILE *stream) { return 1; }

int fputs_unlocked(const char *s, FILE *stream) { return 1; }

wint_t fputwc_unlocked(wchar_t wc, FILE *stream) { return 1; }
wint_t putwc_unlocked(wchar_t wc, FILE *stream) { return 1; }
wint_t putwchar_unlocked(wchar_t wc) { return 1; }
int fputws_unlocked(const wchar_t *ws, FILE *stream) { return 1; }

int _IO_vfprintf_internal(FILE *stream, const char *format, va_list ap) { return 1; }
int _GI___vfprintf_chk(FILE *stream, const char *format, va_list ap) { return 1; }
int __GI___vfprintf_chk(FILE *stream, const char *format, va_list ap) { return 1; }
int _GI___vfprintf(FILE *stream, const char *format, va_list ap) { return 1; }
int __GI___vfprintf(FILE *stream, const char *format, va_list ap) { return 1; }
int __GI_vfprintf(FILE *stream, const char *format, va_list ap) { return 1; }

int _IO_fprintf(FILE *stream, const char *format, ...) { return 1; }
int _IO_vfprintf(FILE *stream, const char *format, va_list ap) { return 1; }
int ___fprintf_chk(FILE *stream, const char *format, ...) { return 1; }
int __fprintf_chk(FILE *stream, const char *format, ...) { return 1; }
int __vfprintf_chk(FILE *stream, const char *format, ...) { return 1; }


