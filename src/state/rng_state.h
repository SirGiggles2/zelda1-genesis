#ifndef ROOMROM_RNG_H
#define ROOMROM_RNG_H

/* Phase 7 Task 7.1 framework. Byte-for-byte port of NES Z1 RNG.
 *
 * NES truth: reference/aldonunez/Variables.inc:8 — Random := $18.
 *            reference/aldonunez/Z_07.asm:499-515 — @ScrambleRandom
 *            taps bit 1 of $18 and $19, EORs them, ROR-chains carry
 *            through 13 bytes ($18..$24).
 *
 * Debate 2026-05-09 verdict Q2=(a): byte-for-byte port. Codex / Gemini /
 *   Sonnet / Opus 4-of-4 unanimous. Widening to 16/32-bit state breaks
 *   slot-indexed reads at Z_04.asm:1221, :11209, :11866 (LDA Random,X
 *   in Wizzrobe align, drop tables, AI direction picks).
 *
 * Master plan signature contract (lines 1251-1255):
 *   void rng_seed(uint16_t seed)
 *   uint16_t rng_next(void)
 *   uint16_t rng_peek(void)
 *   #define PROBE_RNG_SEED ((volatile uint8_t*)...)
 *
 * Internal state IS the 13-byte Random[] array at NES $18..$24
 * (= M68K $FF0018..$FF0024 via nes_ram[]). uint16_t API is for probe
 * ergonomics; bottom byte = Random[0], top byte = Random[1].
 */

#include "platform_abi.h"

#define RNG_RANDOM_BASE   0x0018
#define RNG_RANDOM_LEN    13
#define PROBE_RNG_SEED    ((volatile unsigned char *)&RAM(RNG_RANDOM_BASE))

/* Distribute the 16-bit seed across Random[0..12]. Any nonzero pattern
 * works — NES BootROM writes the seed via @ScrambleRandom warmup. */
void rng_seed(unsigned short seed);

/* Run one @ScrambleRandom step and return Random[1..0] as 16-bit. */
unsigned short rng_next(void);

/* Read current Random[1..0] without advancing state. */
unsigned short rng_peek(void);

#endif
