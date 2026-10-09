/* c_runtime.h — public interface for Stage 2b smoke probe and (later)
 * the broader C runtime support layer.
 *
 * No stdint.h: the vendored SGDK toolchain ships without GCC builtin
 * headers. Primitive types are used directly. On m68k-elf:
 *   unsigned char  = 8 bits
 *   unsigned short = 16 bits
 *   unsigned int   = 32 bits
 *   unsigned long  = 32 bits
 */
#ifndef C_RUNTIME_H
#define C_RUNTIME_H

#ifdef __cplusplus
extern "C" {
#endif

/* VBlank-tick heartbeat. Called from VBlankISR in genesis_shell.asm
 * after jsr IsrNmi. Void, no args — standard SysV m68k ABI. */
extern void c_probe_tick(void);

/* Observable counter. Linker pins to $FFC000+ via build/genesis.ld.
 * Find its actual address in Debug.lst or the ELF symbol table. */
extern volatile unsigned int c_probe_counter;

#ifdef __cplusplus
}
#endif

#endif /* C_RUNTIME_H */
