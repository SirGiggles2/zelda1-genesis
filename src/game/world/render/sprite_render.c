/* NES source: reference/aldonunez/Z_01.asm:Anim_ItemFrameTiles,
 *             Z_02.asm:CommonSpritePatterns.
 * Drained C: src/game/world/draw_dispatch.c:anim_write_specific_item_sprites
 *            (item descriptor owner; no separate renderer runtime candidate).
 * Coverage: PARTIAL (persistent common pairs).
 * Stance: EXTEND.
 *
 * Phase 12.2 SGDK-1 cleanup: dropped <genesis.h>; SAT writes route
 * through render_set_sprite_full; RENDER_SPRITE_SIZE/RENDER_TILE_ATTR_FULL/PALn
 * use portable RENDER_* macros from render_abi.h. */
#include "sprite_render.h"
#include "../../enemies/enemy_render.h"
#include "render_abi.h"
#include "sprite_slots.h"
#include "../../../../engine/src/vram_layout.h"
/* FU3+FU4: renderer reads from atlas/items_chr_x4 (byte-identical to
 * legacy expanded_sprite_chr via FU2).  ROOMROM_ITEM_TILE_* tile-index
 * constants are now in atlas/items_chr_x4.h (supersede roomrom_item_chr.h,
 * same values).  vram_layout.h includes atlas/items_chr_x4.h, so
 * ROOMROM_ITEM_TILE_* are already visible through that path.
 * P4a: atlas headers for ATLAS_ASSERT_SIZE and named dispatch constants. */
#include "../../../../engine/src/atlas/items_chr_x4.h"
#include "../../../../engine/src/atlas/items_chr.h"
#include "../../inventory/inventory_sprite_chr.h"
#include "../../../../engine/src/atlas/atlas_dispatch.h"

/* Phase AA (2026-05-18 cleanup org): sub-pal routing moved to the
 * shared API in src/game/world/render/subpal_routing.h. The
 * ROOMROM_SUBPAL_PAL macro is preserved as a wrapper for source-diff
 * minimization in call sites; new code should call
 * roomrom_spr_subpal_to_pal() directly. */
#include "subpal_routing.h"
#include "platform_abi.h"                   /* nes_ram: CurLevel, RoomId */
#include "../../dungeon/uw_render.h"        /* level / quest of the UW room */
#include "../../dungeon/cellar_meta.h"      /* roomrom_uw_room_is_cellar */
#define ROOMROM_SUBPAL_PAL(s) roomrom_spr_subpal_to_pal((unsigned char)(s))

/* Compile-time dispatch size checks. NES Z1 PPU runs in 8x16 sprite mode
 * (PPUCTRL bit 5 = 1) during gameplay, so item sprites that use a single
 * OAM entry render as 8x16 (paired tiles) — not 8x8.
 * BOMB       -> RENDER_SPRITE_SIZE(1,2): tiles $34 (top: fuse) + $35 (bottom: body)
 * BOOMERANG3 -> RENDER_SPRITE_SIZE(1,2): in-flight $36/$38/$3A/$3C paired with
 *               $37/$39/$3B/$3D (top + bottom halves). 'BOOMERANG' (W=1,H=1)
 *               is the static B-icon variant at tile $4C (separate atlas
 *               entry). roomrom_sprites_set_boomerang renders the in-flight
 *               BOOMERANG3 entry. */
ATLAS_ASSERT_SIZE(BOMB, 1, 2);
ATLAS_ASSERT_SIZE(BOOMERANG3, 1, 2);

/* TODO(Phase 6 cleanup): add ATLAS_ASSERT_SIZE for sword_vert/horz,
 * arrow_vert/horz, explosion, sword_diag once items_chr.h gains multi-tile
 * W/H dispatch entries for those draw rules (NES_NARROW 1x2, NES_SLIM 2x1).
 * Also replace ROOMROM_ITEM_TILE_* tile-index expressions with
 * ROOMROM_ATLAS_ITEMS_<NAME>_OFFSET / 32 once the atlas ordering is
 * reconciled with the legacy blob ordering. */

/* P4b: Link renderer migration - DEFERRED.
 * link_chr.h now emits W_x/H_x dispatch defines (FU1). However, the Link
 * upload path still reads tiles from common_chr via upload_pose(); the atlas
 * link_chr.c blob is not compiled into the build.  ATLAS_ASSERT_SIZE for
 * Link poses is deferred until the upload path switches to atlas/link_chr.
 *
 * TODO(Phase-4b / Phase 6): switch upload_pose() to read from atlas/link_chr;
 * replace LINK_VRAM_TILE + pose offsets with
 *   ROOMROM_LINK_TILE_BASE + ROOMROM_ATLAS_LINK_<POSE>_OFFSET / 32
 * and add ATLAS_ASSERT_SIZE(FACE_DOWN_F1, 2, 2) etc. per-pose. */

/* Sprite CHR source.
 * common_chr (data/chr/common.c) holds the always-loaded sprites including
 * Link, sword, heart. sprites_chr (OW enemies) is OUT-OF-SCOPE for engine
 * currently and is no longer uploaded -- frees ~232 tiles in the SPR bank.
 * NES tile IDs in common_chr are 1:1 (NES tile $58 = common_chr + 0x58*32). */
extern const unsigned char common_chr[7616];
extern const unsigned char demo_chr[8768];   /* T-011: NES sprite tiles $70.. */

#define COMMON_VRAM_TILE_BASE   ROOMROM_SPR_TILE_BASE
#define COMMON_CHR_BYTES        7616u
#define COMMON_BLOCK_TILE_COUNT 238u

#define LINK_VRAM_TILE          (COMMON_VRAM_TILE_BASE + COMMON_BLOCK_TILE_COUNT)
#define LINK_TILES_PER_POSE     4u
#define LINK_POSE_COUNT         8u   /* 4 facings x 2 walk frames */

_Static_assert(ROOMROM_LINK_FLASH3_TILE_BASE ==
                   ROOMROM_LINK_LIFT_TILE_BASE + ROOMROM_LINK_LIFT_TILE_COUNT,
               "Link flash tiles must follow the item-lift pair");
_Static_assert(ROOMROM_LINK_FLASH3_TILE_COUNT ==
                   LINK_POSE_COUNT * LINK_TILES_PER_POSE,
               "Link flash bank must hold every walk pose");
_Static_assert(ROOMROM_LINK_ATTACK_FLASH3_TILE_BASE ==
                   ROOMROM_LINK_FLASH3_TILE_BASE + ROOMROM_LINK_FLASH3_TILE_COUNT,
               "Link attack flash tiles must follow walk flash tiles");

/* S7 v4: attack poses follow the 8 walk poses. 4 attack poses (one per
 * facing), 4 tiles each = 16 contiguous tiles. */
#define ATTACK_POSE_COUNT       4u
#define ATTACK_VRAM_TILE        (LINK_VRAM_TILE + LINK_POSE_COUNT * LINK_TILES_PER_POSE)

_Static_assert(ROOMROM_LINK_ATTACK_FLASH3_TILE_COUNT ==
                   ATTACK_POSE_COUNT * LINK_TILES_PER_POSE,
               "Link attack flash bank must hold every attack pose");

/* HUD backdrop sprite strip retired 2026-05-15. H32 SAT is gameplay-only;
 * opaque black HUD underlay comes from BG_A tile 0 (PAL0 color 0) via
 * clear_hud_underlay_for_row_base() in engine/src/main.c. Slot contract
 * lives in sprite_slots.h: 0..9 gameplay, 10..63 enemy bridge. */

/* Phase 1: item atlas tiles live in their own contiguous block starting
 * after Link attack poses. Tile offsets come from atlas/items_chr_x4.h
 * (ROOMROM_ITEM_TILE_*).
 *
 * Phase 8 W0c fix 2026-05-20: align with canonical ROOMROM_ITEM_TILE_BASE
 * (= SPR_TILE_BASE + SPR_TILE_COUNT_PER_PAL = 533 + 287 = 820). Previous
 * hand-math (771 + 32 + 16 = 819) was off by 1 vs the 287-tile SPR bank
 * size (which includes 1 slack tile). Sword vert top byte-diff probe
 * confirmed slot 819 = blank, slot 820 = actual sword vert top. */
#define ITEM_VRAM_TILE          ROOMROM_ITEM_TILE_BASE_PAL(0)

/* Compile-time guard: VRAM bases must match canonical vram_layout.h
 * layout. Off-by-one here = blank-tile beam / weapon sprites (regressed
 * 2026-05-20 by hand-math drift). DO NOT redefine ITEM_VRAM_TILE without
 * updating this assertion. */
#ifdef __STDC_VERSION__
_Static_assert(ITEM_VRAM_TILE == ROOMROM_ITEM_TILE_BASE_PAL(0),
               "ITEM_VRAM_TILE must equal canonical ROOMROM_ITEM_TILE_BASE_PAL(0). "
               "Hand-computed math drifts; always derive from vram_layout.h.");
#endif

