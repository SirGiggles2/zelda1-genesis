/* nes_ram_sync.c — see nes_ram_sync.h for full task-header docs. */

#include "nes_ram_sync.h"
#include "platform_abi.h"          /* nes_ram[] */
#include "inventory.h"             /* g_inventory */
#include "player_state.h"          /* players[0] */
#include "joy.h"                   /* SGDK BUTTON_* */
#include "../game/combat/combat_runtime.h"  /* roomrom_combat_get_swing/beam_* */

/* NES Z_07 ReadInputs bit layout (Variables.inc ButtonsPressed=$FA,
 * ButtonsDown=$FB). */
#define NES_BTN_A       0x80u
#define NES_BTN_B       0x40u
#define NES_BTN_SELECT  0x20u
#define NES_BTN_START   0x10u
#define NES_BTN_UP      0x08u
#define NES_BTN_DOWN    0x04u
#define NES_BTN_LEFT    0x02u
#define NES_BTN_RIGHT   0x01u

/* Variables.inc:74-75 (NES Z1):
 *   ButtonsPressed := $F8   (edge — set on 0->1 transition this frame)
 *   ButtonsDown    := $FA   (held — 1 while button is down)
 * Plan v5 listed these as $FA/$FB which contradicts both Variables.inc
 * and the transpiled src/zelda_translated/z_07.asm:124-125 declarations.
 * Drain (asm) primary, plan secondary — use $F8/$FA. */
#define NES_RAM_BUTTONS_PRESSED   0x00F8u
#define NES_RAM_BUTTONS_DOWN      0x00FAu
#define NES_RAM_HEART_VALUES      0x066Fu
#define NES_RAM_HEART_PARTIAL     0x0670u
/* NES Variables.inc:80 ObjDir := $0098. ObjDir[0] = Link facing
 * bitmap: $01=R, $02=L, $04=D, $08=U. The earlier sync wrote $008C
 * which is ObjY[slot 8] — a wrong cell that left ObjDir[0] always
 * zero, so collision_check_link_collision_preinit's parry direction
 * test and AI chase-targets all read a dead 0. */
#define NES_RAM_OBJDIR_LINK       0x0098u

/* link_face_t (sprite_render.h): DOWN=0, UP=1, LEFT=2, RIGHT=3.
 * NES bitmap: $01=R, $02=L, $04=D, $08=U. */
static const unsigned char k_face_to_nes_dir[4] = {
    0x04u,  /* DOWN  */
    0x08u,  /* UP    */
    0x02u,  /* LEFT  */
    0x01u   /* RIGHT */
};

static unsigned char sgdk_to_nes_buttons(u16 joy)
{
    unsigned char n = 0u;
    if (joy & BUTTON_A)     n |= NES_BTN_A;
    if (joy & BUTTON_B)     n |= NES_BTN_B;
    /* Genesis pad has no Select. C is the closest analogue (third
     * face button); Z1 Select only opens the pause menu, which on
     * Genesis is reachable via Start anyway. Wiring C->Select keeps
     * the NES side aware of C presses in case a future native consumer
     * binds to Select. */
    if (joy & BUTTON_C)     n |= NES_BTN_SELECT;
    if (joy & BUTTON_START) n |= NES_BTN_START;
    if (joy & BUTTON_UP)    n |= NES_BTN_UP;
    if (joy & BUTTON_DOWN)  n |= NES_BTN_DOWN;
    if (joy & BUTTON_LEFT)  n |= NES_BTN_LEFT;
    if (joy & BUTTON_RIGHT) n |= NES_BTN_RIGHT;
    return n;
}

void nes_ram_sync_input(u16 held, u16 edge_pressed)
{
    nes_ram[NES_RAM_BUTTONS_PRESSED] = sgdk_to_nes_buttons(edge_pressed);
    nes_ram[NES_RAM_BUTTONS_DOWN]    = sgdk_to_nes_buttons(held);
    /* ObjInputDir ($03F8): NES UpdateMode5Play sets it from ButtonsDown &
     * $0F every play frame before UpdatePlayer (Z_05.asm, "Save current
     * direction from input buttons"). Read by tile-object pushes (T-050)
     * and Wallmaster. UpdatePlayer's later adjustments are Link's (T-102). */
    nes_ram[0x03F8u] = (unsigned char)(nes_ram[NES_RAM_BUTTONS_DOWN] & 0x0Fu);
}

