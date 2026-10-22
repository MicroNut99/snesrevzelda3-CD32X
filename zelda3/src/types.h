#ifndef ZELDA3_TYPES_H_
#define ZELDA3_TYPES_H_

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

// Build time config options
enum {
  kEnableLargeScreen = 0,      // Z32X: no widescreen margins (the PPU's line buffers are 256 wide)
  // How much extra spacing to add on the sides
  kPpuExtraLeftRight = kEnableLargeScreen ? 96 : 0,
};

typedef uint8_t uint8;
typedef int8_t int8;
typedef uint16_t uint16;
typedef int16_t int16;
typedef uint32_t uint32;
typedef int32_t int32;
typedef uint64_t uint64;
typedef int64_t int64;
typedef unsigned int uint;

#define arraysize(x) sizeof(x)/sizeof(x[0])
#define sign8(x) ((x) & 0x80)
#define sign16(x) ((x) & 0x8000)
#define load24(x) ((*(uint32*)&(x))&0xffffff)

#ifdef _MSC_VER
#define countof _countof
#define NORETURN __declspec(noreturn)
#define FORCEINLINE __forceinline
#define NOINLINE __declspec(noinline)
#else
#define countof(a) (sizeof(a)/sizeof(*(a)))
#define NORETURN
#define FORCEINLINE inline
#define NOINLINE
#endif

#ifdef _DEBUG
#define kDebugFlag 1
#else
#define kDebugFlag 0
#endif

static FORCEINLINE uint16 abs16(uint16 t) { return sign16(t) ? -t : t; }
static FORCEINLINE uint8 abs8(uint8 t) { return sign8(t) ? -t : t; }
static FORCEINLINE int IntMin(int a, int b) { return a < b ? a : b; }
static FORCEINLINE int IntMax(int a, int b) { return a > b ? a : b; }
static FORCEINLINE uint UintMin(uint a, uint b) { return a < b ? a : b; }
static FORCEINLINE uint UintMax(uint a, uint b) { return a > b ? a : b; }

// windows.h defines this too
#ifdef HIBYTE
#undef HIBYTE
#endif

/* Z32X (CD32X port): the SNES's RAM and data are little-endian and words sit at any address.
 * The SH-2 is big-endian and cannot read a word at an odd address, so every 16-/32-bit value
 * in g_ram, the save RAM and the game data is read through these little-endian, alignment-1
 * types (GCC scalar_storage_order). On a little-endian PC they are the same bytes as before. */
#ifdef __cplusplus
/* C++ (the Z32X build): GCC ignores scalar_storage_order in C++, so these are small classes -
 * two bytes, alignment 1, little-endian, read and written through conversions. Every pointer
 * into SNES memory is one of these; mixing them with native pointers is a compile error. */
template <typename T> struct __attribute__((packed, may_alias)) LeInt {
  uint8 b[sizeof(T)];
  operator T() const { uint32 v = 0; for (unsigned i = 0; i < sizeof(T); i++) v |= (uint32)b[i] << (8 * i); return (T)v; }
  LeInt &operator=(T v) { for (unsigned i = 0; i < sizeof(T); i++) b[i] = (uint8)((uint32)v >> (8 * i)); return *this; }
  LeInt &operator+=(int v) { return *this = (T)(*this + v); }
  LeInt &operator-=(int v) { return *this = (T)(*this - v); }
  LeInt &operator|=(int v) { return *this = (T)(*this | v); }
  LeInt &operator&=(int v) { return *this = (T)(*this & v); }
  LeInt &operator^=(int v) { return *this = (T)(*this ^ v); }
  LeInt &operator<<=(int v) { return *this = (T)(*this << v); }
  LeInt &operator>>=(int v) { return *this = (T)(*this >> v); }
  LeInt &operator*=(int v) { return *this = (T)(*this * v); }
  LeInt &operator++() { return *this = (T)(*this + 1); }
  LeInt &operator--() { return *this = (T)(*this - 1); }
  T operator++(int) { T o = *this; *this = (T)(o + 1); return o; }
  T operator--(int) { T o = *this; *this = (T)(o - 1); return o; }
};
typedef LeInt<uint16> le16;
typedef LeInt<int16> lei16;
typedef LeInt<uint32> le32;
#define LE_SSO __attribute__((packed, may_alias))
#define LE16(p) (*(le16 *)(p))
#define LEI16(p) (*(lei16 *)(p))
#define LE32(p) (*(le32 *)(p))
#define LEA16(p) ((le16 *)(p))
#define LEAI16(p) ((lei16 *)(p))
#define U16LE le16                     /* a little-endian field in a struct over SNES memory */
/* Z32X: a native table of the code (not SNES memory) copied into SNES memory, word by word;
 * a byte of a native table as the SNES would see it (little-endian) */
