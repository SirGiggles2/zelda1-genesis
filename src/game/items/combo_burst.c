/* T-260: Remix arrow-burst bomb — 8 arrows from the bomb's blast.
 *
 * NES source: Z_07.asm UpdateArrowOrBoomerang / MoveShot / DrawArrow,
 * Z_01.asm CheckMonsterArrowOrRodCollision (building blocks only).
 * Drained C: arrow.c roomrom_arrow_update_for_player, object_move_shot,
 * link_collision_check_monster_arrow_only. Coverage: NONE for the burst.
 * Stance: GREENFIELD native pool. Each live entry is loaded into the
 * player-1 arrow slot $12 for the shared mover and admitted enemy-owner
 * arrow checks, then the slot and NES scratch $00-$0F are restored, so the real
 * arrow is untouched. Arrows fly through terrain like NES arrows and end at
 * the room bound or a monster hit (3-update spark). Diagonals use the
 * user-drawn up-right arrow (arrow_diag_chr.c), flipped. */
#include "combo_recipes.h"
#include "combo_ram.h"
#include "arrow_diag_chr.h"
#include "bomb.h"
#include "../../abi/platform_abi.h"
#include "../world/object_dispatch.h"      /* object_move_shot */
#include "../world/draw_dispatch.h"        /* draw_item_frame_tile */
#include "../enemies/enemy_render.h"       /* enemy_render_entry_t */
#include "../combat/collision_dispatch.h"

#define BURST_N        COMBO_BURST_N
#define BURST_SLOT     0x12u
#define CR             COMBO_RAM
#define MONSTER_FIRST  1u
#define MONSTER_LAST   11u

typedef ComboBurstArrow BurstArrow;

/* N, NE, E, SE, S, SW, W, NW in NES direction bits (R1 L2 D4 U8). */
static const unsigned char k_burst_dirs[BURST_N] = {
    0x08u, 0x09u, 0x01u, 0x05u, 0x04u, 0x06u, 0x02u, 0x0Au
};

void combo_arrow_burst_place(unsigned char player)
{
    (void)player;   /* bomb placed from the loaded player context */
    roomrom_bomb_place_emitting();
}

unsigned char combo_burst_live(void)
{
    unsigned char i;
    if (roomrom_bomb_emitting()) return 1u;
    for (i = 0u; i < BURST_N; ++i) if (CR->burst[i].state) return 1u;
    return 0u;
}

void combo_burst_clear(void)
{
    unsigned char i;
    for (i = 0u; i < BURST_N; ++i) CR->burst[i].state = 0u;
    CR->sprite_count = 0u;
    CR->emit[0] = CR->emit[1] = 0u;
}

const enemy_render_entry_t *combo_burst_sprites(unsigned char *count)
{
    *count = CR->sprite_count;
    return CR->sprites;
}

void combo_burst_emit(unsigned char x, unsigned char y)
{
    unsigned char i;
    /* DrawArrow palette = InvArrow - 1 (wood 0, silver 1). */
    CR->burst_pal = (unsigned char)(F_ram_0659_N ? F_ram_0659_N - 1u : 0u);
    for (i = 0u; i < BURST_N; ++i) {
        BurstArrow *a = &CR->burst[i];
        a->state = 0x10u;
        a->x = x;
        a->y = y;
        a->grid = 0u;
        a->frac = 0u;
        a->cnt = 0u;
    }
    CR->burst_room = F_ram_00EB_N3;
    CR->burst_level = F_ram_0010_N2;
}

/* NES OAM-style item entry; enemy_render adds the item-atlas marker. */
static void burst_add(unsigned char tile, unsigned char attrs, unsigned char x, unsigned char y)
{
    enemy_render_entry_t *e;
    if (CR->sprite_count >= COMBO_BURST_SPRITES) return;
    e = &CR->sprites[CR->sprite_count++];
    e->tile = tile;
    e->attrs = attrs;
    e->x = x;
    e->y = y;
}