void nes_ram_sync_input2(u16 held, u16 edge_pressed)
{
    nes_ram[NES_RAM_BUTTONS_PRESSED + 1u] = sgdk_to_nes_buttons(edge_pressed);
    nes_ram[NES_RAM_BUTTONS_DOWN + 1u]    = sgdk_to_nes_buttons(held);
}

/* Per-frame sync: nes_ram is canonical for drained combat
 * (link_collision_link_be_harmed writes LINK_HEARTS = RAM($066F) directly).
 * Pull back into g_inventory so Genesis-side readers (HUD, options) see
 * current values. Prior direction (g_inventory -> nes_ram) silently
 * overwrote combat damage every frame; hearts never decremented under
 * enemy contact. Seed the nes_ram side once at gameplay start via
 * nes_ram_seed_inventory_hearts(). */
void nes_ram_sync_inventory_hearts(void)
{
    g_inventory.heart_values  = nes_ram[NES_RAM_HEART_VALUES];
    g_inventory.heart_partial = nes_ram[NES_RAM_HEART_PARTIAL];
}

void nes_ram_seed_inventory_hearts(void)
{
    nes_ram[NES_RAM_HEART_VALUES]  = g_inventory.heart_values;
    nes_ram[NES_RAM_HEART_PARTIAL] = g_inventory.heart_partial;
}

void nes_ram_sync_link_face(void)
{
    unsigned char idx = (unsigned char)players[0].face;
    if (idx > 3u) idx = 0u;
    nes_ram[NES_RAM_OBJDIR_LINK] = k_face_to_nes_dir[idx];
}

/* NES Variables.inc cell bases.
 *   ObjX    := $0070  (per-slot X, 12 slots + weapons)
 *   ObjY    := $0084
 *   ObjDir  := $0098
 *   ObjState:= $00AC
 *   ObjType := $034F
 * Sword weapon slot = 13 (NES SwordSlot/RodSlot).
 * Sword-shot weapon slot = 14 (NES SwordShotSlot/MagicShotSlot). */
#define NES_OBJ_X_BASE      0x0070u
#define NES_OBJ_Y_BASE      0x0084u
#define NES_OBJ_DIR_BASE    0x0098u
#ifndef NES_OBJ_STATE_BASE
#define NES_OBJ_STATE_BASE  0x00ACu
#endif
#ifndef NES_OBJ_TYPE_BASE
#define NES_OBJ_TYPE_BASE   0x034Fu
#endif
#define NES_SWORD_SLOT      13u
#define NES_ITEM_SWORD_LEVEL 0x0657u

void nes_ram_sync_sword(void)
{
    unsigned char st  = roomrom_combat_get_swing_state();
    if (st == 0u) {
        nes_ram[NES_OBJ_STATE_BASE + NES_SWORD_SLOT] = 0u;
    } else {
        unsigned char fi = (unsigned char)roomrom_combat_get_swing_face();
        if (fi > 3u) {
            fi = 0u;
        }
        nes_ram[NES_OBJ_DIR_BASE + NES_SWORD_SLOT] = k_face_to_nes_dir[fi];
        nes_ram[NES_OBJ_X_BASE + NES_SWORD_SLOT] =
            (unsigned char)roomrom_combat_get_swing_x();
        nes_ram[NES_OBJ_Y_BASE + NES_SWORD_SLOT] =
            (unsigned char)roomrom_combat_get_swing_y();
        nes_ram[NES_OBJ_STATE_BASE + NES_SWORD_SLOT] = st;
    }

}

void nes_ram_seed_sword_level(unsigned char level)
{
    nes_ram[NES_ITEM_SWORD_LEVEL] = level;
}