#define SWORD_VERT_VRAM_TILE    (ITEM_VRAM_TILE + ROOMROM_ITEM_TILE_SWORD_VERT)
#define SWORD_HORZ_VRAM_TILE    (ITEM_VRAM_TILE + ROOMROM_ITEM_TILE_SWORD_HORZ)
#define BOOMERANG_VRAM_TILE     (ITEM_VRAM_TILE + ROOMROM_ITEM_TILE_BOOMERANG)
#define ARROW_VERT_VRAM_TILE    (ITEM_VRAM_TILE + ROOMROM_ITEM_TILE_ARROW_VERT)
#define ARROW_HORZ_VRAM_TILE    (ITEM_VRAM_TILE + ROOMROM_ITEM_TILE_ARROW_HORZ)
#define BOMB_VRAM_TILE          (ITEM_VRAM_TILE + ROOMROM_ITEM_TILE_BOMB)
#define EXPLOSION_VRAM_TILE     (ITEM_VRAM_TILE + ROOMROM_ITEM_TILE_EXPLOSION)
#define SWORD_DIAG_VRAM_TILE    (ITEM_VRAM_TILE + ROOMROM_ITEM_TILE_SWORD_DIAG)

typedef struct {
    unsigned char nes_ids[4];     /* TL, BL, TR, BR (Genesis 2x2 column-major) */
    unsigned char per_tile_hflip; /* bitmask: bit 0 = TL flipped, bit 1 = BL, etc. */
} link_pose_def_t;

/* Walk poses. pose_index = face*2 + frame. Captured from NES live OAM. */
static const link_pose_def_t link_poses[LINK_POSE_COUNT] = {
    /* DOWN  frame 0 */ { {0x58u, 0x59u, 0x0Au, 0x0Bu}, 0x0u },
    /* DOWN  frame 1 */ { {0x5Au, 0x5Bu, 0x08u, 0x09u}, 0xCu },
    /* UP    frame 0 */ { {0x0Cu, 0x0Du, 0x0Eu, 0x0Fu}, 0x0u },
    /* UP    frame 1 */ { {0x0Eu, 0x0Fu, 0x0Cu, 0x0Du}, 0xFu },
    /* LEFT  frame 0 */ { {0x06u, 0x07u, 0x04u, 0x05u}, 0xFu },
    /* LEFT  frame 1 */ { {0x02u, 0x03u, 0x00u, 0x01u}, 0xFu },
    /* RIGHT frame 0 */ { {0x04u, 0x05u, 0x06u, 0x07u}, 0x0u },
    /* RIGHT frame 1 */ { {0x00u, 0x01u, 0x02u, 0x03u}, 0x0u },
};

/* Attack poses (Link wielding sword, body sprite changes during the swing
 * window — Z1 sets Player ObjState = $10 when WieldSword fires). NES tile
 * IDs from probe_nes_link_swing_capture.lua, 2026-04-30:
 *   DOWN: slot18=$14, slot19=$16, no flip
 *   UP  : slot18=$18, slot19=$1A, no flip
 *   LEFT: slot18=$12 hflip, slot19=$10 hflip (mirror of RIGHT)
 *   RIGHT:slot18=$10, slot19=$12, no flip
 * In 8x16 NES sprite mode, slot18 tile $XX expands to common_chr tiles
 * $XX (top) + $XX+1 (bottom). So 4 8x8 tiles per facing. */
static const link_pose_def_t attack_poses[ATTACK_POSE_COUNT] = {
    /* DOWN  */ { {0x14u, 0x15u, 0x16u, 0x17u}, 0x0u },
    /* UP    */ { {0x18u, 0x19u, 0x1Au, 0x1Bu}, 0x0u },
    /* LEFT  */ { {0x12u, 0x13u, 0x10u, 0x11u}, 0xFu },
    /* RIGHT */ { {0x10u, 0x11u, 0x12u, 0x13u}, 0x0u },
};

/* Phase 1: item-atlas variant selector. 0 = orig, 1 = redux. Read at
 * upload time by roomrom_sprites_upload_chr to pick the correct slice
 * of roomrom_atlas_items_x4[][]. */
static unsigned char s_item_chr_variant = 0u;  /* ROOMROM_ITEM_VARIANT_ORIG */

/* Cache only covers gameplay slots 0..9. Enemy slots 10..63 are written
 * directly via render_abi (enemy_render.c) without per-slot caching. */
#define ROOMROM_SPRITE_CACHE_COUNT (ROOMROM_SPRITE_SLOT_GAMEPLAY_LAST + 1u)

typedef struct {
    signed short x;
    signed short y;
    unsigned short size;
    unsigned short attr;
    unsigned short link;
    unsigned char valid;
} roomrom_sprite_cache_t;

static roomrom_sprite_cache_t s_sprite_cache[ROOMROM_SPRITE_CACHE_COUNT];

/* Door-mask state last written (0 off, 1 on, $FF unknown). */
static unsigned char s_door_masks_state = 0xFFu;

void roomrom_sprites_invalidate_cache(void)
{
    unsigned char i;
    s_door_masks_state = 0xFFu;
    for (i = 0u; i < ROOMROM_SPRITE_CACHE_COUNT; i++) {
        s_sprite_cache[i].valid = 0u;
    }
}

static void roomrom_sprites_set_full_cached(unsigned short slot,
                                            signed short   x,
                                            signed short   y,
                                            unsigned short size,
                                            unsigned short attr,
                                            unsigned short link)
{
    roomrom_sprite_cache_t *c;
    /* Every slot but the HUD ones (already in Genesis rows) is play area. */
    if (slot < ROOMROM_SPRITE_SLOT_HUD_B_ITEM || slot > ROOMROM_SPRITE_SLOT_HUD_COMPASS)
        y = (signed short)(y + ROOMROM_PLAY_SPRITE_DY);
    /* NES item and weapon sprites draw in front of the background; in the
     * UW the edge and door BG tiles are high priority (T-131), so these
     * sprites are too. */
    if (nes_ram[0x0010u] != 0u &&
        slot >= ROOMROM_SPRITE_SLOT_SWORD && slot <= ROOMROM_SPRITE_SLOT_MAGIC_SHOT)
        attr = (unsigned short)(attr | 0x8000u);
    if (slot < ROOMROM_SPRITE_CACHE_COUNT) {
        c = &s_sprite_cache[slot];
        if (c->valid != 0u &&
            c->x == x && c->y == y &&
            c->size == size && c->attr == attr && c->link == link) {
            return;
        }
        c->x = x;
        c->y = y;
        c->size = size;
        c->attr = attr;
        c->link = link;
        c->valid = 1u;
    }
    /* Phase 12.2 SGDK-1 cleanup: route SAT slot write through adapter. */
    render_set_sprite_full(slot, x, y, size, attr, link);
}

#define VDP_setSpriteFull roomrom_sprites_set_full_cached

void roomrom_sprites_set_redux(unsigned char redux)
{
    s_item_chr_variant = redux ? 1u : 0u;
}

/* Horizontally flip a Genesis 4bpp 8x8 tile (32 bytes) in place.
 * Each row is 4 bytes (2 pixels per byte, high nibble = left pixel). */
static void hflip_tile_inplace(unsigned char *t)
{
    unsigned char r;
    for (r = 0; r < 8; r++) {
        unsigned char b0 = t[r*4 + 0], b1 = t[r*4 + 1],
                      b2 = t[r*4 + 2], b3 = t[r*4 + 3];
        t[r*4 + 0] = (unsigned char)(((b3 & 0xF0u) >> 4) | ((b3 & 0x0Fu) << 4));
        t[r*4 + 1] = (unsigned char)(((b2 & 0xF0u) >> 4) | ((b2 & 0x0Fu) << 4));
        t[r*4 + 2] = (unsigned char)(((b1 & 0xF0u) >> 4) | ((b1 & 0x0Fu) << 4));
        t[r*4 + 3] = (unsigned char)(((b0 & 0xF0u) >> 4) | ((b0 & 0x0Fu) << 4));
    }
}

/* Helper: upload one pose's 4 tiles into VRAM, baking per-tile hflip. */
#ifndef COMMON_SPRITE_PATTERN_TILE_COUNT
/* NES Z1 always-loaded sprite pattern block: tiles 0x00..0x6F (= 112)
 * are the canonical Link/Sword/Boomerang/etc. CHR. Any pose tile_id
 * >= 112 means the source CHR isn't in common_chr — should be sourced
 * from item atlas (Phase 1) or a future per-room CHR slot. Filling
 * with zeros prevents accidental garbage from extractor over-reach. */
#define COMMON_SPRITE_PATTERN_TILE_COUNT 112u
#endif

static void upload_pose(unsigned short vram_tile_base,
                        const link_pose_def_t *pose,
                        unsigned char pixel_bias)
{
    unsigned char t, i;
    unsigned char buf[32];
    for (t = 0; t < LINK_TILES_PER_POSE; t++) {
        unsigned short nes_off = (unsigned short)pose->nes_ids[t] * 32u;
        if (pose->nes_ids[t] >= COMMON_SPRITE_PATTERN_TILE_COUNT) {
            for (i = 0; i < 32; i++) buf[i] = 0;
        } else {
            for (i = 0; i < 32; i++) buf[i] = common_chr[nes_off + i];
        }
        if (pose->per_tile_hflip & (1u << t)) {
            hflip_tile_inplace(buf);
        }
        if (pixel_bias != 0u) {
            for (i = 0u; i < 32u; ++i) {
                const unsigned char hi = (unsigned char)(buf[i] >> 4);
                const unsigned char lo = (unsigned char)(buf[i] & 0x0Fu);
                buf[i] = (unsigned char)
                    (((hi ? (unsigned char)(hi + pixel_bias) : 0u) << 4) |
                     (lo ? (unsigned char)(lo + pixel_bias) : 0u));
            }
        }
        render_chr_upload(
            (unsigned short)((vram_tile_base + t) * 32u),
            buf, 32u);
    }
}