static void burst_draw(unsigned char i, const BurstArrow *a)
{
    const unsigned char d = k_burst_dirs[i];
    const unsigned char pal = CR->burst_pal;
    unsigned char t, flip;
    if ((a->state & 0xF0u) == 0x20u) {             /* spark, palette row 5 */
        burst_add(draw_item_frame_tile(2u, 2u), 0x01u,
                                     (unsigned char)(a->x + 4u), a->y);
        return;
    }
    if ((d & 0x03u) == 0u) {                       /* up/down: narrow, centred */
        burst_add(draw_item_frame_tile(2u, 0u),
                                     (unsigned char)(pal | ((d & 0x04u) ? 0x80u : 0u)),
                                     (unsigned char)(a->x + 4u), a->y);
        return;
    }
    flip = (unsigned char)(((d & 0x02u) ? 0x40u : 0u) | ((d & 0x04u) ? 0x80u : 0u));
    if ((d & 0x0Cu) == 0u) {                       /* left/right: wide pair, y+3 */
        t = draw_item_frame_tile(2u, 1u);
        burst_add((d & 0x02u) ? (unsigned char)(t + 2u) : t,
                                     (unsigned char)(pal | flip), a->x,
                                     (unsigned char)(a->y + 3u));
        burst_add((d & 0x02u) ? t : (unsigned char)(t + 2u),
                                     (unsigned char)(pal | flip),
                                     (unsigned char)(a->x + 8u), (unsigned char)(a->y + 3u));
        return;
    }
    /* Diagonal: up-right art; H flip swaps the two columns. */
    burst_add(
        (d & 0x02u) ? ARROW_DIAG_RIGHT_NES_TILE : ARROW_DIAG_LEFT_NES_TILE,
        (unsigned char)(pal | flip), a->x, a->y);
    burst_add(
        (d & 0x02u) ? ARROW_DIAG_LEFT_NES_TILE : ARROW_DIAG_RIGHT_NES_TILE,
        (unsigned char)(pal | flip), (unsigned char)(a->x + 8u), a->y);
}

/* UpdateArrowOrBoomerang state $1x, on the borrowed slot. */
static void burst_fly(void)
{
    const unsigned char d = F_ram_0098_O(BURST_SLOT);
    unsigned char blocked = 0u;
    F_ram_000E_R = 0u;
    if (d & 0x03u) {
        F_ram_000F_R2 = d;
        object_move_shot((unsigned char)(d & 0x03u), BURST_SLOT);
        if (F_ram_000E_R & 0x80u) blocked = 1u;
        F_ram_000E_R = (unsigned char)(F_ram_000E_R + 1u);
    }
    if (!blocked && (d & 0x0Cu)) {
        F_ram_000F_R2 = d;
        object_move_shot((unsigned char)(d & 0x0Cu), BURST_SLOT);
        if (F_ram_000E_R & 0x80u) blocked = 1u;
    }
    if (blocked) {                                 /* HandleArrowOrBoomerangBlocked */
        F_ram_03D0_O(BURST_SLOT) = 3u;
        F_ram_00AC_O(BURST_SLOT) = (unsigned char)(F_ram_00AC_O(BURST_SLOT) + 0x10u);
        return;
    }
}

/* Enemy AI owns vulnerability, multipart centres and death tails. Borrow
 * slot18 only at that owner's final arrow check, never in the mover. */
void combo_burst_check_monster(unsigned int monster_slot)
{
    unsigned char slot[8], scratch[16], i, mask=0u, hit=0u;
    if (F_ram_04F0_X2(monster_slot) || F_NES_OBJ_METASTATE_BASE_O(monster_slot)) return;
    /* Exact reject-only form of Remix's slender arrow centre (+6,+8
     * vertical/diagonal, +8,+9 horizontal) and strict threshold11. The
     * owner already supplied its centre/part in scratch2/3. No side effects
     * survive a candidate miss, so distant arrows need no slot bridge. */
    for (i=0u;i<BURST_N;++i) {
        const BurstArrow *a=&CR->burst[i];
        const unsigned char vertical=(unsigned char)(k_burst_dirs[i]&0x0Cu);
        if ((a->state&0xF0u)!=0x10u) continue;
        if ((unsigned char)(nes_ram[2]-a->x-(vertical?6u:8u)+10u)>=21u ||
            (unsigned char)(nes_ram[3]-a->y-(vertical?8u:9u)+10u)>=21u) continue;
        mask |= (unsigned char)(1u<<i);
    }
    if (!mask) return;
    slot[0]=F_ram_00AC_O(BURST_SLOT);slot[1]=F_ram_0070_O(BURST_SLOT);
    slot[2]=F_ram_0084_O(BURST_SLOT);slot[3]=F_ram_0098_O(BURST_SLOT);
    slot[4]=F_ram_0394_O4(BURST_SLOT);slot[5]=F_ram_03A8_R4(BURST_SLOT);
    slot[6]=F_ram_03BC_O3(BURST_SLOT);slot[7]=F_ram_03D0_O(BURST_SLOT);
    for(i=0u;i<16u;++i)scratch[i]=nes_ram[i];
    for(i=0u;i<BURST_N;++i) {
        BurstArrow *a=&CR->burst[i];
        unsigned char j;
        if (!(mask & (1u<<i))) continue;
        /* A preceding arrow/weapon may have just made this actor invincible. */
        if (F_ram_04F0_X2(monster_slot) || F_NES_OBJ_METASTATE_BASE_O(monster_slot)) break;
        for(j=0u;j<16u;++j)nes_ram[j]=scratch[j];
        F_ram_00AC_O(BURST_SLOT)=a->state;F_ram_0070_O(BURST_SLOT)=a->x;
        F_ram_0084_O(BURST_SLOT)=a->y;F_ram_0098_O(BURST_SLOT)=k_burst_dirs[i];
        F_ram_03D0_O(BURST_SLOT)=a->cnt;
        CR->borrowed_arrow=1u;
        collision_check_monster_arrow_or_rod_single(monster_slot,BURST_SLOT);
        CR->borrowed_arrow=0u;
        hit |= nes_ram[6]; /* Ganon's owner consumes the collision result. */
        a->state=F_ram_00AC_O(BURST_SLOT);a->cnt=F_ram_03D0_O(BURST_SLOT);
    }
    F_ram_00AC_O(BURST_SLOT)=slot[0];F_ram_0070_O(BURST_SLOT)=slot[1];
    F_ram_0084_O(BURST_SLOT)=slot[2];F_ram_0098_O(BURST_SLOT)=slot[3];
    F_ram_0394_O4(BURST_SLOT)=slot[4];F_ram_03A8_R4(BURST_SLOT)=slot[5];
    F_ram_03BC_O3(BURST_SLOT)=slot[6];F_ram_03D0_O(BURST_SLOT)=slot[7];
    for(i=0u;i<16u;++i)nes_ram[i]=scratch[i];
    nes_ram[6] |= hit;
}

