/* src/stdint.h — minimal stdint shim for SGDK m68k-elf toolchain.
 * The vendored gcc ships no GCC builtin headers. This provides just
 * the fixed-width types used by the native FS source files.
 * On m68k-elf: char=8, short=16, int=32, long=32.
 */
#ifndef STDINT_H
#define STDINT_H

typedef unsigned char  uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int   uint32_t;
typedef signed char    int8_t;
typedef signed short   int16_t;
typedef signed int     int32_t;

#endif /* STDINT_H */
