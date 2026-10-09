/* roomrom_rng.c — byte-for-byte port of Z_07.asm:499-515 @ScrambleRandom.
 *
 * NES asm:
 *   LDX #$18 ; Y = $0D ; LDA $00,X ; AND #$02 ; STA $00
 *   LDA $01,X ; AND #$02 ; EOR $00 ; CLC
 *   BEQ @LoopRandom ; SEC
 *   @LoopRandom: ROR $00,X ; INX ; DEY ; BNE @LoopRandom
 *
 * Translation: bit 1 of Random[0] EOR bit 1 of Random[1] -> carry-in.
 * Then ROR carry through the 13 bytes Random[0..12].
 */

#include "rng_state.h"

void rng_seed(unsigned short seed)
{
    unsigned char i;
    /* NES ClearRam seed (Z_05.asm:7411-7431): zeroes $00..$EF then
     * explicitly stores #$40 at Random ($0018). Random[1..12] stays $00.
     * @ScrambleRandom (Z_07.asm:499) propagates the $40 bit through the
     * ROR-chain on subsequent frames. To get bytewise RNG parity with
     * NES BizHawk we MUST seed the same way — anything else (e.g.
     * 0xACE1-derived pattern) diverges on frame 0.
     * 'seed' argument retained for caller compat but ignored. */
    (void)seed;
    for (i = 0; i < RNG_RANDOM_LEN; i++) {
        RAM(RNG_RANDOM_BASE + i) = (i == 0u) ? 0x40u : 0x00u;
    }
    /* Same store: CurEdgeSpawnCell ($525), where FindNextEdgeSpawnCell
     * first looks (T-012: left 0, the first edge monster spawned off
     * the top of the room). */
    RAM(0x0525u) = 0x40u;
}

static void rng_scramble(void)
{
    /* Reproduce @ScrambleRandom tap+ROR-chain exactly. */
    unsigned char b0 = RAM(RNG_RANDOM_BASE + 0) & 0x02;
    unsigned char b1 = RAM(RNG_RANDOM_BASE + 1) & 0x02;
    unsigned char carry_in = (b0 ^ b1) ? 1 : 0;
    unsigned char i;
    for (i = 0; i < RNG_RANDOM_LEN; i++) {
        unsigned char v = RAM(RNG_RANDOM_BASE + i);
        unsigned char new_carry = v & 0x01;
        v = (unsigned char)((v >> 1) | (carry_in ? 0x80 : 0x00));
        RAM(RNG_RANDOM_BASE + i) = v;
        carry_in = new_carry;
    }
}

unsigned short rng_next(void)
{
    rng_scramble();
    return (unsigned short)(((unsigned short)RAM(RNG_RANDOM_BASE + 1) << 8)
                            | (unsigned short)RAM(RNG_RANDOM_BASE + 0));
}

unsigned short rng_peek(void)
{
    return (unsigned short)(((unsigned short)RAM(RNG_RANDOM_BASE + 1) << 8)
                            | (unsigned short)RAM(RNG_RANDOM_BASE + 0));
}
