#include "player_defer.h"

#define PLAYER_WRITER_POSE_PUBLICATION "main: apply_warp_outcome image stores / movement pose publication"
#define PLAYER_WRITER_MOVEMENT_PUBLICATION "main: sync_link_from_nes / movement grid-fraction publication"
#define PLAYER_WRITER_ACTOR_BRIDGE "main: link_ctx_apply / link_ctx_load_player / link_ctx_store_player per-field handoffs"
#define PLAYER_WRITER_ANIMATION_PUBLICATION "ObjAnimFrame writers / main: reveal image read before set_link_halves"
#define PLAYER_WRITER_IMAGE_SEMANTIC "original image scalar writers; complete field slice required"
#define PLAYER_WRITER_ARROW_SLICE "original slot18 producers/consumers/reset/timer; complete unit9 slice required"
#define PLAYER_WRITER_WEAPON_PUBLICATION "original slot13/14 compatibility stores, independently timed from native setters"

const PlayerDeferDescriptor player_defer_descriptors[PLAYER_PHASE_COUNT] = {
#define PLAYER_DEFER_FIELD(id, role, member, nes, phase) \
    { #id, PLAYER_WRITER_##phase, (unsigned short)(member), (nes), PLAYER_DEFER_##role, PLAYER_PHASE_##phase, PLAYER_DEFER_OWNER_RECORD },
#include "player_defer_fields.def"
#undef PLAYER_DEFER_FIELD
};
#define BLOCK ((unsigned char *)(void *)(nes_ram + GR_OFF))
#define CONTROL (BLOCK + PLAYER_DEFER_CONTROL_OFF)
#define ENROLLED (CONTROL + 4u)
#define PENDING (BLOCK + PLAYER_DEFER_PENDING_OFF)
#define MASK (BLOCK + PLAYER_DEFER_MASK_DATA)
/* const tables go in ROM; there are no mutable .bss/.data globals. */
static const unsigned short guards[] = {
    PLAYER_DEFER_SNAPSHOT_LO, PLAYER_DEFER_SNAPSHOT_HI,
    PLAYER_DEFER_MASK_LO, PLAYER_DEFER_MASK_HI, PLAYER_DEFER_REFS_LO,
    PLAYER_DEFER_VIEWS_LO, PLAYER_DEFER_VIEWS_HI,
    PLAYER_DEFER_OBSERVE_LO, PLAYER_DEFER_OBSERVE_HI
};
static const unsigned char guard[4] = { 0x44u, 0x50u, 0x56u, 0x33u };

static unsigned char valid(unsigned char p, enum PlayerDeferField field)
{
    return p < PLAYER_RAM_COUNT && (unsigned int)field < PLAYER_PHASE_COUNT;
}
static unsigned short index_of(unsigned char p, enum PlayerDeferField field)
{
    return (unsigned short)(p * PLAYER_PHASE_COUNT + field);
}
static unsigned char bit(const unsigned char *bits, unsigned short n)
{
    return (unsigned char)(bits[n / 8u] & (1u << (n % 8u)));
}
static unsigned char *live(unsigned char p, enum PlayerDeferField field)
{
    const unsigned short off = player_defer_descriptors[field].record_offset;
    if (off != PLAYER_PHASE_ONLY)
        return (unsigned char *)(void *)&G_PLAYERS[p] + off;
    return BLOCK + PLAYER_DEFER_VIEWS_OFF + index_of(p, field);
}
unsigned char player_phase_read(unsigned char p, enum PlayerDeferField field)
{
    return valid(p, field) ? *live(p, field) : 0u;
}
unsigned char player_phase_store(unsigned char p, enum PlayerDeferField field, unsigned char value)
{
    if (!valid(p, field)) return 0u;
    *live(p, field) = value;
    return 1u;
}
const PlayerRam *player_phase_immediate(unsigned char p)
{
    return p < PLAYER_RAM_COUNT ? &G_PLAYERS[p] : (const PlayerRam *)0;
}
const unsigned char *player_defer_observed(unsigned char p)
{
    if (p >= PLAYER_RAM_COUNT || CONTROL[0] == 0u) return (const unsigned char *)0;
    return BLOCK + PLAYER_DEFER_OBSERVE_OFF + p * PLAYER_RAM_STRIDE;
}
unsigned char player_defer_changed(unsigned char p, enum PlayerDeferField field)
{
    return valid(p, field) && CONTROL[0] == 2u
        ? (unsigned char)(bit(MASK, index_of(p, field)) != 0u) : 0u;
}
unsigned char player_defer_canaries_ok(void)
{
    unsigned int g, k;
    for (g = 0u; g < sizeof guards / sizeof guards[0]; ++g)
        for (k = 0u; k < 4u; ++k)
            if (BLOCK[guards[g] + k] != guard[k]) return 0u;
    return player_ram_canaries_ok(); /* includes existing sidecar end guard */
}
unsigned char player_defer_begin(const unsigned char *enrolled)
{
    unsigned short n;
    unsigned int g, k;
    unsigned char p;
    if (CONTROL[0] != 0u) return 0u; /* unsupported nesting: no mutation */
    CONTROL[1] = g_cur_player;
    for (g = 0u; g < sizeof guards / sizeof guards[0]; ++g)
        for (k = 0u; k < 4u; ++k) BLOCK[guards[g] + k] = guard[k];
    for (n = 0u; n < PLAYER_DEFER_BITS_BYTES; ++n) {
        ENROLLED[n] = enrolled ? enrolled[n] : 0u;
        MASK[n] = 0u;
    }
    /* Observe all four records for isolation, including inactive players.
     * This diagnostic copy is never a restoration source. */
    for (p = 0u; p < PLAYER_RAM_COUNT; ++p) {
        const unsigned char *record = (const unsigned char *)(const void *)&G_PLAYERS[p];
        for (n = 0u; n < PLAYER_RAM_STRIDE; ++n)
            BLOCK[PLAYER_DEFER_OBSERVE_OFF + p * PLAYER_RAM_STRIDE + n] = record[n];
    }
    for (n = 0u; n < PLAYER_DEFER_BYTES; ++n)
        if (bit(ENROLLED, n))
            PENDING[n] = *live((unsigned char)(n / PLAYER_PHASE_COUNT),
                              (enum PlayerDeferField)(n % PLAYER_PHASE_COUNT));
    CONTROL[0] = 1u;
    return 1u; /* selector never changed */
}
unsigned char player_defer_hold(void)
{
    unsigned short n;
    if (CONTROL[0] != 1u) return 0u;
    for (n = 0u; n < PLAYER_DEFER_BYTES; ++n) {
        unsigned char *value, old, now;
        if (!bit(ENROLLED, n)) continue;
        value = live((unsigned char)(n / PLAYER_PHASE_COUNT),
                     (enum PlayerDeferField)(n % PLAYER_PHASE_COUNT));
        old = PENDING[n];
        now = *value;
        if (now != old) {
            MASK[n / 8u] |= (unsigned char)(1u << (n % 8u));
            *value = old;
        }
        PENDING[n] = now;
    }
    CONTROL[0] = 2u;
    g_cur_player = CONTROL[1];
    return 1u;
}
unsigned char player_defer_release(void)
{
    unsigned short n;
    if (CONTROL[0] != 2u) return 0u; /* double release, no writes */
    for (n = 0u; n < PLAYER_DEFER_BYTES; ++n)
        if (bit(MASK, n))
            *live((unsigned char)(n / PLAYER_PHASE_COUNT),
                  (enum PlayerDeferField)(n % PLAYER_PHASE_COUNT)) = PENDING[n];
    for (n = 0u; n < PLAYER_DEFER_BITS_BYTES; ++n) {
        MASK[n] = 0u;
        ENROLLED[n] = 0u;
    }
    CONTROL[0] = 0u;
    g_cur_player = CONTROL[1];
    return 1u;
}
