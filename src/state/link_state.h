#ifndef LINK_STATE_H
#define LINK_STATE_H

#include "platform_abi.h"

/* ---------------------------------------------------------------------------
 * Phase 6 Task 6.1 — typed LinkState.
 *
 * NES Z1 keeps Link in object slot 0. Per-object fields are arrays in WRAM;
 * the Link slice is the [0] entry in each. The shape below mirrors that
 * slice as a typed struct so Phase 6 systems consume `players[i].x` instead
 * of `RAM(NES_OBJ_X+i)`. PlayerState is a typedef alias used by Phase 13;
 * Phase 6 reads `players[0]` only.
 *
 * NES source authority: reference/aldonunez/Variables.inc + Z_07.asm.
 * Field naming follows the NES ObjXxx prefix so cross-references stay
 * obvious in drained C and parity oracle traces.
 *
 * Sizes are chosen to match the NES storage class:
 *   - 8-bit fields (uint8_t)  for byte-wide WRAM cells.
 *   - 16-bit fields (int16_t) for fractional-position accumulators that
 *     need a sign in Genesis-native code.
 *
 * The legacy RAM(...) shims at the bottom of this header are kept until
 * the last consumer has migrated, so no caller breaks during the
 * incremental promotion.
 *
 * NOTE: this header is included from contexts that pull in SGDK
 * `<types.h>` (RoomRom main.c via `<genesis.h>`) AND from drained C
 * that does not. SGDK's types.h `#define`s `int16_t`/`uint8_t` to
 * its own short names if stdint hasn't been seen first, which then
 * makes a later `<stdint.h>` clash. We sidestep the whole tangle by
 * using plain C primitives — `unsigned char`/`signed char`/`signed
 * short` — which mean the same on the m68k-elf toolchain. Sizes are
 * the same as the original int16_t/uint8_t plan; the comment above
 * each field still names the NES width.
 * ------------------------------------------------------------------------ */

/* Facing / direction constants are intentionally NOT redefined here.
 * RoomRom (`roomrom_sprites.h` + `RoomRom/src/main.c`) ships its own
 * `link_face_t` and `link_dir_t` enums whose tokens (`LINK_FACE_DOWN`,
 * `LINK_DIR_NONE`, …) collide with any preprocessor `#define` of the
 * same name — the macro expansion turns the enum body into garbage at
 * preprocess time. Phase 6 code that wants the NES bitfield encoding of
 * `dir` uses raw literals (0x01 RIGHT, 0x02 LEFT, 0x04 DOWN, 0x08 UP)
 * or a future per-subsystem header. RoomRom keeps its ordinal enum
 * until Phase 6 sweeps it. */

typedef struct LinkState {
    /* Position. NES ObjX[0] = $70, ObjY[0] = $84. Signed 16-bit so
     * room-edge math stays correct under Genesis scroll. */
    signed short  x;
    signed short  y;

    /* Sub-pixel accumulators. NES ObjPosFrac[0] = $3A8,
     * ObjQSpeedFrac[0] = $3BC. */
    unsigned char pos_frac;
    unsigned char qspeed_frac;

    /* Facing / movement direction.
     *   `dir`       = NES ObjDir[0] = $98 (current movement bit)
     *   `face`      = persisted facing for sprite rendering
     *   `input_dir` = NES ObjInputDir[0] = $3F8 (this frame's pad mask)
     *   `moving_dir`= NES LINK_MOVING_DIR = $0F (last active axis)
     */
    unsigned char dir;
    unsigned char face;
    unsigned char input_dir;
    unsigned char moving_dir;

    /* Action state. NES ObjState[0] = $AC + ObjMetastate[0] = $405. */
    unsigned char state;
    unsigned char metastate;

    /* Movement / collision. NES ObjMovingLimit[0] = $380,
     * ObjGridOffset[0] = $394. */
    unsigned char moving_limit;
    signed char   grid_offset;

    /* Animation. NES ObjAnimCounter[0] = $3D0, ObjAnimFrame[0] = $3E4. */
    unsigned char anim_counter;
    unsigned char anim_frame;

    /* Invincibility. NES ObjInvincibilityTimer[0] = $4F0,
     * ObjInvincibilityMask[0] = $4B2. */
    unsigned char invincibility_timer;
    unsigned char invincibility_mask;

    /* Damage / knockback. NES ObjShoveDir[0] = $C0, ObjShoveDistance[0]
     * = $D3. */
    unsigned char shove_dir;
    unsigned char shove_distance;

    /* Item use. NES ObjShootTimer[0] = $451; halt flag = $66C
     * (LINK_HALT_FLAG); plus a Genesis-native byte for the in-flight
     * item-use kind so item dispatch does not have to re-derive it
     * from ObjMetastate every frame. */
    unsigned char shoot_timer;
    unsigned char halt_flag;
    unsigned char item_use_kind;
    unsigned char item_use_timer;

    /* Hit points. NES ObjHP[0] = $485. */
    unsigned char hp;

    /* Death. Existing platform_abi cells:
     *   MODE11_DEATH_TIMER, DEATH_FRAME_COUNTER. */
    unsigned char death_timer;
    unsigned char death_frame_counter;

    /* Per-room scratch. Was LINK_ROOM_SCRATCH = $59. Phase 6 keeps the
     * byte even though most consumers will move to typed accessors. */
    unsigned char room_scratch;
} LinkState;

/* PlayerState alias — the ONLY shape Phase 13 multiplayer needs to
 * differ. Adding more players is `players[i]`, not a struct rewrite. */
typedef LinkState PlayerState;

/* Legacy RAM shims — kept while consumers migrate. These will be deleted
 * in a follow-up commit once the last raw RAM(...) reference is gone. */
#define LINK_MOVING_DIR       RAM(NES_LINK_MOVING_DIR)
#define LINK_ROOM_SCRATCH     RAM(NES_LINK_ROOM_SCRATCH)
#define LINK_HALT_FLAG        RAM(NES_LINK_HALT_FLAG)
#define MODE11_DEATH_TIMER    RAM(NES_MODE11_DEATH_TIMER)
#define DEATH_FRAME_COUNTER   RAM(NES_DEATH_FRAME_COUNTER)

#endif /* LINK_STATE_H */
