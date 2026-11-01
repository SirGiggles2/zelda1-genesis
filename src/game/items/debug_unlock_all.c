/* NES source: Variables.inc Items/Inv* (equipment tiers and resource cells).
 * Drained C: existing debug_unlock_all_items profile and inventory_t.
 * Coverage: FULL (debug permanent equipment, capacities and counters).
 * Stance: EXTEND. The title shortcut deliberately grants these items. */
#include "debug_unlock_all.h"
#include "../../state/inventory.h"
#include "../../abi/platform_abi.h"

/* NES RAM cell offsets (per reference/aldonunez/Variables.inc:236-267).
 * Mirror inventory_t writes into nes_ram[] so drained gameplay paths that
 * read via OBJ(NES_*) / RAM($65X) see the unlocked state. */
#define NES_INV_SWORD         0x0657u
#define NES_INV_RECORDER      0x065Cu
#define NES_INV_WAND          0x065Fu
#define NES_INV_BOMBS         0x0658u
#define NES_INV_ARROW         0x0659u
#define NES_INV_BOW           0x065Au
#define NES_INV_CANDLE        0x065Bu
#define NES_INV_FOOD          0x065Du
#define NES_INV_POTION        0x065Eu
#define NES_INV_RAFT          0x0660u
#define NES_INV_BOOK          0x0661u
#define NES_INV_RING          0x0662u
#define NES_INV_LADDER        0x0663u
#define NES_INV_MAGIC_KEY     0x0664u
#define NES_INV_BRACELET      0x0665u
#define NES_INV_LETTER        0x0666u
#define NES_INV_COMPASS       0x0667u
#define NES_INV_MAP           0x0668u
#define NES_INV_COMPASS9      0x0669u
#define NES_INV_MAP9          0x066Au
#define NES_INV_CLOCK         0x066Cu
#define NES_INV_RUPEES        0x066Du
#define NES_INV_KEYS          0x066Eu
#define NES_HEART_VALUES      0x066Fu
#define NES_HEART_PARTIAL     0x0670u
#define NES_INV_TRIFORCE      0x0671u
#define NES_INV_BOOMERANG     0x0674u
#define NES_INV_MAGIC_BOOM    0x0675u
#define NES_INV_MAGIC_SHIELD  0x0676u
#define NES_INV_MAX_BOMBS     0x067Cu
#define NES_INV_SELECTED_B    0x0656u
#define NES_RUPEES_TO_ADD     0x067Du
#define NES_RUPEES_TO_SUB     0x067Eu

/* T-090: 1 only for a gameplay session entered by the title debug chord
 * (A+B+C / X+Y+Z); 0 on the File Select path. Gates every gameplay debug
 * input and debug seed (engine/src/main.c). */
unsigned char g_debug_session = 0u;

void debug_unlock_all_items(void)
{
    /* Populate inventory_t. All counts/tiers maxed; per-dungeon
     * compass+map bitfields = $FF (all 8 dungeons collected); triforce
     * bitfield = $FF (all 8 pieces); heart_values = 0xFF (16 max + 16 cur). */
    g_inventory.items            = 0xFFu;   /* bow+wand+boomerang+flute+bait+letter+pot1+pot2 */
    g_inventory.bombs            = 16u;
    g_inventory.arrow            = INV_ARROW_SILVER;
    g_inventory.bow              = 1u;
    g_inventory.candle           = INV_CANDLE_RED;
    g_inventory.food             = 1u;
    g_inventory.potion           = 2u;       /* 2nd potion tier */
    g_inventory.raft             = 1u;
    g_inventory.book             = 1u;
    g_inventory.ring             = INV_RING_RED;
    g_inventory.ladder           = 1u;
    g_inventory.magic_key        = 1u;
    g_inventory.bracelet         = 1u;
    g_inventory.letter           = INV_LETTER_READ;
    g_inventory.compass_q1       = 0xFFu;
    g_inventory.map_q1           = 0xFFu;
    g_inventory.compass_l9       = 0xFFu;
    g_inventory.map_l9           = 0xFFu;
    g_inventory.clock            = 0u; /* transient pickup, not equipment */
    g_inventory.rupees           = 255u;
    g_inventory.keys             = 99u;
    g_inventory.heart_values     = 0xFFu;    /* hi nibble = max (16), lo = current (16) */
    g_inventory.heart_partial    = 0xFFu;
    g_inventory.triforce         = 0xFFu;
    g_inventory.boomerang_wood   = 1u;
    g_inventory.boomerang_magic  = 1u;
    g_inventory.magic_shield     = INV_SHIELD_MAGIC;
    g_inventory.max_bombs        = MAX_BOMBS_UPGRADE_2;   /* 16 cap */
    g_inventory.rupees_to_add    = 0u;
    g_inventory.rupees_to_sub    = 0u;
    g_inventory.selected_b_item  = 0u;       /* occupied magic-boomerang slot */

    /* Mirror into nes_ram[] so drained gameplay reads see the same
     * values via NES Variables.inc cells. */
    /* NES Items is the sword tier, not the compatibility equipment mask. */
    F_ram_0657_N2        = 3u;
    F_NES_INV_RECORDER_N     = 1u;
    F_NES_INV_WAND_N         = 1u;
    F_ram_0658_N        = g_inventory.bombs;
    F_ram_0659_N        = g_inventory.arrow;
    F_ram_065A_N2          = g_inventory.bow;
    F_ram_065B_N       = g_inventory.candle;
    F_NES_INV_FOOD_N         = g_inventory.food;
    F_NES_INV_POTION_N       = g_inventory.potion;
    F_ram_0660_N2         = g_inventory.raft;
    F_NES_INV_BOOK_N         = g_inventory.book;
    F_ram_0662_N         = g_inventory.ring;
    F_ram_0663_N2       = g_inventory.ladder;
    F_ram_0664_N    = g_inventory.magic_key;
    F_ram_0665_N     = g_inventory.bracelet;
    F_ram_0666_N       = g_inventory.letter;
    F_ram_0667_N      = g_inventory.compass_q1;
    F_ram_0668_N          = g_inventory.map_q1;
    F_ram_0669_N     = g_inventory.compass_l9;
    F_ram_066A_N         = g_inventory.map_l9;
    F_ram_066C_N        = g_inventory.clock;
    F_ram_066D_N       = (unsigned char)g_inventory.rupees;
    F_ram_066E_N         = g_inventory.keys;
    F_ram_066F_N2     = g_inventory.heart_values;
    F_ram_0670_N2    = g_inventory.heart_partial;
    F_ram_0671_N     = g_inventory.triforce;
    F_NES_INV_BOOMERANG_N    = g_inventory.boomerang_wood;
    F_ram_0675_N2   = g_inventory.boomerang_magic;
    F_ram_0676_N = g_inventory.magic_shield;
    F_ram_067C_N    = g_inventory.max_bombs;
    F_ram_0656_N   = g_inventory.selected_b_item;
    F_NES_RUPEES_TO_ADD_N    = g_inventory.rupees_to_add;
    F_ram_067E_N    = g_inventory.rupees_to_sub;
}
