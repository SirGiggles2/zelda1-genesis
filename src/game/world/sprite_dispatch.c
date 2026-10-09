/* sprite_dispatch.c — native sprite subsystem dispatch (Phase 4).
 *
 * Drain MATCH per finding 4_3n. Inlines core helper
 * `z01_reset_cur_sprite_index` (NES Z_01.asm:3095) per NES semantics
 * to keep src/game/ free of transpile shims.
 */

#include "sprite_dispatch.h"
#include "platform_abi.h"      /* RAM */
#include "room_state.h"        /* ROOM_OAM_BYTE */
#include "enemy_state.h"       /* ENEMY_PLAYER_OBJ_X, ENEMY_DIR, ENEMY_SCRATCH_Y, ENEMY_FRAME_FLAGS */
#include "sprite_state.h"      /* OAM_SPRITE_ATTR */
#include "combat_state.h"      /* LINK_ACTION_TIMER, COMBAT_WEAPON_SLOT */
#include "object_state.h"      /* OBJ_TILE_X/_Y, OBJ_ANIM_CNTR, OBJ_HFLIP */

/* NES RollingSpriteIndex = $0341. Wrap value = $28 (40 slots). */
#define ROLLING_SPRITE_INDEX_ADDR 0x0341u
#define ROLLING_SPRITE_INDEX_WRAP 0x28u

/* Inline equivalent of NES ResetCurSpriteIndex (Z_01.asm:3095):
 *   LDA #$00 / STA RollingSpriteIndex / RTS */
static inline void sprite_reset_cur_sprite_index_inline(void)
{
    RAM(ROLLING_SPRITE_INDEX_ADDR) = 0u;
}

void sprite_cycle_cur_sprite_index(void)
{
    /* drain: idx = RAM($0341) + 1; if idx == $28 reset; else store. */
    const unsigned char idx =
        (unsigned char)(RAM(ROLLING_SPRITE_INDEX_ADDR) + 1u);
    if (idx == ROLLING_SPRITE_INDEX_WRAP) {
        sprite_reset_cur_sprite_index_inline();
    } else {
        RAM(ROLLING_SPRITE_INDEX_ADDR) = idx;
    }
}

/* NES source: Z_01 Anim_WriteSpritePairNotFlashing/CycleCurSpriteIndex.
 * Drained C: sprite_cycle_cur_sprite_index above.
 * Coverage: native two-sided pair publisher, all byte cursor inputs.
 * Stance: EXTEND two consecutive cycles into one committed cursor. */
void sprite_cycle_cur_sprite_index_twice(void)
{
    unsigned char idx = (unsigned char)(RAM(ROLLING_SPRITE_INDEX_ADDR) + 2u);
    if (idx == ROLLING_SPRITE_INDEX_WRAP ||
        idx == ROLLING_SPRITE_INDEX_WRAP + 1u)
        idx = (unsigned char)(idx - ROLLING_SPRITE_INDEX_WRAP);
    RAM(ROLLING_SPRITE_INDEX_ADDR) = idx;
}

unsigned char sprite_cycle_sprite_index_in_a(unsigned char idx)
{
    /* drain: same as cycle_cur_sprite_index but seed from `idx` param
     * and return the new value. */
    idx = (unsigned char)(idx + 1u);
    if (idx == ROLLING_SPRITE_INDEX_WRAP) {
        sprite_reset_cur_sprite_index_inline();
        return 0u;
    }
    RAM(ROLLING_SPRITE_INDEX_ADDR) = idx;
    return idx;
}

void sprite_hide_object_sprites(void)
{
    /* drain (sprite_runtime.c:29-36): write $F8 (off-screen Y) to
     * OAM Y-byte of slots 24..63 (offsets 96..255 step 4). Then bump
     * high-priority OAM cursor at $0342 via cycle_sprite_index_in_a. */
    unsigned char d2 = 96u;
    do {
        ROOM_OAM_BYTE(d2) = 0xF8u;
        d2 = (unsigned char)(d2 + 4u);
    } while (d2 != 0u);  /* wraps to 0 after 256 → exit */
    RAM(0x0342) = sprite_cycle_sprite_index_in_a((unsigned char)RAM(0x0342));
}

/* --- Animation cluster (NES Z_07.asm Plan-C drained subset) ------------
 * Drain at src/oracle/world/sprite_runtime.c:65-117. */