void roomrom_sprites_upload_persistent_chr(void)
{
    /* Phase 3: sprite bank is 1x (sub-pal 0 only). sprites_chr (OW
     * enemies) deferred -- not uploaded. Common gameplay sprite block
     * (Link, sword, hearts) goes first. */
    render_chr_upload((unsigned short)(COMMON_VRAM_TILE_BASE * 32u),
                      common_chr, COMMON_CHR_BYTES);
    /* HUD position marker $3E/$3F outside the scene overlay. */
    render_chr_upload((unsigned short)(ROOMROM_HUD_MARKER_TILE_BASE * 32u),
                      common_chr + 0x3Eu * 32u,
                      ROOMROM_HUD_MARKER_TILE_COUNT * 32u);
    /* T-195: live NES shop OAM tile $56, paired with $57. Source bytes
     * match the ROM-derived common block; protect them from OW/UW banks. */
    render_chr_upload((unsigned short)(ROOMROM_SHIELD_TILE_BASE * 32u),
                      common_chr + 0x56u * 32u,
                      ROOMROM_SHIELD_TILE_COUNT * 32u);
    /* T-011: Link item-lift pair $78/$79 outside the scene overlay. The
     * NES sprite table's $70.. tiles are the extracted demo block (tile k
     * = NES $70 + k; byte-matched to NES CHR-RAM in a cave, t011 f520). */
    render_chr_upload((unsigned short)(ROOMROM_LINK_LIFT_TILE_BASE * 32u),
                      demo_chr + (0x78u - 0x70u) * 32u,
                      ROOMROM_LINK_LIFT_TILE_COUNT * 32u);

    /* Walk poses (32 tiles). */
    {
        unsigned char p;
        for (p = 0; p < LINK_POSE_COUNT; p++) {
            upload_pose(
                (unsigned short)(LINK_VRAM_TILE + p * LINK_TILES_PER_POSE),
                &link_poses[p], 0u);
        }
    }

    /* NES Anim_WriteSpritePair may choose sprite sub-pal 3 while Link is
     * hurt. Keep one permanent biased walk bank for PAL1[13..15]; no
     * per-frame CHR upload or BG-palette mutation. */
    {
        unsigned char p;
        for (p = 0u; p < LINK_POSE_COUNT; ++p) {
            upload_pose(
                (unsigned short)(ROOMROM_LINK_FLASH3_TILE_BASE +
                                 p * LINK_TILES_PER_POSE),
                &link_poses[p], 12u);
        }
    }

    /* Attack poses (16 tiles). */
    {
        unsigned char p;
        for (p = 0; p < ATTACK_POSE_COUNT; p++) {
            upload_pose(
                (unsigned short)(ATTACK_VRAM_TILE + p * LINK_TILES_PER_POSE),
                &attack_poses[p], 0u);
            upload_pose(
                (unsigned short)(ROOMROM_LINK_ATTACK_FLASH3_TILE_BASE +
                                 p * LINK_TILES_PER_POSE),
                &attack_poses[p], 12u);
        }
    }

    /* The pause inventory's 16 live-extracted tiles stay resident in
     * their reservation (1280..1295; the pause re-uploads the same
     * bytes): the gameplay HUD uses the compass marker pair (14/15, NES
     * attr $03 drawn with the pixel-9 biased $3E pair on PAL0) before
     * pause opens, and the world raft $6C (4/5) and ladder $76 (6/7) use
     * theirs (T-056: neither is in the items atlas; its $76 entry is
     * not the ladder). */
    render_chr_upload((unsigned short)(ROOMROM_SUBSCREEN_SPRITE_TILE_BASE * 32u),
                      &k_inventory_sprite_chr[0][0],
                      (unsigned short)(ROOMROM_SUBSCREEN_SPRITE_TILE_COUNT * 32u));

    /* Prepare fixed FX with persistent CHR, before controller play.
     * First use during a Patra beam burst/death previously missed VBlank. */
    enemy_render_prepare_fx_chr();
}

unsigned char roomrom_sprites_item_chr_variant(void)
{
    return s_item_chr_variant;
}

void roomrom_sprites_upload_items_chr(void)
{
    /* FU3 (atlas pipeline): N sub-pal copies of the item atlas from the
     * atlas/items_chr_x4 blob. N = ROOMROM_ITEM_SUBPAL_COUNT (3 since
     * 2026-05-08 sub-pal-3-drop unblock; was 4 prior). Blob layout:
     * pal0_bytes||pal1_bytes||...||palN-1_bytes; per-pal stride =
     * ROOMROM_ATLAS_ITEMS_X4_PER_PAL_BYTES.
     *
     * Items live at ROOMROM_ITEM_TILE_BASE — outside the SCENE_OBJ
     * range (1069..1204), so safe to re-upload per scene_load. */
    unsigned short variant = s_item_chr_variant;
    unsigned char  s;
    for (s = 0; s < ROOMROM_ITEM_SUBPAL_COUNT; s++) {
        unsigned short vram_tile = (unsigned short)ROOMROM_ITEM_TILE_BASE_PAL(s);
        unsigned long  blob_off  = (unsigned long)(ROOMROM_ATLAS_ITEMS_X4_PER_PAL_BYTES)
                                   * (unsigned long)s;
        render_chr_upload(
            (unsigned short)(vram_tile * 32u),
            &roomrom_atlas_items_x4[variant][blob_off],
            (unsigned short)ROOMROM_ATLAS_ITEMS_X4_PER_PAL_BYTES
        );
    }
}

void roomrom_sprites_upload_chr(void)
{
    roomrom_sprites_upload_persistent_chr();
    roomrom_sprites_upload_items_chr();
}

void roomrom_sprites_load_palette(void)
{
    /* Phase 4: RENDER_PAL1 holds NES sprite PALRAM, loaded by the BG palette path
     * (roomrom_bg_palette_load_palram_full writes RENDER_PAL0 + RENDER_PAL1 from full
     * 32-byte NES PALRAM). This stub kept for ABI compatibility with
     * existing call sites. */
}

/* T-092: NES Link_EndMoveAndAnimate @Animate (Z_07.asm) draws Link two
 * pixels lower in the overworld (CurLevel 0, caves included) and in
 * cellars (GameMode 9); normal dungeon rooms use ObjY as is. Lockstep OW
 * captures: NES OAM Y = ObjY + 2 (t050_pond_fairy $AD/$AF, newgame
 * $5D/$5F). Native cellars use mode 9; retain the legacy metadata
 * fallback for direct scene fixtures, but do not require declared-list
 * membership for a controller-earned cellar (T-221 L3 raft). */
static short link_draw_y(short y)
{
    if (nes_ram[0x0010u] == 0u || nes_ram[0x0012u] == 9u ||
        roomrom_uw_room_is_cellar(roomrom_uw_room_render_get_level(),
                                  roomrom_uw_room_render_get_quest(),
                                  nes_ram[0x00EBu]))
        return (short)(y + 2);
    return y;
}

/* T-131: NES draws Link as two 8x16 sprites (OAM 18/19). In the UW,
 * ShowLinkSpritesBehindHorizontalDoors (Z_01.asm) sets the behind-BG
 * bit on a half whose X is below $10 or at $E9 and above (inside the W/E
 * door frames), and WriteBlankPrioritySprites keeps eight blank sprites on
 * the rows at Y $3D and $DD so the 8-sprite line limit hides Link (and
 * any later sprite) under the N/S door frames. Genesis: door tiles are
 * high priority, so a half in front is priority 1 and a half behind
 * priority 0; the rows are masked by an X = 0 sprite after an X != 0 lead
 * (slots 0..3). Lockstep t131 snapshots f1726/f1910: NES #18 x$0F a$60,
 * #19 x$17 a$40; blank sprites #0-#15 y$3D/$DD. The OW keeps priority 0
 * (cave arch hi-prio marks) and no masks. */
static unsigned char link_half_prio(short hx)
{
    unsigned char x = (unsigned char)hx;
    /* NES source: Z_05.asm:UpdateMode10Stairs_Full / PutLinkBehindBackground.
     * Drained C: cave_fade.c:cave_fade_mark_arch_hi_prio and this renderer.
     * Coverage: FULL dungeon stairs priority; normal door rules retained.
     * Stance: EXTEND. CurLevel already names the destination while Link is
     * still outside. InitMode10 preserves foreground; subsequent stairs
     * updates put both halves behind BG, regardless of horizontal position. */
    if (nes_ram[0x0012u] == 0x10u && nes_ram[0x0010u] != 0u)
        return nes_ram[0x0011u] ? 0u : 1u;
    if (nes_ram[0x0010u] == 0u) return 0u;
    return (x >= 0xE9u || x < 0x10u) ? 0u : 1u;
}

static void set_door_masks(void)
{
    static const unsigned char k_band_y[2] = { 0x3Du, 0xDDu };
    unsigned char b, on = (nes_ram[0x0010u] != 0u &&
                          nes_ram[0x0012u] != 0x10u && nes_ram[0x0012u] != 9u) ? 1u : 0u;
    if (on == s_door_masks_state) return;     /* SAT already holds them */
    s_door_masks_state = on;
    for (b = 0u; b < 2u; ++b) {
        unsigned short lead = (unsigned short)(ROOMROM_SPRITE_SLOT_MASK_N_LEAD + 2u * b);
        signed short y = on ? (signed short)k_band_y[b] : (signed short)-32;
        /* Screen X -127 / -128 = SAT X 1 / 0, both off the left edge. */
        VDP_setSpriteFull(lead, (signed short)-127, y, RENDER_SPRITE_SIZE(1, 2),
                          RENDER_TILE_ATTR_FULL(RENDER_PAL1, 0, 0, 0, 0),
                          ROOMROM_SPRITE_NEXT(lead));
        VDP_setSpriteFull((unsigned short)(lead + 1u), (signed short)-128, y,
                          RENDER_SPRITE_SIZE(1, 2),
                          RENDER_TILE_ATTR_FULL(RENDER_PAL1, 0, 0, 0, 0),
                          ROOMROM_SPRITE_NEXT(lead + 1u));
    }
}

