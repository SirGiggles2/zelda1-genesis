#ifndef BOSS_OVERLAY_H
#define BOSS_OVERLAY_H

/* T-261 (Remix only): boss name banner + floating health bars.
 * Presentation only: reads ENEMY_TYPE/HP/METASTATE, never writes game state.
 * Part rules come from docs/audit/2026-10-29_T261_boss_sources.md. */

#define BOSS_OVERLAY_MAX_BARS 12u      /* monster slots 1..11 + spare */
#define BOSS_BAR_WIDTH        16u      /* bar fill in pixels */
#define BOSS_BANNER_FRAMES    120u     /* name banner, ~2 s at 60 Hz, after its tiles load */
#define BOSS_HUD_BAR_PX       128u     /* giant HUD bar width */
#define BOSS_NAME_GANON       9u       /* name only, no bar */

#define BOSS_RULE_BAR         0x01u    /* own bar per object */
#define BOSS_RULE_CHAIN       0x02u    /* Moldorm/Lanmola: one bar per chain (slots 1-5, 6-10) */
#define BOSS_RULE_GLEEOK      0x04u    /* one shared bar from ObjHP[5] + dead-neck mask */
#define BOSS_RULE_NO_SLOT5    0x08u    /* Manhandla: base in slot 5 cannot die */

typedef struct {
    unsigned char obj_type;
    unsigned char name_id;             /* index into k_boss_names */
    unsigned char flags;               /* BOSS_RULE_* */
} boss_part_rule_t;

typedef struct {
    unsigned char active;              /* encounter live */
    unsigned char name_id;
    unsigned char banner_timer;        /* frames left, 0 = no banner; the game holds meanwhile */
    unsigned char bar_count;
    unsigned char slot[BOSS_OVERLAY_MAX_BARS];      /* anchor slot of each bar */
    unsigned char obj_type[BOSS_OVERLAY_MAX_BARS];
    unsigned char spawn_hp[BOSS_OVERLAY_MAX_BARS];
    unsigned char fill_px[BOSS_OVERLAY_MAX_BARS];   /* 0..BOSS_BAR_WIDTH */
    unsigned char parts_seen;          /* most bars alive at once this encounter */
    unsigned char hud_mode;            /* always 1: name + bar live in HUD row 6 */
    unsigned char hud_fill;            /* giant bar, 0..BOSS_HUD_BAR_PX, never rises */
    unsigned short hp_full_sum;        /* spawn HP of every part seen this encounter */
} boss_overlay_state_t;

extern boss_overlay_state_t g_boss_overlay;     /* probes read this by ELF symbol */
extern unsigned char g_boss_overlay_gen;        /* changes when the model is rebuilt */
extern const boss_part_rule_t k_boss_part_rules[];
extern const unsigned char k_boss_part_rule_count;
extern const char *const k_boss_names[];

const boss_part_rule_t *boss_overlay_rule_for(unsigned char obj_type);
void boss_overlay_room_enter(void);
void boss_overlay_update(void);
void boss_overlay_clear(void);
/* Appends bar + banner sprites at SAT slot `sat` (never past `last`);
 * returns the next free slot. Called by both enemy sweeps. */
unsigned short boss_overlay_emit(unsigned short sat, unsigned short last);
extern unsigned char g_boss_strip_live;
/* Encounter intro: the whole game holds (no actor, timer or input moves)
 * while the name flies in and up into the HUD (user 2026-10-29). The host
 * then skips its game tick and calls boss_overlay_intro_frame instead. */
static inline unsigned char boss_overlay_intro_hold(void)
{
    return (unsigned char)(g_boss_overlay.active && g_boss_overlay.banner_timer);
}
void boss_overlay_intro_frame(void);
/* Call-site guard: outside boss fights the sweeps pay one byte test, and the
 * emit body stays out of their register allocation (T-261 lag gate). */
static inline __attribute__((always_inline))
unsigned short boss_overlay_emit_guarded(unsigned short sat, unsigned short last)
{
    if (!g_boss_overlay.active && !g_boss_strip_live) return sat;
    return boss_overlay_emit(sat, last);
}

#endif