/* file-static helper — drives Link's swing/use animation timer
 * progression. drain at sprite_runtime.c:65-77. */
static void sprite_animate_link_obj_state(void)
{
    const unsigned char state = LINK_ACTION_TIMER;
    const unsigned char major = (unsigned char)(state & 0x30u);
    if (major == 0x10u || major == 0x20u) {
        if (state & 0x0Fu) {
            LINK_ACTION_TIMER = (uint8_t)(state | 0x30u);
        } else {
            LINK_ACTION_TIMER = (uint8_t)(state + 1u);
        }
        OBJ_HFLIP(0) = 1u;
    } else if (major == 0x30u) {
        LINK_ACTION_TIMER = (uint8_t)(state & 0xC0u);
    }
}

void sprite_roll_over_anim_counter(unsigned int slot)
{
    OBJ_ANIM_CNTR(slot) = COMBAT_WEAPON_SLOT;
    OBJ_HFLIP(slot) = (uint8_t)(OBJ_HFLIP(slot) ^ 0x01u);
}

unsigned char sprite_anim_fetch_obj_pos(unsigned int slot)
{
    COMBAT_WEAPON_SLOT = OBJ_TILE_X(slot);
    ENEMY_SCRATCH_Y    = OBJ_TILE_Y(slot);
    ENEMY_FRAME_FLAGS  = 0u;
    return 0u;
}

void sprite_anim_set_obj_hflip(unsigned int slot)
{
    ENEMY_FRAME_FLAGS = OBJ_HFLIP(slot);
}

void sprite_anim_advance_and_fetch(unsigned int val, unsigned int slot)
{
    COMBAT_WEAPON_SLOT = (unsigned char)val;
    OBJ_ANIM_CNTR(slot) = (uint8_t)(OBJ_ANIM_CNTR(slot) - 1u);
    if (OBJ_ANIM_CNTR(slot) == 0u) {
        sprite_roll_over_anim_counter(slot);
    }
    (void)sprite_anim_fetch_obj_pos(slot);
}

void sprite_animate_object_walking(unsigned int slot)
{
    /* drain at sprite_runtime.c:104-117. NES AnimateObjectWalking.
     * Walk-cycle progression for a moving object slot. */
    OBJ_ANIM_CNTR(slot) = (uint8_t)(OBJ_ANIM_CNTR(slot) - 1u);
    if (OBJ_ANIM_CNTR(slot) == 0u) {
        if (slot == 0u) {
            sprite_animate_link_obj_state();
        }
        COMBAT_WEAPON_SLOT = 6u;
        sprite_roll_over_anim_counter(slot);
    }
    (void)sprite_anim_fetch_obj_pos(slot);
    const unsigned char dir = (unsigned char)(ENEMY_DIR(slot) & 0x0Cu);
    if (dir != 0u) {
        sprite_anim_set_obj_hflip(slot);
    } else {
        if (!(ENEMY_DIR(slot) & 1u)) {
            ENEMY_FRAME_FLAGS = (uint8_t)(ENEMY_FRAME_FLAGS + 1u);
        }
    }
}

void sprite_show_link_sprites_behind_horizontal_doors(void)
{
    /* drain (sprite_runtime.c:38-61) — see drain comment for full
     * background explanation. NES Z_01.asm:1594-ish.
     *
     * If Link's left edge (link_x) or right edge (link_x + 8) is in
     * the "off-side" range ($00..$0F or $E9..$FF), set OAM attr bit
     * $20 on the corresponding Link sprite slot (18 = left half,
     * 19 = right half) to drop priority below high-prio BG. */
    const unsigned char link_x  = (unsigned char)ENEMY_PLAYER_OBJ_X;
    const unsigned char x_left  = link_x;
    const unsigned char x_right = (unsigned char)(link_x + 8u);

    if (x_left < 0x10u || x_left >= 0xE9u) {
        OAM_SPRITE_ATTR(18) = (uint8_t)(OAM_SPRITE_ATTR(18) | 0x20u);
    }
    if (x_right < 0x10u || x_right >= 0xE9u) {
        OAM_SPRITE_ATTR(19) = (uint8_t)(OAM_SPRITE_ATTR(19) | 0x20u);
    }
}