static void set_link_halves(short x, short y, unsigned short tile,
                            unsigned char pal_index)
{
    signed short gy = (signed short)link_draw_y(y);
    /* Whirlwind teleport: the pickup hides Link's sprites (Sprites+72/76
     * = $F8) and nothing draws him (UpdatePlayer returns while halted,
     * Link_EndMoveAndAnimate while WhirlwindTeleportingState != 0) until
     * the drop-off (Z_01.asm UpdateWhirlwind_Full; T-171). */
    if (nes_ram[0x0522u] != 0u) {
        x = -32;
        gy = -32;
    }
    set_door_masks();
    VDP_setSpriteFull(ROOMROM_SPRITE_SLOT_LINK, (signed short)x, gy,
                      RENDER_SPRITE_SIZE(1, 2),
                      RENDER_TILE_ATTR_FULL(pal_index, link_half_prio(x), 0, 0, tile),
                      ROOMROM_SPRITE_NEXT(ROOMROM_SPRITE_SLOT_LINK));
    VDP_setSpriteFull(ROOMROM_SPRITE_SLOT_LINK_R, (signed short)(x + 8), gy,
                      RENDER_SPRITE_SIZE(1, 2),
                      RENDER_TILE_ATTR_FULL(pal_index, link_half_prio((short)(x + 8)),
                                            0, 0, (unsigned short)(tile + 2u)),
                      ROOMROM_SPRITE_NEXT(ROOMROM_SPRITE_SLOT_LINK_R));
}

/* T-011: NES DrawLinkLiftingItem pose: left half $78/$79, right half
 * $78/$79 h-flipped (two hands) or $08/$09 h-flipped for a half-width
 * item (one hand). Link palette (NES sprite sub-pal 0 = PAL1). */
void roomrom_sprites_set_link_lift(short x, short y, unsigned char one_hand)
{
    const unsigned short left = RENDER_TILE_ATTR_FULL(RENDER_PAL1, 0, 0, 0,
                                                      ROOMROM_LINK_LIFT_TILE_BASE);
    const unsigned short right = RENDER_TILE_ATTR_FULL(RENDER_PAL1, 0, 0, 1,
        one_hand ? (unsigned short)(COMMON_VRAM_TILE_BASE + 0x08u)
                 : (unsigned short)ROOMROM_LINK_LIFT_TILE_BASE);
    roomrom_sprites_set_link_sat(x, y, left, right);
}

/* T-011: Link's two 8x16 halves from Genesis SAT words at ObjY as is:
 * DrawLinkLiftingItem takes the position from
 * Anim_FetchObjPosForSpriteDescriptor, without the walk draw's OW +2
 * (link_draw_y; NES OAM t011 f504: Link Y $9D = ObjY). The priority bit
 * follows the Link door rule like set_link_halves. */
void roomrom_sprites_set_link_sat(short x, short y,
                                  unsigned short left_sat, unsigned short right_sat)
{
    signed short gy = (signed short)y;
    set_door_masks();
    VDP_setSpriteFull(ROOMROM_SPRITE_SLOT_LINK, (signed short)x, gy,
                      RENDER_SPRITE_SIZE(1, 2),
                      (unsigned short)((left_sat & 0x7FFFu) |
                                       ((unsigned short)link_half_prio(x) << 15)),
                      ROOMROM_SPRITE_NEXT(ROOMROM_SPRITE_SLOT_LINK));
    VDP_setSpriteFull(ROOMROM_SPRITE_SLOT_LINK_R, (signed short)(x + 8), gy,
                      RENDER_SPRITE_SIZE(1, 2),
                      (unsigned short)((right_sat & 0x7FFFu) |
                                       ((unsigned short)link_half_prio((short)(x + 8)) << 15)),
                      ROOMROM_SPRITE_NEXT(ROOMROM_SPRITE_SLOT_LINK_R));
}

void roomrom_sprites_set_link_pose(short x, short y,
                                   link_face_t face, unsigned char frame)
{
    roomrom_sprites_set_link_pose_pal(x, y, face, frame, RENDER_PAL1);
}

void roomrom_sprites_set_link_pose_pal(short x, short y,
                                       link_face_t face, unsigned char frame,
                                       unsigned char pal_index)
{
    unsigned short pose_idx = (unsigned short)face * 2u + (unsigned short)frame;
    unsigned short tile = LINK_VRAM_TILE + pose_idx * LINK_TILES_PER_POSE;
    if (pal_index > 3u) pal_index = RENDER_PAL1;
    set_link_halves(x, y, tile, pal_index);
}

void roomrom_sprites_set_link_hurt_pose(short x, short y,
                                       link_face_t face, unsigned char frame,
                                       unsigned char invincibility_timer)
{
    unsigned char subpal = (unsigned char)(invincibility_timer & 3u);
    unsigned short pose_idx = (unsigned short)face * 2u + (unsigned short)frame;
    unsigned short tile = LINK_VRAM_TILE + pose_idx * LINK_TILES_PER_POSE;
    unsigned char pal = (unsigned char)(RENDER_PAL1 + subpal);
    if (subpal == 3u) {
        tile = (unsigned short)(ROOMROM_LINK_FLASH3_TILE_BASE +
                                pose_idx * LINK_TILES_PER_POSE);
        pal = RENDER_PAL1;
    }
    set_link_halves(x, y, tile, pal);
}

void roomrom_sprites_set_link_attack_pose(short x, short y, link_face_t face)
{
    unsigned short pose_idx = (unsigned short)face;
    unsigned short tile = ATTACK_VRAM_TILE + pose_idx * LINK_TILES_PER_POSE;
    const unsigned char timer = nes_ram[0x04F0u];
    unsigned char pal = RENDER_PAL1;
    if (timer != 0u) {
        const unsigned char subpal = (unsigned char)(timer & 3u);
        if (subpal == 3u) {
            tile = (unsigned short)(ROOMROM_LINK_ATTACK_FLASH3_TILE_BASE +
                                    pose_idx * LINK_TILES_PER_POSE);
        } else {
            pal = (unsigned char)(RENDER_PAL1 + subpal);
        }
    }
    set_link_halves(x, y, tile, pal);
}

