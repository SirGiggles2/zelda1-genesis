/* T-261 boss part rules. Every row: docs/audit/2026-10-29_T261_boss_sources.md
 * (ObjType from Z_07.asm UpdateObject_JumpTable). */
#include "boss_overlay.h"

const char *const k_boss_names[] = {
    "AQUAMENTUS", "DODONGO", "MANHANDLA", "GLEEOK", "DIGDOGGER",
    "GOHMA", "MOLDORM", "LANMOLA", "PATRA", "GANON"
};

const boss_part_rule_t k_boss_part_rules[] = {
    { 0x3Du, 0u, BOSS_RULE_BAR },                       /* Aquamentus */
    { 0x31u, 1u, BOSS_RULE_BAR },                       /* Dodongo */
    { 0x32u, 1u, BOSS_RULE_BAR },
    { 0x3Cu, 2u, BOSS_RULE_BAR | BOSS_RULE_NO_SLOT5 },  /* Manhandla hands */
    { 0x42u, 3u, BOSS_RULE_GLEEOK },                    /* Gleeok, 1-4 necks */
    { 0x43u, 3u, BOSS_RULE_GLEEOK },
    { 0x44u, 3u, BOSS_RULE_GLEEOK },
    { 0x45u, 3u, BOSS_RULE_GLEEOK },
    { 0x46u, 3u, BOSS_RULE_BAR },                       /* flying head: live-checked */
    { 0x38u, 4u, 0u },                                  /* big Digdogger: invincible */
    { 0x39u, 4u, 0u },
    { 0x18u, 4u, BOSS_RULE_BAR },                       /* little Digdogger */
    { 0x33u, 5u, BOSS_RULE_BAR },                       /* Gohma */
    { 0x34u, 5u, BOSS_RULE_BAR },
    { 0x41u, 6u, BOSS_RULE_CHAIN },                     /* Moldorm */
    { 0x3Au, 7u, BOSS_RULE_CHAIN },                     /* Lanmola */
    { 0x3Bu, 7u, BOSS_RULE_CHAIN },
    { 0x47u, 8u, BOSS_RULE_BAR },                       /* Patra core */
    { 0x48u, 8u, BOSS_RULE_BAR },
    { 0x25u, 8u, BOSS_RULE_BAR },                       /* Patra children */
    { 0x26u, 8u, BOSS_RULE_BAR },
    { 0x3Eu, 9u, 0u },                                  /* Ganon: name only */
};
const unsigned char k_boss_part_rule_count =
    (unsigned char)(sizeof(k_boss_part_rules) / sizeof(k_boss_part_rules[0]));

/* ObjType -> rule index + 1 (0 = not a boss part). One byte read per
 * slot per tick instead of a table scan (T-261 lag gate). */
static const unsigned char k_rule_by_type[256] = {
    [0x3D] = 1,  [0x31] = 2,  [0x32] = 3,  [0x3C] = 4,  [0x42] = 5,  [0x43] = 6,
    [0x44] = 7,  [0x45] = 8,  [0x46] = 9,  [0x38] = 10, [0x39] = 11, [0x18] = 12,
    [0x33] = 13, [0x34] = 14, [0x41] = 15, [0x3A] = 16, [0x3B] = 17, [0x47] = 18,
    [0x48] = 19, [0x25] = 20, [0x26] = 21, [0x3E] = 22,
};
_Static_assert(sizeof(k_boss_part_rules) / sizeof(k_boss_part_rules[0]) == 22u,
               "k_rule_by_type must index every rule row");

const boss_part_rule_t *boss_overlay_rule_for(unsigned char obj_type)
{
    const unsigned char i = k_rule_by_type[obj_type];
    return i ? &k_boss_part_rules[i - 1u] : 0;
}
