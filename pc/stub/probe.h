/* endian_fix.py pass 1: the warning names char (*)[id*16 + sizeof(x)] for each use */
#define WPROBE(id, x) (*(__extension__({ char (*_wp)[(id) * 16 + sizeof(x)] = 1; (void)_wp; (uint16 *)&(x); })))
#define BYTE_N(x) BYTE(x)
#define HIBYTE_N(x) HIBYTE(x)