void roomrom_sprites_spawn_link(short x, short y)
{
    /* SAT chain: 0 (Link) -> 1 (sword) -> 2 (beam) -> 3 (boomerang) -> end. */
    roomrom_sprites_invalidate_cache();
    VDP_setSpriteFull(ROOMROM_SPRITE_SLOT_SWORD,
                      (signed short)-32,
                      (signed short)-32,
                      RENDER_SPRITE_SIZE(1, 2),
                      RENDER_TILE_ATTR_FULL(RENDER_PAL1,0, 0, 0, SWORD_VERT_VRAM_TILE),
                      ROOMROM_SPRITE_NEXT(ROOMROM_SPRITE_SLOT_SWORD));
    VDP_setSpriteFull(ROOMROM_SPRITE_SLOT_BEAM,
                      (signed short)-32,
                      (signed short)-32,
                      RENDER_SPRITE_SIZE(1, 2),
                      RENDER_TILE_ATTR_FULL(RENDER_PAL1, 0, 0, 0, SWORD_VERT_VRAM_TILE),
                      ROOMROM_SPRITE_NEXT(ROOMROM_SPRITE_SLOT_BEAM));
    VDP_setSpriteFull(ROOMROM_SPRITE_SLOT_BOOMERANG,
                      (signed short)-32,
                      (signed short)-32,
                      RENDER_SPRITE_SIZE(1, 1),
                      RENDER_TILE_ATTR_FULL(RENDER_PAL1,0, 0, 0, BOOMERANG_VRAM_TILE),
                      ROOMROM_SPRITE_NEXT(ROOMROM_SPRITE_SLOT_BOOMERANG));
    VDP_setSpriteFull(ROOMROM_SPRITE_SLOT_ARROW,
                      (signed short)-32,
                      (signed short)-32,
                      RENDER_SPRITE_SIZE(1, 2),
                      RENDER_TILE_ATTR_FULL(RENDER_PAL1,0, 0, 0, ARROW_VERT_VRAM_TILE),
                      ROOMROM_SPRITE_NEXT(ROOMROM_SPRITE_SLOT_ARROW));
    VDP_setSpriteFull(ROOMROM_SPRITE_SLOT_BOMB,
                      (signed short)-32,
                      (signed short)-32,
                      RENDER_SPRITE_SIZE(1, 1),
                      RENDER_TILE_ATTR_FULL(RENDER_PAL1,0, 0, 0, BOMB_VRAM_TILE),
                      ROOMROM_SPRITE_NEXT(ROOMROM_SPRITE_SLOT_BOMB));
    VDP_setSpriteFull(ROOMROM_SPRITE_SLOT_EXPLOSION,
                      (signed short)-32,
                      (signed short)-32,
                      RENDER_SPRITE_SIZE(2, 2),
                      RENDER_TILE_ATTR_FULL(RENDER_PAL1,0, 0, 0, EXPLOSION_VRAM_TILE),
                      ROOMROM_SPRITE_NEXT(ROOMROM_SPRITE_SLOT_EXPLOSION));  /* link to slot 7 (room_item) — was 0 (terminator) */
    /* Slot 7 = room_item placeholder, slot 8 = candle_fire. Both must be in
     * the link chain or VDP skips them. Link 8 -> 0 terminates. */
    VDP_setSpriteFull(ROOMROM_SPRITE_SLOT_ROOM_ITEM,
                      (signed short)-32,
                      (signed short)-32,
                      RENDER_SPRITE_SIZE(1, 1),
                      RENDER_TILE_ATTR_FULL(RENDER_PAL1, 0, 0, 0, BOOMERANG_VRAM_TILE),
                      ROOMROM_SPRITE_NEXT(ROOMROM_SPRITE_SLOT_ROOM_ITEM));
    VDP_setSpriteFull(ROOMROM_SPRITE_SLOT_CANDLE_FIRE,
                      (signed short)-32,
                      (signed short)-32,
                      RENDER_SPRITE_SIZE(2, 2),
                      RENDER_TILE_ATTR_FULL(RENDER_PAL1, 1, 0, 0,
                          (unsigned short)(ROOMROM_ITEM_TILE_BASE_PAL(0)
                              + ROOMROM_ITEM_TILE_CANDLE_FIRE_F0)),
                      ROOMROM_SPRITE_NEXT(ROOMROM_SPRITE_SLOT_CANDLE_FIRE));
    /* Slot 9 = magic_shot (rod projectile). Always 16x16 — vertical is
     * mirrored 8x16 ($7A + hflip), horizontal is wide flippable ($7C-$7F). */
    VDP_setSpriteFull(ROOMROM_SPRITE_SLOT_MAGIC_SHOT,
                      (signed short)-32,
                      (signed short)-32,
                      RENDER_SPRITE_SIZE(2, 2),
                      RENDER_TILE_ATTR_FULL(RENDER_PAL1, 1, 0, 0,
                          (unsigned short)(ROOMROM_ITEM_TILE_BASE_PAL(0)
                              + ROOMROM_ITEM_TILE_MAGIC_SHOT_V)),
                      ROOMROM_SPRITE_NEXT(ROOMROM_SPRITE_SLOT_MAGIC_SHOT));
    /* Slot 10: HUD B-item LEFT. Slot 11: RIGHT hflipped. Both linked. */
    VDP_setSpriteFull(ROOMROM_SPRITE_SLOT_HUD_B_ITEM,
                      (signed short)-32,
                      (signed short)-32,
                      RENDER_SPRITE_SIZE(1, 2),
                      RENDER_TILE_ATTR_FULL(RENDER_PAL1, 1, 0, 0, 0),
                      ROOMROM_SPRITE_NEXT(ROOMROM_SPRITE_SLOT_HUD_B_ITEM));
    VDP_setSpriteFull(ROOMROM_SPRITE_SLOT_HUD_B_ITEM_R,
                      (signed short)-32,
                      (signed short)-32,
                      RENDER_SPRITE_SIZE(1, 2),
                      RENDER_TILE_ATTR_FULL(RENDER_PAL1, 1, 0, 1, 0),
                      ROOMROM_SPRITE_NEXT(ROOMROM_SPRITE_SLOT_HUD_B_ITEM_R));
    VDP_setSpriteFull(ROOMROM_SPRITE_SLOT_HUD_PLAYER,
                      (signed short)-32, (signed short)-32,
                      RENDER_SPRITE_SIZE(1, 2),
                      RENDER_TILE_ATTR_FULL(RENDER_PAL1, 1, 0, 0,
                          ROOMROM_HUD_MARKER_TILE_BASE),
                      ROOMROM_SPRITE_NEXT(ROOMROM_SPRITE_SLOT_HUD_PLAYER));
    VDP_setSpriteFull(ROOMROM_SPRITE_SLOT_HUD_COMPASS,
                      (signed short)-32, (signed short)-32,
                      RENDER_SPRITE_SIZE(1, 2),
                      RENDER_TILE_ATTR_FULL(RENDER_PAL0, 1, 0, 0,
                          ROOMROM_HUD_COMPASS_MARKER_TILE),
                      ROOMROM_SPRITE_NEXT(ROOMROM_SPRITE_SLOT_HUD_COMPASS));
    roomrom_sprites_set_link_pose(x, y, LINK_FACE_DOWN, 0u);
    render_update_sprites(ROOMROM_SPRITE_UPLOAD_COUNT_H32);
}

/* NES Z_01.asm:UpdatePositionMarker uses tile $3E (8x16), with sub-pal
 * 0 for Link and sub-pal 2/3 for the flashing compass target. The common
 * persistent sprite block already contains tile $3E/$3F; attr-3 uses the
 * pre-existing biased inventory pair at 1294/1295. */
void roomrom_sprites_set_hud_marker(unsigned char compass, short x, short y,
                                    unsigned char inactive_palette)
{
    unsigned short slot = compass ? ROOMROM_SPRITE_SLOT_HUD_COMPASS
                                  : ROOMROM_SPRITE_SLOT_HUD_PLAYER;
    unsigned short next = compass ? ROOMROM_SPRITE_SLOT_ENEMY_FIRST
                                  : ROOMROM_SPRITE_SLOT_HUD_COMPASS;
    unsigned short tile = compass && inactive_palette
        ? ROOMROM_HUD_COMPASS_MARKER_TILE
        : ROOMROM_HUD_MARKER_TILE_BASE;
    unsigned char pal = compass
        ? (inactive_palette ? RENDER_PAL0 : RENDER_PAL3)
        : RENDER_PAL1;
    VDP_setSpriteFull(slot, (signed short)x, (signed short)y,
                      RENDER_SPRITE_SIZE(1, 2),
                      RENDER_TILE_ATTR_FULL(pal, 1, 0, 0, tile), next);
}

void roomrom_sprites_hide_hud_marker(unsigned char compass)
{
    roomrom_sprites_set_hud_marker(compass, -32, -32, 1u);
}

void roomrom_sprites_set_link_pos(short x, short y)
{
    roomrom_sprites_set_link_pose(x, y, LINK_FACE_DOWN, 0u);
}

void roomrom_sprites_set_sword_vertical(short x, short y, unsigned char vflip,
                                        unsigned char sub_pal)
{
    unsigned short tile = (unsigned short)(ROOMROM_ITEM_TILE_BASE_PAL(sub_pal)
                                           + ROOMROM_ITEM_TILE_SWORD_VERT);
    VDP_setSpriteFull(ROOMROM_SPRITE_SLOT_SWORD,
                      (signed short)x,
                      (signed short)y,
                      RENDER_SPRITE_SIZE(1, 2),
                      RENDER_TILE_ATTR_FULL(ROOMROM_SUBPAL_PAL(sub_pal),
                                            0, vflip, 0, tile),
                      ROOMROM_SPRITE_NEXT(ROOMROM_SPRITE_SLOT_SWORD));
}

void roomrom_sprites_set_sword_horizontal(short x, short y, unsigned char hflip,
                                          unsigned char sub_pal)
{
    unsigned short tile = (unsigned short)(ROOMROM_ITEM_TILE_BASE_PAL(sub_pal)
                                           + ROOMROM_ITEM_TILE_SWORD_HORZ);
    VDP_setSpriteFull(ROOMROM_SPRITE_SLOT_SWORD,
                      (signed short)x,
                      (signed short)y,
                      RENDER_SPRITE_SIZE(2, 2),
                      RENDER_TILE_ATTR_FULL(ROOMROM_SUBPAL_PAL(sub_pal),
                                            0, 0, hflip, tile),
                      ROOMROM_SPRITE_NEXT(ROOMROM_SPRITE_SLOT_SWORD));
}

/* T-116: vanilla sword from the NES item path: sat_attr from
 * enemy_render_item_sat (tile + palette + flips + priority), wide = 16x16
 * horizontal frame ($82 pair), else one 8x16 narrow sprite. x/y are the
 * NES sprite coordinates. */
void roomrom_sprites_set_sword_nes(short x, short y, unsigned char wide,
                                   unsigned short sat_attr)
{
    VDP_setSpriteFull(ROOMROM_SPRITE_SLOT_SWORD,
                      (signed short)x,
                      (signed short)y,
                      wide ? RENDER_SPRITE_SIZE(2, 2) : RENDER_SPRITE_SIZE(1, 2),
                      sat_attr,
                      ROOMROM_SPRITE_NEXT(ROOMROM_SPRITE_SLOT_SWORD));
}

/* T-116: Link's arrow (slot $12) from the NES item path; see
 * roomrom_sprites_set_sword_nes. */
void roomrom_sprites_set_arrow_nes(short x, short y, unsigned char wide,
                                   unsigned short sat_attr)
{
    VDP_setSpriteFull(ROOMROM_SPRITE_SLOT_ARROW,
                      (signed short)x,
                      (signed short)y,
                      wide ? RENDER_SPRITE_SIZE(2, 2) : RENDER_SPRITE_SIZE(1, 2),
                      sat_attr,
                      ROOMROM_SPRITE_NEXT(ROOMROM_SPRITE_SLOT_ARROW));
}

/* T-116: Link's boomerang (slot $0F), one narrow 8x16 item sprite. */
void roomrom_sprites_set_boomerang_nes(short x, short y, unsigned short sat_attr)
{
    VDP_setSpriteFull(ROOMROM_SPRITE_SLOT_BOOMERANG,
                      (signed short)x,
                      (signed short)y,
                      RENDER_SPRITE_SIZE(1, 2),
                      sat_attr,
                      ROOMROM_SPRITE_NEXT(ROOMROM_SPRITE_SLOT_BOOMERANG));
}