static inline void CopyWordsToLe(void *dst, const uint16 *src, size_t n) {
  le16 *d = (le16 *)dst;
  for (size_t i = 0; i < n; i++) d[i] = src[i];
}
static inline void CopyWordsToLe(void *dst, const int16 *src, size_t n) { CopyWordsToLe(dst, (const uint16 *)src, n); }
static inline uint8 NativeLeByte(const uint16 *t, size_t k) { return (uint8)(t[k >> 1] >> (8 * (k & 1))); }
static inline uint8 NativeLeByte(const int16 *t, size_t k) { return NativeLeByte((const uint16 *)t, k); }
typedef le16 vram16;                   /* the PPU's VRAM words (snes/ppu.c) */
#define I16LE lei16
#else
#define LE_SSO __attribute__((scalar_storage_order("little-endian"), packed, may_alias))
#define U16LE uint16
typedef uint16 vram16;
#define I16LE int16
#endif
#ifndef __cplusplus
typedef struct LE_SSO LeU16 { uint16 v; } LeU16;
typedef struct LE_SSO LeI16 { int16 v; } LeI16;
typedef struct LE_SSO LeU32 { uint32 v; } LeU32;
typedef struct LE_SSO LeA16 { uint16 a[0x8000]; } LeA16;    /* an array of LE words */
typedef struct LE_SSO LeAI16 { int16 a[0x8000]; } LeAI16;
#define LE16(p) (((LeU16 *)(p))->v)
#define LEI16(p) (((LeI16 *)(p))->v)
#define LE32(p) (((LeU32 *)(p))->v)
#define LEA16(p) (((LeA16 *)(p))->a)
#define LEAI16(p) (((LeAI16 *)(p))->a)
#endif

/* Z32X: little-endian SNES words (g_ram) into a native uint16 array (the PPU's OAM / CGRAM) */
static inline void CopyLeToWords(uint16 *dst, const void *src, size_t n) {
  const uint8 *s = (const uint8 *)src;
  for (size_t i = 0; i < n; i++) dst[i] = (uint16)(s[2 * i] | s[2 * i + 1] << 8);
}

/* Z32X: the low / high byte of a little-endian value in place (g_ram, VRAM: le16 lvalues) */
#define BYTE_LE(x) (((uint8 *)&(x))[0])
#define HIBYTE_LE(x) (((uint8 *)&(x))[1])

/* Z32X: native-order parts of a native (not g_ram) value - tools/endian_fix.py writes these */
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define BYTE_N(x) (((uint8 *)&(x))[sizeof(x) - 1])
#define HIBYTE_N(x) (((uint8 *)&(x))[sizeof(x) == 1 ? 1 : sizeof(x) - 2])
#define WORD_LO_N(x) (*(uint16 *)((uint8 *)&(x) + sizeof(x) - 2))
#else
#define BYTE_N(x) (((uint8 *)&(x))[0])
#define HIBYTE_N(x) (((uint8 *)&(x))[1])
#define WORD_LO_N(x) (*(uint16 *)&(x))
#endif

#define BYTE(x) (*(uint8*)&(x))
#define HIBYTE(x) (((uint8*)&(x))[1])
#define WORD(x) (*(uint16*)&(x))
#define DWORD(x) (*(uint32*)&(x))
#define XY(x, y) ((y)*64+(x))

#ifndef swap16
static inline uint16 swap16(uint16 v) { return (v << 8) | (v >> 8); }
#endif

typedef struct Point16U {
  uint16 x, y;
} Point16U;

typedef struct PointU8 {
  uint8 x, y;
} PointU8;

typedef struct Pair16U {
  uint16 a, b;
} Pair16U;

typedef struct PairU8 {
  uint8 a, b;
} PairU8;

typedef struct ProjectSpeedRet {
  uint8 x, y;
  uint8 xdiff, ydiff;
} ProjectSpeedRet;

typedef struct LE_SSO OamEnt {
  uint8 x, y, charnum, flags;
} OamEnt;

typedef struct MemBlk {
  const uint8 *ptr;
  size_t size;
} MemBlk;
MemBlk FindIndexInMemblk(MemBlk data, size_t i);

void NORETURN Die(const char *error);

#endif  // ZELDA3_TYPES_H_
