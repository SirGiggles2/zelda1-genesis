/* Phase 8 Task 8.4 — Manhandla callee shims.
 *
 * NES source: Z_04.asm:7747 InitManhandla + Z_04.asm:7842 UpdateManhandla
 *             (full segment loop, fireball gate, mirrored draw fork).
 * Drained C:  src/oracle/enemies/enemy_manhandla_runtime.c — INIT +
 *             UPDATE + helpers (set_all_segments_direction /
 *             check_collisions / move / draw) all PRIMARY.
 * Coverage:   FULL — drain carries every Z_04 UpdateManhandla branch.
 * Stance:     ADOPT — wire enrt_init_manhandla / enrt_update_manhandla
 *             into enemy_loop.c row $3C (INIT + UPDATE). This TU exists
 *             only to resolve the four extern primitives the drain
 *             references via enemy_runtime_private.h that don't have a
 *             c_*-prefixed twin in the existing bridges:
 *               c_turn_randomly_dir8        (NES Z_04.asm:11859)
 *               c_play_boss_hit_cry_if_needed (drained native dispatcher)
 *               c_play_boss_death_cry         (drained native dispatcher)
 *               c_draw_object_mirrored        (frame=0, slot)
 */

#include "boss_manhandla.h"
#include "platform_abi.h"
#include "enemy_state.h"
#include "enemies/enemy_dispatch.h"      /* enemy_play_boss_*_cry */
#include "world/draw_dispatch.h"         /* draw_object_mirrored */

extern const unsigned char Directions8[8];

/* ---- c_turn_randomly_dir8 (NES Z_04.asm:11859) ----------------------
 * Y = GetObjDir8Index(slot); A = ENEMY_RNG_B(slot);
 *   if (A >= $A0) ; don't turn
 *   elif (A >= $50) Y++   ; turn right
 *   else            Y -= 2 ; turn left  (== Y - 2 mod 8)
 *   ENEMY_DIR(slot) = Directions8[Y & 7]                              */
void c_turn_randomly_dir8(unsigned int slot)
{
    unsigned char dir = (unsigned char)ENEMY_DIR(slot);
    unsigned int idx = 0u;
    {
        signed char y;
        for (y = 7; y >= 0; --y) {
            if (Directions8[(unsigned int)y] == dir) {
                idx = (unsigned int)y;
                break;
            }
        }
    }
    {
        const unsigned char rnd = (unsigned char)ENEMY_RNG_B(slot);
        if (rnd >= 0xA0u) {
            /* don't turn */
        } else if (rnd >= 0x50u) {
            idx = (idx + 1u) & 7u;       /* turn right */
        } else {
            /* NES INY, then DEY DEY: net Y-1 (T-147: was Y-2; t129
             * Digdogger turned $02 -> $04 where NES turned to $06). */
            idx = (idx + 7u) & 7u;       /* turn left == Y-1 mod 8 */
        }
    }
    ENEMY_DIR(slot) = Directions8[idx];
}

/* ---- c_play_boss_hit_cry_if_needed → enemy_play_boss_hit_cry_if_needed
 * Drained native dispatcher in src/game/enemies/enemy_dispatch.c. */
void c_play_boss_hit_cry_if_needed(unsigned int slot)
{
    enemy_play_boss_hit_cry_if_needed(slot);
}

/* ---- c_play_boss_death_cry → enemy_play_boss_death_cry
 * Drained native dispatcher in src/game/enemies/enemy_dispatch.c. */
void c_play_boss_death_cry(void)
{
    enemy_play_boss_death_cry();
}

/* ---- c_draw_object_mirrored → draw_object_mirrored(frame=0, slot)
 * NES DrawObjectMirrored(slot) calls the frame-aware variant with
 * frame=0 implicitly via the [00] scratch byte being zeroed pre-call.
 * Same semantics as cave_runtime.c uses. */
void c_draw_object_mirrored(unsigned int slot)
{
    draw_object_mirrored(0u, slot);
}