void roomrom_sprites_clear_sword(void)
{
    VDP_setSpriteFull(ROOMROM_SPRITE_SLOT_SWORD,
                      (signed short)-32,
                      (signed short)-32,
                      RENDER_SPRITE_SIZE(1, 2),
                      RENDER_TILE_ATTR_FULL(RENDER_PAL1,0, 0, 0, SWORD_VERT_VRAM_TILE),
                      ROOMROM_SPRITE_NEXT(ROOMROM_SPRITE_SLOT_SWORD));
}

/* Redux ALttP-style diagonal sword. Per NES Anim_WriteItemSprites,
 * tiles in [$20,$62) take the @Narrow path which draws ONE 8x16 sprite
 * (not 16x16). Tile $48 + auto-paired $49 only. */
void roomrom_sprites_set_sword_diagonal(short x, short y,
                                        unsigned char hflip,
                                        unsigned char vflip,
                                        unsigned char sub_pal)
{
    unsigned short tile = (unsigned short)(ROOMROM_ITEM_TILE_BASE_PAL(sub_pal)
                                           + ROOMROM_ITEM_TILE_SWORD_DIAG);
    VDP_setSpriteFull(ROOMROM_SPRITE_SLOT_SWORD,
                      (signed short)x,
                      (signed short)y,
                      RENDER_SPRITE_SIZE(1, 2),
                      RENDER_TILE_ATTR_FULL(ROOMROM_SUBPAL_PAL(sub_pal),
                                            0, vflip, hflip, tile),
                      ROOMROM_SPRITE_NEXT(ROOMROM_SPRITE_SLOT_SWORD));
}

/* S7 v6 boomerang (slot 3). NES Z1 draws boomerang as a SINGLE 8x8
 * sprite via the @Narrow path in Anim_WriteSpecificItemSprites
 * (Z_01.asm:5279). Tile $36 is in [$20,$62), so $07 := 0 → only the
 * left half of the sprite pair is written. The "spin" comes from
 * cycling 3 frame tiles ($36/$38/$3A) and 4 flip combos ($00,$40,
 * $C0,$80) per BoomerangFrameCycle / BoomerangBaseSpriteAttrCycle
 * at Z_07.asm:3779.
 * Earlier code rendered RENDER_SPRITE_SIZE(2,2) which packed 4 sequential
 * blob tiles into a 16x16 quad, producing the "two boomerangs"
 * visual ($37 + $39 are not part of frame 0 — they belong to other
 * animation phases). 8x8 single-tile is the NES-faithful shape. */
static const unsigned char k_boomerang_frame_cycle[8] = {
    0u, 1u, 2u, 1u, 0u, 1u, 2u, 1u
};
static const unsigned char k_boomerang_attr_cycle[8] = {
    0x00u, 0x00u, 0x00u, 0x40u, 0x40u, 0xC0u, 0x80u, 0x80u
};

void roomrom_sprites_set_boomerang(short x, short y,
                                   unsigned char phase_idx,
                                   unsigned char sub_pal)
{
    unsigned char p = (unsigned char)(phase_idx & 0x7u);
    unsigned char frame_n = k_boomerang_frame_cycle[p];   /* 0, 1, or 2 */
    unsigned char attr    = k_boomerang_attr_cycle[p];
    unsigned char vflip   = (unsigned char)((attr & 0x80u) ? 1u : 0u);
    unsigned char hflip   = (unsigned char)((attr & 0x40u) ? 1u : 0u);
    unsigned short tile   = (unsigned short)(ROOMROM_ITEM_TILE_BASE_PAL(sub_pal)
                                             + ROOMROM_ITEM_TILE_BOOMERANG
                                             + (unsigned short)frame_n * 2u);
    /* NES Z1 sprites in gameplay run with PPUCTRL bit 5 = 1 (8x16 mode).
     * Live BizHawk capture (probe_nes_throw.lua, frame 280) confirms tile
     * $36+$37 form one 8x16 OAM entry: $36 = upper 8x8 (rows 4-7 of cell),
     * $37 = lower 8x8 (rows 0-3). Atlas idx 6/7 are adjacent in items_chr_x4
     * so RENDER_SPRITE_SIZE(1,2) consumes both via column-major fetch. */
    VDP_setSpriteFull(ROOMROM_SPRITE_SLOT_BOOMERANG,
                      (signed short)x,
                      (signed short)y,
                      RENDER_SPRITE_SIZE(1, 2),
                      RENDER_TILE_ATTR_FULL(ROOMROM_SUBPAL_PAL(sub_pal),
                                            0, vflip, hflip, tile),
                      ROOMROM_SPRITE_NEXT(ROOMROM_SPRITE_SLOT_BOOMERANG));
}

void roomrom_sprites_clear_boomerang(void)
{
    VDP_setSpriteFull(ROOMROM_SPRITE_SLOT_BOOMERANG,
                      (signed short)-32,
                      (signed short)-32,
                      RENDER_SPRITE_SIZE(1, 2),
                      RENDER_TILE_ATTR_FULL(RENDER_PAL1,0, 0, 0, BOOMERANG_VRAM_TILE),
                      ROOMROM_SPRITE_NEXT(ROOMROM_SPRITE_SLOT_BOOMERANG));
}

/* LEGACY, UNCALLED (T-157): no production caller uses this room-item
 * switch. Its item-ID cases predate the active NES ItemIdToSlot drain and
 * are not authoritative (for example $0C is raft, not ring). Active item
 * objects use item_object_update -> draw_animate_item_object ->
 * enemy_render's ITEM_ATTR_MARKER atlas translation. Keep this only as
 * historical Phase K code until its fixed SAT slot is retired.
 *
 * Task 5.9.1 room-item sprite. Slot 7. Slice-1 placeholder uses the
 * boomerang tile from items_chr_x4 atlas (CHR for triforce/etc. not
 * yet extracted; see project_chr_extraction_items_blocker). */
/* Phase K (2026-05-18): item_id -> tile dispatch. Triforce uses
 * extracted CHR (TRIFORCE_PIECE, 2x2 wide, sub-pal 2); other UW item
 * kinds (compass 0x10, map 0x11, heart container, big key, ring, etc)
 * fall back to boomerang glyph placeholder until their CHR is
 * extracted + added to item_chr_manifest.json. */