void combo_burst_update(void)
{
    unsigned char saved_slot[8], scratch[16];
    unsigned char i, any = 0u;
    CR->sprite_count = 0u;
    for (i = 0u; i < BURST_N; ++i) any |= CR->burst[i].state;
    if (!any) return;
    if (F_ram_00EB_N3 != CR->burst_room || F_ram_0010_N2 != CR->burst_level) {   /* left the room */
        combo_burst_clear();
        return;
    }
    saved_slot[0] = F_ram_00AC_O(BURST_SLOT);
    saved_slot[1] = F_ram_0070_O(BURST_SLOT);
    saved_slot[2] = F_ram_0084_O(BURST_SLOT);
    saved_slot[3] = F_ram_0098_O(BURST_SLOT);
    saved_slot[4] = F_ram_0394_O4(BURST_SLOT);
    saved_slot[5] = F_ram_03A8_R4(BURST_SLOT);
    saved_slot[6] = F_ram_03BC_O3(BURST_SLOT);
    saved_slot[7] = F_ram_03D0_O(BURST_SLOT);
    for (i = 0u; i < 16u; ++i) scratch[i] = nes_ram[i];
    for (i = 0u; i < BURST_N; ++i) {
        BurstArrow *a = &CR->burst[i];
        if (!a->state) continue;
        F_ram_00AC_O(BURST_SLOT) = a->state;
        F_ram_0070_O(BURST_SLOT) = a->x;
        F_ram_0084_O(BURST_SLOT) = a->y;
        F_ram_0098_O(BURST_SLOT) = k_burst_dirs[i];
        F_ram_0394_O4(BURST_SLOT) = a->grid;
        F_ram_03A8_R4(BURST_SLOT) = a->frac;
        F_ram_03BC_O3(BURST_SLOT) = 0xC0u;         /* WieldArrow q-speed */
        F_ram_03D0_O(BURST_SLOT) = a->cnt;
        if ((a->state & 0xF0u) == 0x10u) {
            burst_fly();
        } else {
            F_ram_00AC_O(BURST_SLOT) = 0x28u;      /* CheckState20 */
            F_ram_03D0_O(BURST_SLOT) = (unsigned char)(F_ram_03D0_O(BURST_SLOT) - 1u);
            if (F_ram_03D0_O(BURST_SLOT) == 0u) F_ram_00AC_O(BURST_SLOT) = 0u;
        }
        a->state = F_ram_00AC_O(BURST_SLOT);
        a->x = F_ram_0070_O(BURST_SLOT);
        a->y = F_ram_0084_O(BURST_SLOT);
        a->grid = F_ram_0394_O4(BURST_SLOT);
        a->frac = F_ram_03A8_R4(BURST_SLOT);
        a->cnt = F_ram_03D0_O(BURST_SLOT);
        if (a->state) burst_draw(i, a);
    }
    F_ram_00AC_O(BURST_SLOT) = saved_slot[0];
    F_ram_0070_O(BURST_SLOT) = saved_slot[1];
    F_ram_0084_O(BURST_SLOT) = saved_slot[2];
    F_ram_0098_O(BURST_SLOT) = saved_slot[3];
    F_ram_0394_O4(BURST_SLOT) = saved_slot[4];
    F_ram_03A8_R4(BURST_SLOT) = saved_slot[5];
    F_ram_03BC_O3(BURST_SLOT) = saved_slot[6];
    F_ram_03D0_O(BURST_SLOT) = saved_slot[7];
    for (i = 0u; i < 16u; ++i) nes_ram[i] = scratch[i];
}
