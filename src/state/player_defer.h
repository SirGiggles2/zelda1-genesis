#ifndef PLAYER_DEFER_H
#define PLAYER_DEFER_H

#include "player_ram.h"

/* T-367 unit 3: no runtime callers/enrollments. The original image/effect
 * transaction is the only active room-defer owner. Never wrap it with this
 * helper until each enrolled field's complete authority slice has switched.
 * D = delayed semantic kernel; P = independent publication of an I value.
 * Native I fields are observable here, but never transaction destinations. */
#define PLAYER_PHASE_ONLY 0xFFFFu
enum PlayerDeferRole { PLAYER_DEFER_D, PLAYER_DEFER_P };
/* Writer phases remain separate from transaction phases:
 * POSE/MOVEMENT: main movement completion and original sync handoffs;
 * ACTOR_BRIDGE: link_ctx_load_player/apply/store at their original per-field
 * sites (including the later diagonal store); never a whole-record reconcile.
 * ANIMATION: original ObjAnimFrame writers/reveal publication.
 * IMAGE_SEMANTIC/ARROW: original scalar/slot writers, migrated atomically.
 * WEAPON: original slot13/14 publishers; never latest native pose at release. */
enum PlayerDeferPublication {
    PLAYER_PHASE_POSE_PUBLICATION, PLAYER_PHASE_MOVEMENT_PUBLICATION,
    PLAYER_PHASE_ACTOR_BRIDGE, PLAYER_PHASE_ANIMATION_PUBLICATION,
    PLAYER_PHASE_IMAGE_SEMANTIC, PLAYER_PHASE_ARROW_SLICE,
    PLAYER_PHASE_WEAPON_PUBLICATION
};
enum PlayerDeferField {
#define PLAYER_DEFER_FIELD(id, role, member, nes, phase) PLAYER_PHASE_##id,
#include "player_defer_fields.def"
#undef PLAYER_DEFER_FIELD
    PLAYER_PHASE_COUNT
};
/* A future catalog edit cannot silently enroll I or record padding. */
#define PLAYER_DEFER_FIELD(id, role, member, nes, phase) \
    _Static_assert((member) == PLAYER_PHASE_ONLY || \
        (((member) >= __builtin_offsetof(PlayerRam, obj_dir) && \
          (member) <= __builtin_offsetof(PlayerRam, sword_blocked)) || \
         ((member) >= __builtin_offsetof(PlayerRam, arrow_rod) && \
          (member) < __builtin_offsetof(PlayerRam, arrow_rod) + sizeof(ArrowRodRam)) || \
          (member) == __builtin_offsetof(PlayerRam, sword_qspeed) || \
          (member) == __builtin_offsetof(PlayerRam, bubble_block_timer)), "D/P excludes I/padding: " #id);
#include "player_defer_fields.def"
#undef PLAYER_DEFER_FIELD

typedef struct PlayerDeferDescriptor {
    const char *representation, *writer;
    unsigned short record_offset, nes_label;
    unsigned char role, publication, owner;
} PlayerDeferDescriptor;
#define PLAYER_DEFER_OWNER_RECORD 1u /* explicit p argument, never selector */
extern const PlayerDeferDescriptor player_defer_descriptors[PLAYER_PHASE_COUNT];

/* Every new mutable byte is in the existing native reserve. Snapshot and
 * mask guards are carved from their unit-2 capacities, not record padding.
 * Each descriptor is ONE byte: bit0 = first byte, independent of host endian. */
#define PLAYER_DEFER_BYTES (PLAYER_RAM_COUNT * PLAYER_PHASE_COUNT)
#define PLAYER_DEFER_BITS_BYTES ((PLAYER_DEFER_BYTES + 7u) / 8u)
#define PLAYER_DEFER_SNAPSHOT_LO PLAYER_RAM_SNAPSHOT_OFF
#define PLAYER_DEFER_PENDING_OFF (PLAYER_DEFER_SNAPSHOT_LO + 4u)
#define PLAYER_DEFER_SNAPSHOT_HI (PLAYER_RAM_MASK_OFF - 4u)
#define PLAYER_DEFER_MASK_LO PLAYER_RAM_MASK_OFF
#define PLAYER_DEFER_MASK_DATA (PLAYER_DEFER_MASK_LO + 4u)
#define PLAYER_DEFER_MASK_HI (PLAYER_RAM_REFS_OFF - 8u)
#define PLAYER_DEFER_REFS_LO (PLAYER_RAM_REFS_OFF - 4u)
#define PLAYER_DEFER_VIEWS_LO (PLAYER_RAM_SELECTOR_OFF + 4u)
#define PLAYER_DEFER_VIEWS_OFF (PLAYER_DEFER_VIEWS_LO + 4u)
#define PLAYER_DEFER_VIEWS_HI (PLAYER_DEFER_VIEWS_OFF + PLAYER_DEFER_BYTES)
#define PLAYER_DEFER_CONTROL_OFF (PLAYER_DEFER_VIEWS_HI + 4u)
#define PLAYER_DEFER_OBSERVE_LO (PLAYER_DEFER_CONTROL_OFF + 4u + PLAYER_DEFER_BITS_BYTES)
#define PLAYER_DEFER_OBSERVE_OFF (PLAYER_DEFER_OBSERVE_LO + 4u)
#define PLAYER_DEFER_OBSERVE_HI (PLAYER_DEFER_OBSERVE_OFF + PLAYER_RAM_COUNT * PLAYER_RAM_STRIDE)
#define PLAYER_DEFER_END (PLAYER_DEFER_OBSERVE_HI + 4u)
_Static_assert(PLAYER_DEFER_PENDING_OFF + PLAYER_DEFER_BYTES <= PLAYER_DEFER_SNAPSHOT_HI, "D/P snapshot capacity");
_Static_assert(PLAYER_DEFER_MASK_DATA + PLAYER_DEFER_BITS_BYTES <= PLAYER_DEFER_MASK_HI, "D/P mask capacity");
_Static_assert(PLAYER_DEFER_VIEWS_LO > PLAYER_RAM_SELECTOR_OFF, "selector disjoint");
_Static_assert(PLAYER_DEFER_END < PLAYER_RAM_END_CANARY_OFF, "phase views fit reserve");
_Static_assert(GR_OFF + PLAYER_DEFER_END < 0x8000u, "phase views A4 reachable");
_Static_assert(GR_OFF + PLAYER_DEFER_END <= GR_MAP_SCRATCH_OFF, "T-257 map scratch above phase views");

/* Bit index = p * COUNT + field. NULL means no migrated mappings. Begin
 * freezes enrollment for the whole transaction; active-player count is NOT
 * enrollment. Later slices must pass a ROM-constant mask of switched fields.
 * No image read/import/export occurs in any helper. No callbacks/selection. */
unsigned char player_defer_begin(const unsigned char *enrolled);
unsigned char player_defer_hold(void);
unsigned char player_defer_release(void);
unsigned char player_defer_canaries_ok(void);
unsigned char player_defer_changed(unsigned char p, enum PlayerDeferField field);
unsigned char player_phase_read(unsigned char p, enum PlayerDeferField field);
unsigned char player_phase_store(unsigned char p, enum PlayerDeferField field, unsigned char value);
/* I access is explicit and independent from delayed/P reads. No setter here:
 * real native writers own I. Pre-begin observations are diagnostics only. */
const PlayerRam *player_phase_immediate(unsigned char p);
const unsigned char *player_defer_observed(unsigned char p);
#endif