void roomrom_sprites_set_room_item(short x, short y,
                                   unsigned char item_id,
                                   unsigned char sub_pal)
{
    /* NES source: Z_01.asm:ItemIdToSlot, Anim_ItemFrameOffsets/Tiles,
     *             Anim_WriteSpecificItemSprites; Z_03.asm:FetchPatternBlockUWBoss.
     * Drained C: src/oracle/items/item_runtime.c:item_take_item;
     *            native room-item sprite dispatch here.
     * Coverage: PARTIAL (L9 Power Triforce presentation; connected pickup pending).
     * Stance: EXTEND the ROM-derived boss bank already resident in L9. */
    /* Item ID $0E maps to slot $1B (Power Triforce), whose NES frame tile
     * is $F2. The L9 boss bank holds $C0..$FF; $F2/$F3 and $F4/$F5
     * form the two 8x16 OAM columns in NES sprite palette 2. */
    if (item_id == 0x0Eu) {
        unsigned short tile = (unsigned short)(ROOMROM_BOSS_TILE_BASE + 0x32u);
        VDP_setSpriteFull(ROOMROM_SPRITE_SLOT_ROOM_ITEM,
                          (signed short)x,
                          (signed short)y,
                          RENDER_SPRITE_SIZE(2, 2),
                          RENDER_TILE_ATTR_FULL(ROOMROM_SUBPAL_PAL(2u),
                                                1, 0, 0, tile),
                          ROOMROM_SPRITE_NEXT(ROOMROM_SPRITE_SLOT_ROOM_ITEM));
        return;
    }
    /* UW_ITEM_ID_TRIFORCE = 0x1B per engine/data/uw_item_rooms.h.
     * Triforce piece renders as 2x2 (wide_16x16_pair: 4 tiles
     * LT/LB/RT/RB in column-major), sub-pal 2 (gold/yellow). */
    if (item_id == 0x1Bu) {
        unsigned short tile = (unsigned short)(ROOMROM_ITEM_TILE_BASE_PAL(2u)
                                                + ROOMROM_ITEM_TILE_TRIFORCE_PIECE);
        VDP_setSpriteFull(ROOMROM_SPRITE_SLOT_ROOM_ITEM,
                          (signed short)x,
                          (signed short)y,
                          RENDER_SPRITE_SIZE(2, 2),
                          RENDER_TILE_ATTR_FULL(ROOMROM_SUBPAL_PAL(2u),
                                                1, 0, 0, tile),
                          ROOMROM_SPRITE_NEXT(ROOMROM_SPRITE_SLOT_ROOM_ITEM));
        return;
    }
    /* Phase K continuation (2026-05-18): compass + map extracted CHR.
     * NES item_id 0x10 (UW_ITEM_ID_COMPASS) -> ROOMROM_ITEM_TILE_COMPASS
     *               (1x2 narrow_8x16, sub-pal 0).
     * NES item_id 0x11 (UW_ITEM_ID_MAP)     -> ROOMROM_ITEM_TILE_MAP. */
    if (item_id == 0x10u) {
        unsigned short tile = (unsigned short)(ROOMROM_ITEM_TILE_BASE_PAL(0u)
                                                + ROOMROM_ITEM_TILE_COMPASS);
        VDP_setSpriteFull(ROOMROM_SPRITE_SLOT_ROOM_ITEM,
                          (signed short)x,
                          (signed short)y,
                          RENDER_SPRITE_SIZE(1, 2),
                          RENDER_TILE_ATTR_FULL(ROOMROM_SUBPAL_PAL(0u),
                                                1, 0, 0, tile),
                          ROOMROM_SPRITE_NEXT(ROOMROM_SPRITE_SLOT_ROOM_ITEM));
        return;
    }
    if (item_id == 0x11u) {
        unsigned short tile = (unsigned short)(ROOMROM_ITEM_TILE_BASE_PAL(0u)
                                                + ROOMROM_ITEM_TILE_MAP);
        VDP_setSpriteFull(ROOMROM_SPRITE_SLOT_ROOM_ITEM,
                          (signed short)x,
                          (signed short)y,
                          RENDER_SPRITE_SIZE(1, 2),
                          RENDER_TILE_ATTR_FULL(ROOMROM_SUBPAL_PAL(0u),
                                                1, 0, 0, tile),
                          ROOMROM_SPRITE_NEXT(ROOMROM_SPRITE_SLOT_ROOM_ITEM));
        return;
    }
    /* Phase K continuation #2 (2026-05-18): heart container / big key /
     * book / raft / ladder. NES item slot IDs per Anim_ItemFrameTiles
     * (Z_01.asm:5201-5207). All narrow_8x16 dispatch with sub-pal 0. */
    {
        unsigned short tile_offset = 0xFFFFu;  /* sentinel = use placeholder */
        switch (item_id) {
        /* RoomItemId is a pickup ID, not an inventory/animation slot.
         * NES Z_01.asm:ItemIdToSlot maps $1A to heart slot $18. */
        case 0x1Au: tile_offset = ROOMROM_ITEM_TILE_HEART_CONTAINER; break;
        case 0x18u: tile_offset = ROOMROM_ITEM_TILE_BIG_KEY;         break;
        case 0x0Bu: tile_offset = ROOMROM_ITEM_TILE_BOOK_OF_MAGIC;   break;
        case 0x0Au: tile_offset = ROOMROM_ITEM_TILE_RAFT;            break;
        case 0x0Du: tile_offset = ROOMROM_ITEM_TILE_LADDER;          break;
        /* Phase K continuation #3 (2026-05-18): full UW pickup set. */
        case 0x0Cu: tile_offset = ROOMROM_ITEM_TILE_RING;            break;
        case 0x1Cu: tile_offset = ROOMROM_ITEM_TILE_MAGIC_KEY;       break;
        case 0x0Fu: tile_offset = ROOMROM_ITEM_TILE_BRACELET;        break;
        case 0x04u: tile_offset = ROOMROM_ITEM_TILE_BOW;             break;
        case 0x06u: tile_offset = ROOMROM_ITEM_TILE_RECORDER;        break;
        case 0x07u: tile_offset = ROOMROM_ITEM_TILE_FOOD;            break;
        case 0x08u: tile_offset = ROOMROM_ITEM_TILE_POTION;          break;
        default:    tile_offset = 0xFFFFu;                            break;
        }
        if (tile_offset != 0xFFFFu) {
            unsigned char pal = (item_id == 0x1Au) ? 2u : 0u;
            unsigned short tile = (unsigned short)(ROOMROM_ITEM_TILE_BASE_PAL(pal)
                                                    + tile_offset);
            VDP_setSpriteFull(ROOMROM_SPRITE_SLOT_ROOM_ITEM,
                              (signed short)x,
                              (signed short)y,
                              RENDER_SPRITE_SIZE((item_id == 0x1Au) ? 2 : 1, 2),
                              RENDER_TILE_ATTR_FULL(ROOMROM_SUBPAL_PAL(pal),
                                                    1, 0, 0, tile),
                              ROOMROM_SPRITE_NEXT(ROOMROM_SPRITE_SLOT_ROOM_ITEM));
            return;
        }
    }
    /* Placeholder fallback (ring / other unmapped item_ids). */
    {
        unsigned short tile = (unsigned short)(ROOMROM_ITEM_TILE_BASE_PAL(sub_pal)
                                                + ROOMROM_ITEM_TILE_BOOMERANG);
        VDP_setSpriteFull(ROOMROM_SPRITE_SLOT_ROOM_ITEM,
                          (signed short)x,
                          (signed short)y,
                          RENDER_SPRITE_SIZE(1, 1),
                          RENDER_TILE_ATTR_FULL(ROOMROM_SUBPAL_PAL(sub_pal),
                                                1, 0, 0, tile),
                          ROOMROM_SPRITE_NEXT(ROOMROM_SPRITE_SLOT_ROOM_ITEM));
    }
}

void roomrom_sprites_clear_room_item(void)
{
    VDP_setSpriteFull(ROOMROM_SPRITE_SLOT_ROOM_ITEM,
                      (signed short)-32,
                      (signed short)-32,
                      RENDER_SPRITE_SIZE(1, 1),
                      RENDER_TILE_ATTR_FULL(RENDER_PAL1, 0, 0, 0, BOOMERANG_VRAM_TILE),
                      ROOMROM_SPRITE_NEXT(ROOMROM_SPRITE_SLOT_ROOM_ITEM));
}

void roomrom_sprites_set_candle_fire(short x, short y,
                                     unsigned char hflip,
                                     unsigned char sub_pal,
                                     unsigned char frame_index)
{
    /* Phase P (2026-05-18): frame_index 0..3 -> F0/F1/F2/F3 manifest
     * entries. Caller advances per-instance timer (candle_fire.c
     * s_anim_frame). Pre-Phase-P hardcoded F0 (no animation). */
    unsigned short frame_offset;
    switch (frame_index & 0x03u) {
    case 1u:  frame_offset = ROOMROM_ITEM_TILE_CANDLE_FIRE_F1; break;
    case 2u:  frame_offset = ROOMROM_ITEM_TILE_CANDLE_FIRE_F2; break;
    case 3u:  frame_offset = ROOMROM_ITEM_TILE_CANDLE_FIRE_F3; break;
    default:  frame_offset = ROOMROM_ITEM_TILE_CANDLE_FIRE_F0; break;
    }
    unsigned short tile = (unsigned short)(ROOMROM_ITEM_TILE_BASE_PAL(sub_pal)
                                            + frame_offset);
    VDP_setSpriteFull(ROOMROM_SPRITE_SLOT_CANDLE_FIRE,
                      (signed short)x, (signed short)y,
                      RENDER_SPRITE_SIZE(2, 2),
                      RENDER_TILE_ATTR_FULL(ROOMROM_SUBPAL_PAL(sub_pal),
                                            1, 0, hflip, tile),
                      ROOMROM_SPRITE_NEXT(ROOMROM_SPRITE_SLOT_CANDLE_FIRE));
}

void roomrom_sprites_clear_candle_fire(void)
{
    unsigned short tile = (unsigned short)(ROOMROM_ITEM_TILE_BASE_PAL(0)
                                            + ROOMROM_ITEM_TILE_CANDLE_FIRE_F0);
    VDP_setSpriteFull(ROOMROM_SPRITE_SLOT_CANDLE_FIRE,
                      (signed short)-32, (signed short)-32,
                      RENDER_SPRITE_SIZE(2, 2),
                      RENDER_TILE_ATTR_FULL(RENDER_PAL1, 1, 0, 0, tile),
                      ROOMROM_SPRITE_NEXT(ROOMROM_SPRITE_SLOT_CANDLE_FIRE));
}

/* S7 v7 arrow (slot 4). Vertical 8x16 for UP/DOWN, horizontal 16x16
 * for LEFT/RIGHT (hflip on LEFT).
 * sub_pal selects which 4-copy bank to read (NES base attr = 0). */
void roomrom_sprites_set_arrow(short x, short y, link_face_t face,
                               unsigned char sub_pal)
{
    unsigned short tile_vert = (unsigned short)(ROOMROM_ITEM_TILE_BASE_PAL(sub_pal)
                                                + ROOMROM_ITEM_TILE_ARROW_VERT);
    unsigned short tile_horz = (unsigned short)(ROOMROM_ITEM_TILE_BASE_PAL(sub_pal)
                                                + ROOMROM_ITEM_TILE_ARROW_HORZ);
    switch (face) {
    case LINK_FACE_UP:
        /* 5.8.1 diag: priority bit set so projectile renders ABOVE
         * BG_A door art (uses BG priority 0x8000). Codex H3
         * confirmed. Applies to all weapon projectile sprites. */
        VDP_setSpriteFull(ROOMROM_SPRITE_SLOT_ARROW, (signed short)x, (signed short)y, RENDER_SPRITE_SIZE(1, 2),
                          RENDER_TILE_ATTR_FULL(ROOMROM_SUBPAL_PAL(sub_pal),
                                                1, 0, 0, tile_vert),
                          ROOMROM_SPRITE_NEXT(ROOMROM_SPRITE_SLOT_ARROW));
        break;
    case LINK_FACE_DOWN:
        VDP_setSpriteFull(ROOMROM_SPRITE_SLOT_ARROW, (signed short)x, (signed short)y, RENDER_SPRITE_SIZE(1, 2),
                          RENDER_TILE_ATTR_FULL(ROOMROM_SUBPAL_PAL(sub_pal),
                                                1, 1, 0, tile_vert),
                          ROOMROM_SPRITE_NEXT(ROOMROM_SPRITE_SLOT_ARROW));
        break;
    case LINK_FACE_LEFT:
        /* Phase 1: horizontal arrow ($86..$89) now sourced from live NES
         * item atlas. RIGHT renders as-is, LEFT mirrors via hflip. */
        VDP_setSpriteFull(ROOMROM_SPRITE_SLOT_ARROW, (signed short)x, (signed short)y, RENDER_SPRITE_SIZE(2, 2),
                          RENDER_TILE_ATTR_FULL(ROOMROM_SUBPAL_PAL(sub_pal),
                                                1, 0, 1, tile_horz),
                          ROOMROM_SPRITE_NEXT(ROOMROM_SPRITE_SLOT_ARROW));
        break;
    case LINK_FACE_RIGHT:
        VDP_setSpriteFull(ROOMROM_SPRITE_SLOT_ARROW, (signed short)x, (signed short)y, RENDER_SPRITE_SIZE(2, 2),
                          RENDER_TILE_ATTR_FULL(ROOMROM_SUBPAL_PAL(sub_pal),
                                                1, 0, 0, tile_horz),
                          ROOMROM_SPRITE_NEXT(ROOMROM_SPRITE_SLOT_ARROW));
        break;
    default:
        roomrom_sprites_clear_arrow();
        return;
    }
}

