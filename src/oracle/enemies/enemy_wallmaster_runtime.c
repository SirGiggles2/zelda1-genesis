#include "enemy_runtime_private.h"

/* Wallmaster scratch: RAM[0]..RAM[4] are used as temporaries during the
 * "calc start position" / "patch sprites" flow. They alias the common
 * ENEMY_SCRATCH_X/Y/ENEMY_ATTR_SCRATCH etc. but we name them explicitly
 * here to match the NES comments.
 */
#define WALLMASTER_MINOR_MAJOR_MIN     F_ram_0000_R3  /* minor init minor coord (min) */
#define WALLMASTER_MAJOR_MINOR_MIN     F_ram_0001_R2  /* major coord minimum value  */
#define WALLMASTER_INSTR_AXIS          F_ram_0002_R2  /* axis-decrease direction bit */
#define WALLMASTER_INSTR_MINOR_MIN     F_ram_0003_R3  /* init minor distance (major min) */
#define WALLMASTER_INIT_MINOR_COORD    F_ram_0004_R2  /* initial minor coord result */

/* Room-state alias needed by enrt_wallmaster_calc_start_position. */
#define ROOM_INPUT_DIR                 F_ram_03F8_R2

/*
 * Wallmaster family — scratch/draw helpers shared between state init and
 * the sprite-patching path. See z_04.asm for the original comments.
 */

/* SpriteRelativeExtents table lives in bank 1 asm (z_01): {0x08, 0x00}.
 * Rather than add another import, inline it here — it's a two-byte constant
 * used only by the wallmaster sprite-patcher.
 */
static const unsigned char WallmasterSpriteRelExtents[2] = { 0x08, 0x00 };

/*
 * Wallmaster_CalcStartPosition
 *
 * Entry:
 *   instr_offset   = initial instruction-table offset (stored into
 *                    ENEMY_PUSH_TIMER(slot))
 *   init_major_min = minimum major coord (the wall's base position; also
 *                    serves as initial distance reference)
 *   slot           = monster slot (D2)
 *   Reads RAM:
 *     WALLMASTER_MINOR_MAJOR_MIN ($0000) — Link's minor coord
 *     WALLMASTER_MAJOR_MINOR_MIN ($0001) — Link's major coord
 *     WALLMASTER_INSTR_AXIS      ($0002) — axis-decrease direction bit
 *     ROOM_INPUT_DIR             ($03F8)
 *     LINK_DIR                   ($0098,A4,0) == ENEMY_DIR(0)
 *
 * Exit:
 *   Writes RAM:
 *     WALLMASTER_INSTR_MINOR_MIN ($0003) = init_major_min (passed through)
 *     WALLMASTER_INIT_MINOR_COORD ($0004) = computed initial minor coord
 *     ENEMY_PUSH_TIMER(slot)   = final instr offset (base + optional +8 / +$10)
 *   Returns index 0 or 1 — 0 if Link is at the minimum wall, 1 if at the
 *   farther wall (caller uses this to index an initial-coord table).
 */
unsigned int enrt_wallmaster_calc_start_position(unsigned int instr_offset,
                                                 unsigned int init_major_min,
                                                 unsigned int slot) {
    unsigned char dist;
    unsigned char instr_axis;
    unsigned char link_minor;
    unsigned char link_major;
    unsigned char idx;

    ENEMY_PUSH_TIMER(slot) = (unsigned char)instr_offset;
    WALLMASTER_INSTR_MINOR_MIN = (unsigned char)init_major_min;

    /* If Link is still (input dir = 0) use distance $24, else $32. */
    dist = (ROOM_INPUT_DIR == 0) ? 0x24 : 0x32;

    /* If Link faces in the direction passed in (which decreases along the
     * wall), negate the distance and add 8 to the instruction offset so we
     * consult the block for the opposite direction along the same wall.
     */
    instr_axis = WALLMASTER_INSTR_AXIS;
    if (ENEMY_DIR(0) == instr_axis) {
        ENEMY_PUSH_TIMER(slot) = (unsigned char)(ENEMY_PUSH_TIMER(slot) + 0x08);
        dist = (unsigned char)(-(int)(signed char)dist);
    }

    /* initial minor coord = Link's minor + (signed) dist. */
    link_minor = WALLMASTER_MINOR_MAJOR_MIN;
    WALLMASTER_INIT_MINOR_COORD = (unsigned char)(link_minor + dist);

    /* Default: return index 0 (minimum wall) for major-coord table lookup. */
    idx = 0;

    /* If Link's major coord <> minimum major coord, he's at the farther wall.
     * Add $10 to the instr offset and bump the index to 1.
     */
    link_major = WALLMASTER_MAJOR_MINOR_MIN;
    if (link_major != WALLMASTER_INSTR_MINOR_MIN) {
        ENEMY_PUSH_TIMER(slot) = (unsigned char)(ENEMY_PUSH_TIMER(slot) + 0x10);
        idx = 1;
    }
    return idx;
}

/*
 * Wallmaster_PutSpriteBehindBgIfNeeded
 *
 * Entry:
 *   sprite_byte_off = offset into OAM shadow of the sprite's attribute cell
 *                     (so attr = OAM[$0202 + off], x = OAM[$0203 + off]).
 *
 * Walks extent indices {1, 0}: checks sprite_x + SpriteRelativeExtents[idx].
 * If the result >= $E9 or < $18, sets priority bit $20 on the sprite
 * attribute (puts it behind the background).
 */
void enrt_wallmaster_put_sprite_behind_bg_if_needed(unsigned int sprite_byte_off) {
    int i;
    for (i = 1; i >= 0; --i) {
        unsigned char sx = F_ram_0203_R3(sprite_byte_off);
        unsigned char probe = (unsigned char)(sx + WallmasterSpriteRelExtents[i]);
        if (probe >= 0xE9 || probe < 0x18) {
            unsigned char attr = F_ram_0202_R3(sprite_byte_off);
            F_ram_0202_R3(sprite_byte_off) = (unsigned char)(attr | 0x20);
        }
    }
}

/*
 * Wallmaster_PutSpritesBehindBgIfNeeded
 *
 * Applies PutSpriteBehindBgIfNeeded to both sprite offsets stored in
 * WALLMASTER_MINOR_MAJOR_MIN ($0000) and WALLMASTER_MAJOR_MINOR_MIN ($0001)
 * by _L_z04_L_Wallmaster_State1_PatchSprites. Note: the original asm falls
 * through into PutSpriteBehindBgIfNeeded after loading the second offset;
 * replicate that by calling the helper twice.
 */
void enrt_wallmaster_put_sprites_behind_bg_if_needed(void) {
    enrt_wallmaster_put_sprite_behind_bg_if_needed(WALLMASTER_MINOR_MAJOR_MIN);
    enrt_wallmaster_put_sprite_behind_bg_if_needed(WALLMASTER_MAJOR_MINOR_MIN);
}
