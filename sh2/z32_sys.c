/* Zelda 3 32X - the few newlib system calls malloc and friends need (no files, no console). */
#include <errno.h>
#include <stddef.h>
#include <sys/stat.h>
#undef errno
extern int errno;
extern char __heap_start[], __heap_end[];
static char *brk_p;
char *z32_heap_peak;
void *_sbrk(ptrdiff_t incr) {
  if (!brk_p) brk_p = __heap_start;
  if (brk_p + incr > __heap_end || brk_p + incr < __heap_start) { errno = ENOMEM; return (void *)-1; }
  char *p = brk_p; brk_p += incr;
  if (brk_p > z32_heap_peak) z32_heap_peak = brk_p;
  return p;
}
void _exit(int c) { (void)c; for (;;) { } }
int _close(int f) { (void)f; return -1; }
int _fstat(int f, struct stat *s) { (void)f; s->st_mode = S_IFCHR; return 0; }
int _isatty(int f) { (void)f; return 1; }
int _lseek(int f, int o, int w) { (void)f; (void)o; (void)w; return 0; }
int _read(int f, char *b, int n) { (void)f; (void)b; (void)n; return 0; }
int _write(int f, const char *b, int n) { (void)f; (void)b; return n; }
int _open(const char *n, int fl, int m) { (void)n; (void)fl; (void)m; errno = ENOENT; return -1; }
int _kill(int p, int s) { (void)p; (void)s; return -1; }
int _getpid(void) { return 1; }
int _gettimeofday(void *t, void *z) { (void)t; (void)z; return -1; }
int _unlink(const char *n) { (void)n; return -1; }
int _link(const char *a, const char *b) { (void)a; (void)b; return -1; }