void roomrom_sprites_clear_arrow(void)
{
    VDP_setSpriteFull(ROOMROM_SPRITE_SLOT_ARROW,
                      (signed short)-32,
                      (signed short)-32,
                      RENDER_SPRITE_SIZE(1, 2),
                      RENDER_TILE_ATTR_FULL(RENDER_PAL1,0, 0, 0, ARROW_VERT_VRAM_TILE),
                      ROOMROM_SPRITE_NEXT(ROOMROM_SPRITE_SLOT_ARROW));
}

/* S7 v8 bomb (slot 5). NES Z1 DrawBomb (Z_07.asm:4869) -> DrawCloud ->
 * Anim_WriteItemSprites with item slot $01, frame 0 -> ItemFrameTiles[$03]
 * = $34. Tile $34 in [$20, $62) -> @Narrow path. NES PPU runs 8x16 sprite
 * mode in gameplay; OAM "tile $34" pairs $34 (top: fuse + bomb-cap) + $35
 * (bottom: bomb body). Live BizHawk capture confirms both halves contain
 * content (probe_nes_throw.lua frame 280). Atlas idx 20/21 are adjacent
 * in items_chr_x4 post-manifest-update; RENDER_SPRITE_SIZE(1,2) consumes both via
 * column-major fetch. sub_pal selects 4-copy bank (NES DrawCloud sets Y=1). */
void roomrom_sprites_set_bomb(short x, short y, unsigned char sub_pal)
{
    unsigned short tile = (unsigned short)(ROOMROM_ITEM_TILE_BASE_PAL(sub_pal)
                                           + ROOMROM_ITEM_TILE_BOMB);
    VDP_setSpriteFull(ROOMROM_SPRITE_SLOT_BOMB,
                      (signed short)x,
                      (signed short)y,
                      RENDER_SPRITE_SIZE(1, 2),
                      RENDER_TILE_ATTR_FULL(ROOMROM_SUBPAL_PAL(sub_pal),
                                            0, 0, 0, tile),
                      ROOMROM_SPRITE_NEXT(ROOMROM_SPRITE_SLOT_BOMB));
}

void roomrom_sprites_clear_bomb(void)
{
    VDP_setSpriteFull(ROOMROM_SPRITE_SLOT_BOMB,
                      (signed short)-32,
                      (signed short)-32,
                      RENDER_SPRITE_SIZE(1, 2),
                      RENDER_TILE_ATTR_FULL(RENDER_PAL1,0, 0, 0, BOMB_VRAM_TILE),
                      ROOMROM_SPRITE_NEXT(ROOMROM_SPRITE_SLOT_BOMB));
}

/* S7 v8 explosion (slot 6). NES Z1 cloud cluster uses item slot $01
 * frames 1-3 = ItemFrameTiles[$04..$06] = $70/$72/$74. Each tile in
 * [$6C, $7C) -> @Wide -> Anim_WriteMirroredSpritePair (Z_01.asm:5304):
 * 2 OAMs side-by-side, right = left hflipped. PPU 8x16 sprite mode
 * (PPUCTRL bit 5 = 1) makes each OAM 8x16 (paired tile $7X with $7X+1
 * as bottom). Visible cluster = 2*8x16 = 16x16 per frame.
 *
 * Live BizHawk capture (probe_nes_explode.lua frame 335) confirms 4
 * cluster positions × 2 OAMs each (hfl=0 + hfl=1). Atlas blob layout
 * post-regen (wide_16x16_mirrored_8x16_phase_cycle draw_rule):
 *
 *   blob[EXPLOSION + 0..3] = $70/$71/$70-hflip/$71-hflip  (frame 0)
 *   blob[EXPLOSION + 4..7] = $72/$73/$72-hflip/$73-hflip  (frame 1)
 *   blob[EXPLOSION + 8..11]= $74/$75/$74-hflip/$75-hflip  (frame 2)
 *
 * SGDK column-major RENDER_SPRITE_SIZE(2,2) fetch order is LT, LB, RT, RB —
 * matches the blob exactly. Cycle phase advances every
 * EXPLOSION_PHASE_FRAMES ticks of the bomb explode timer.
 *
 * Genesis impl draws ONE 16x16 cluster centered on the bomb origin
 * (NES draws 4 clusters at BombCloud offsets, Z_07.asm:4924-4974;
 * 4-cluster parity is a separate enhancement). */
#define EXPLOSION_PHASE_FRAMES 6u

void roomrom_sprites_set_explosion(short x, short y, unsigned char timer,
                                   unsigned char sub_pal)
{
    /* timer counts DOWN from BOMB_EXPLODE_FRAMES (24). Map to phase 0..2
     * advancing every 6 elapsed frames so each NES frame holds for ~6
     * Genesis ticks (4 phases over 24 frames, clamped to 0..2).
     * sub_pal selects which 4-copy bank to read (NES DrawCloud sets Y=1). */
    unsigned char elapsed = (unsigned char)(24u - (unsigned char)(timer & 0x1Fu));
    unsigned char phase   = (unsigned char)((elapsed / EXPLOSION_PHASE_FRAMES) % 3u);
    unsigned short tile   = (unsigned short)(ROOMROM_ITEM_TILE_BASE_PAL(sub_pal)
                                             + ROOMROM_ITEM_TILE_EXPLOSION
                                             + (unsigned short)phase * 4u);
    VDP_setSpriteFull(ROOMROM_SPRITE_SLOT_EXPLOSION,
                      (signed short)x,
                      (signed short)y,
                      RENDER_SPRITE_SIZE(2, 2),
                      RENDER_TILE_ATTR_FULL(ROOMROM_SUBPAL_PAL(sub_pal),
                                            0, 0, 0, tile),
                      ROOMROM_SPRITE_NEXT(ROOMROM_SPRITE_SLOT_EXPLOSION));
}

void roomrom_sprites_clear_explosion(void)
{
    VDP_setSpriteFull(ROOMROM_SPRITE_SLOT_EXPLOSION,
                      (signed short)-32,
                      (signed short)-32,
                      RENDER_SPRITE_SIZE(2, 2),
                      RENDER_TILE_ATTR_FULL(RENDER_PAL1,0, 0, 0, EXPLOSION_VRAM_TILE),
                      ROOMROM_SPRITE_NEXT(ROOMROM_SPRITE_SLOT_EXPLOSION));  /* link to slot 7 — keeps slots 7/8 in chain */
}


/* NES source: Z_05 InitMode6/ResetInvObjState; Z_07 DrawSpritesBetweenRooms.
 * Drained C: ow_scroll_tick and room_dispatch room_hide_all_sprites.
 * Coverage: PARTIAL (native fixed SAT and weapon-cache publication).
 * Stance: EXTEND the existing native sprite owner, no gameplay state. */
void roomrom_sprites_hide_transition_items(void)
{
    roomrom_sprites_clear_sword();
    roomrom_sprites_clear_boomerang();
    roomrom_sprites_clear_arrow();
    roomrom_sprites_clear_bomb();
    roomrom_sprites_clear_explosion();
    roomrom_sprites_clear_candle_fire();
    roomrom_sprites_clear_room_item();
    enemy_render_weapon_reset(0x0Eu);
    enemy_render_weapon_reset(0x10u);
    enemy_render_weapon_reset(0x11u);
    enemy_render_weapon_reset(0x13u);
}

/* Phase P (2026-05-18) fairy spark renderer.
 * NES Z_04.asm:11508 DrawFairy — 2-frame flicker (F0/F1) every 4
 * vblanks. Fairy sprite is sub-pal 1 (NES forced attribute).
 * Caller picks SAT slot from enemy bridge range; frame from per-fairy
 * state machine OR global FrameCounter & 0x04. */
void roomrom_sprites_set_fairy_spark(unsigned char slot,
                                     short x, short y,
                                     unsigned char frame_index,
                                     unsigned char link_to)
{
    unsigned short tile_offset = (frame_index & 1u)
        ? (unsigned short)ROOMROM_ITEM_TILE_FAIRY_SPARK_F1
        : (unsigned short)ROOMROM_ITEM_TILE_FAIRY_SPARK_F0;
    unsigned short tile = (unsigned short)(ROOMROM_ITEM_TILE_BASE_PAL(1u)
                                            + tile_offset);
    VDP_setSpriteFull(slot,
                      (signed short)x, (signed short)y,
                      RENDER_SPRITE_SIZE(1, 2),
                      RENDER_TILE_ATTR_FULL(ROOMROM_SUBPAL_PAL(1u),
                                            1, 0, 0, tile),
                      link_to);
}
