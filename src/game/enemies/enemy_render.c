/* enemy_render.c — NES OAM mirror -> Genesis SAT bridge.
 *
 * NES source:
 *   Z_01.asm:5365 Anim_WriteSprite (single-OAM write primitive)
 *   Z_01.asm:4958 SpriteOffsets (41-entry sprite slot table)
 *   Z_01.asm:3088 CycleCurSpriteIndex (RollingSpriteIndex advance)
 *
 * NES source: reference/aldonunez/Z_01.asm:Anim_WriteSprite,
 *             Anim_WriteSpecificItemSprites, ItemIdToSlot/Anim_ItemFrameTiles.
 * Drained C:  NONE (no enemy_render_runtime.c candidate).
 * Coverage:   PARTIAL (item/enemy routing and spark art; unresolved item rows remain).
 * Stance:     EXTEND.
 *
 * Phase 7 substrate fix 2026-05-15. Replaces c_anim_write_sprite no-op
 * stub at enemy_lamnola_bridge.c:33. Enemy logic ticks correctly now
 * (LBA install + scroll re-init committed earlier this session) but no
 * enemy sprites pixel-rendered without this bridge.
 */

#include "enemy_render.h"
#include "platform_abi.h"
#include "render_abi.h"
#include "world/render/sprite_slots.h"
#include "world/render/sprite_render.h"  /* roomrom_sprites_item_chr_variant */
#include "world/render/subpal_routing.h"  /* Phase AA centralized sub-pal -> OAM pal API */
#include "enemy_loop.h"   /* ENEMY_LOOP_SLOT_FIRST/LAST */
#include "enemy_state.h"  /* ENEMY_X, ENEMY_Y, ENEMY_ALIVE_FLAG, ENEMY_THROWER_SLOT */
#include "../../../RoomRom/src/roomrom_vram_map.h"  /* canonical ROOMROM_SPR_TILE_BASE */
#include "../../../RoomRom/src/atlas/level_chr_swap.h"  /* sub-pal 3 cache flush key */

/* NES RAM cells — see reference/aldonunez/Variables.inc. */
#define NES_SPRITES_BASE        0x0200u   /* OAM mirror, 64 sprites x 4 bytes */
#define NES_ROLLING_SPR_INDEX   0x0341u
/* NES_OBJ_INV_TIMER_BASE ($04F0) comes from platform_abi.h. */
#define NES_FRAME_COUNTER       0x0015u
#define NES_SCRATCH_03          0x0003u   /* sprite attrs byte */

/* NES Z_01.asm:5365 — Anim_WriteSprite SpriteOffsets table.
 * 41 entries; index via RollingSpriteIndex (0..$27). */
static const unsigned char k_sprite_offsets[41] = {
    0x60u, 0xBCu, 0x64u, 0xB8u, 0x68u, 0xB4u, 0x6Cu, 0xB0u,
    0x70u, 0xCCu, 0x74u, 0xC8u, 0x78u, 0xC4u, 0x7Cu, 0xC0u,
    0x80u, 0xDCu, 0x84u, 0xD8u, 0x88u, 0xD4u, 0x8Cu, 0xD0u,
    0x90u, 0xECu, 0x94u, 0xE8u, 0x98u, 0xE4u, 0x9Cu, 0xE0u,
    0xA0u, 0xFCu, 0xA4u, 0xF8u, 0xA8u, 0xF4u, 0xACu, 0xF0u,
    0x60u
};

/* NES_OBJ_X + slot, NES_OBJ_Y + slot per platform_abi.h. */
#define ENEMY_RENDER_OBJ_X(slot)  OBJ(NES_OBJ_X, (slot))
#define ENEMY_RENDER_OBJ_Y(slot)  OBJ(NES_OBJ_Y, (slot))
#define ENEMY_RENDER_INV_TIMER(slot) OBJ(NES_OBJ_INV_TIMER_BASE, (slot))

/* NES Z_01.asm:3088 — CycleCurSpriteIndex: advance + wrap at $28. */
static inline void cycle_cur_sprite_index(void)
{
    unsigned char r = (unsigned char)(RAM(NES_ROLLING_SPR_INDEX) + 1u);
    if (r >= 0x28u) r = 0u;
    RAM(NES_ROLLING_SPR_INDEX) = r;
}

/* Phase E 2026-05-15: multi-latch cache. Each ENEMY_LOOP slot can
 * accumulate up to N tiles per frame (multi-tile bosses like Aquamentus
 * write 3-6 tiles, walkers write 1-2). Native sweep emits one Genesis
 * SAT entry per latched tile @ SIZE(1,2) 8x16 (1:1 NES OAM mapping).
 * Worst case: 11 slots * 4 entries = 44 SAT writes/frame, well under
 * H32's 64-slot hardware budget. Per-tile h_flip preserved (each entry
 * stores its own attrs byte) — fixes spec gap #5. */
/* T-050: 20 — the pond fairy draws itself plus eight orbiting hearts
 * through its own slot (PondFairy_MoveHearts uses CurObjIndex). */
#define ENEMY_RENDER_MAX_PER_SLOT  20u

typedef struct {
    unsigned char tile;
    unsigned char attrs;
    unsigned char x;
    unsigned char y;
} enemy_render_entry_t;

static enemy_render_entry_t
    s_enemy_entries[ENEMY_LOOP_SLOT_LAST + 1u][ENEMY_RENDER_MAX_PER_SLOT];
static unsigned char s_enemy_count[ENEMY_LOOP_SLOT_LAST + 1u];
/* Entry written by Anim_WriteSprite (single sprite), which flashes with
 * FrameCounter bits; the pair writer (Anim_WriteSpritePair) already put
 * ObjInvincibilityTimer bits in the attrs (NES Z_01.asm:5151, 5365).
 * Attr bit 2 is unused by the NES OAM. */
#define ANIM_WRITE_SPRITE_MARKER 0x04u

void enemy_render_native_reset(void)
{
    unsigned char i;
    for (i = 0u; i <= ENEMY_LOOP_SLOT_LAST; ++i) {
        s_enemy_count[i] = 0u;
    }
}

void anim_write_sprite_drained(unsigned int tile, unsigned int slot)
{
    /* Phase C 2026-05-15: drop latch-time invincibility flash. The
     * native sweep applies hit-flash LIVE per frame so the palette
     * cycles every VBlank regardless of when the enemy last drew.
     * NES Z_01.asm:5367-5371 stays for compat — the OAM byte we
     * write below still reflects the latched state if any external
     * consumer reads it. */
    unsigned char attrs = RAM(NES_SCRATCH_03);

    /* Phase E 2026-05-15 — multi-latch append: each anim_write call
     * adds one entry to the slot's cache (capped at MAX_PER_SLOT).
     * Used by oracle drained enemies (moldorm, lamnola, ganon) which
     * call this directly via c_anim_write_sprite. Per-tile h_flip
     * preserved because each entry stores its own attrs byte. */
    {
        unsigned char cur_slot = ENEMY_THROWER_SLOT;
        if (cur_slot <= ENEMY_LOOP_SLOT_LAST) {
            unsigned char n = s_enemy_count[cur_slot];
            if (n < ENEMY_RENDER_MAX_PER_SLOT) {
                enemy_render_entry_t *e = &s_enemy_entries[cur_slot][n];
                e->tile  = (unsigned char)tile;
                e->attrs = (unsigned char)(attrs | ANIM_WRITE_SPRITE_MARKER);
                e->x     = ENEMY_RENDER_OBJ_X(slot);
                e->y     = ENEMY_RENDER_OBJ_Y(slot);
                s_enemy_count[cur_slot] = (unsigned char)(n + 1u);
            }
        }
    }

    /* 2026-05-15 perf: NES OAM writes are no longer needed — the native
     * sweep reads the side-channel cache (s_enemy_* arrays) populated
     * above. Skip 4 OAM byte writes per call (~50 calls/frame =
     * ~1600 cycles/frame saved). RollingSpriteIndex still advances so
     * any external SpriteOffsets-table consumer sees the same cadence. */
    cycle_cur_sprite_index();
}

/* Bridge entry replacing the c_anim_write_sprite stub. */
void c_anim_write_sprite(unsigned int tile, unsigned int slot)
{
    anim_write_sprite_drained(tile, slot);
}

/* Phase A/E cache feeder. Called from native draw_dispatch.c
 * anim_write_sprite_pair_not_flashing on EACH iteration (LEFT + RIGHT
 * halves) so natively-dispatched enemies populate s_enemy_entries the
 * same way the drain-shim path (anim_write_sprite_drained via
 * c_anim_write_sprite) does for oracle enemies. ENEMY_THROWER_SLOT
 * mirrors NES CurObjIndex which enemy_loop_tick sets before dispatching
 * the per-slot update fn — same key both paths.
 *
 * Phase E 2026-05-15: append entry to multi-latch cache (cap 4 per
 * slot). Per-tile h_flip preserved: each entry stores its own attrs.
 * Function name kept as _pair_left for ABI stability — it now publishes
 * BOTH halves via separate calls. */
static void weapon_add(unsigned char slot, unsigned char tile,
                       unsigned char attrs, unsigned char x, unsigned char y);

__attribute__((always_inline)) void enemy_render_publish_pair_left(unsigned char tile,
                                    unsigned char attrs,
                                    unsigned char x,
                                    unsigned char y)
{
    unsigned char cur_slot = ENEMY_THROWER_SLOT;
    if (cur_slot <= ENEMY_LOOP_SLOT_LAST) {
        unsigned char n = s_enemy_count[cur_slot];
        if (n < ENEMY_RENDER_MAX_PER_SLOT) {
            enemy_render_entry_t *e = &s_enemy_entries[cur_slot][n];
            e->tile  = tile;
            e->attrs = attrs;
            e->x     = x;
            e->y     = y;
            s_enemy_count[cur_slot] = (unsigned char)(n + 1u);
        }
    } else {
        /* T-130: item objects above the monster slots (the room item $13)
         * draw through the same NES writers into the weapon cache. */
        weapon_add(cur_slot, tile, attrs, x, y);
    }
}

/* Z_04.asm Wallmaster @PatchSprites on the native cache. The NES patches
 * the two OAM records the Wallmaster just wrote; the Genesis draw goes to
 * this cache instead (enemy OAM writes are skipped), so the patch never
 * reached the screen (T-171 t013_route t7611: the hand inside the wall
 * drawn in front of it, and the closed hand's left half the Keese tile).
 * Wallmaster_PutSpriteBehindBgIfNeeded: a sprite with X + 8 or X + 0
 * >= $E9 or < $18 gets attribute $20. Frame 1: tile $9C on the left
 * sprite becomes $AC, else the right sprite's tile becomes $AC. */
void enemy_render_wallmaster_patch(unsigned char slot, unsigned char closed_hand)
{
    unsigned char n, k;
    enemy_render_entry_t *pair;
    if (slot > ENEMY_LOOP_SLOT_LAST) return;
    n = s_enemy_count[slot];
    if (n < 2u) return;
    pair = &s_enemy_entries[slot][n - 2u];
    for (k = 0u; k < 2u; ++k) {
        const unsigned char x = pair[k].x;
        const unsigned char a = (unsigned char)(x + 8u), b = x;
        if (a >= 0xE9u || a < 0x18u || b >= 0xE9u || b < 0x18u)
            pair[k].attrs = (unsigned char)(pair[k].attrs | 0x20u);
    }
    if (closed_hand) {
        if (pair[0].tile == 0x9Cu) pair[0].tile = 0xACu;
        else pair[1].tile = 0xACu;
    }
}

/* NES source: Z_01.asm Anim_WriteSpritePairNotFlashing; drained C:
 * draw_dispatch.c; coverage: ordinary two-sided native enemies; stance:
 * EXTEND. Submit both halves with one slot/count lookup. */
void enemy_render_publish_native_pair(unsigned char left_tile,
                                      unsigned char left_attrs,
                                      unsigned char right_tile,
                                      unsigned char right_attrs,
                                      unsigned char left_x,
                                      unsigned char right_x,
                                      unsigned char y)
{
    unsigned char slot = ENEMY_THROWER_SLOT;
    unsigned char n;
    enemy_render_entry_t *e;
    if (slot > ENEMY_LOOP_SLOT_LAST) {
        enemy_render_publish_pair_left(left_tile, left_attrs, left_x, y);
        enemy_render_publish_pair_left(right_tile, right_attrs, right_x, y);
        return;
    }
    n = s_enemy_count[slot];
    if (n >= ENEMY_RENDER_MAX_PER_SLOT) return;
    e = &s_enemy_entries[slot][n];
    e->tile = left_tile;
    e->attrs = left_attrs;
    e->x = left_x;
    e->y = y;
    ++n;
    if (n < ENEMY_RENDER_MAX_PER_SLOT) {
        ++e;
        e->tile = right_tile;
        e->attrs = right_attrs;
        e->x = right_x;
        e->y = y;
        ++n;
    }
    s_enemy_count[slot] = n;
}

/* Phase D 2026-05-15 — meta-object (spark / cloud) frame publisher.
 * Reads ENEMY_METASTATE(slot), picks a frame tile from the cloud or
 * spark table, and overwrites the slot's cache entry so the native
 * sweep emits a SAT entry at the meta sprite's coordinates.
 *
 * Cloud uses the bomb-cloud item frames $70/$72/$74. Spark uses
 * item-slot $24 frames $62/$64 from CommonSpritePatterns.
 *
 * attrs: NES DrawCloud writes [04]/[05] = 1 (palette 1, blue). Use
 * sub-pal 1 = bits 1..0 of attrs = 0x01.
 *
 * Position: ENEMY_X(slot) / ENEMY_Y(slot) (the slot's last logical
 * position, suitable until the slot recycles). */
#define ENEMY_RENDER_META_CLOUD_TILE_BASE 0x60u
/* NES DrawCloud (Z_07.asm:4912) routes through Anim_WriteItemSprites
 * with item slot $01 (Bomb). Anim_ItemFrameOffsets[$01]=$03, so the
 * frame tiles read k_anim_item_frame_tiles[$03..$06] = $34, $70, $72, $74.
 *   - frame 0 ($34): bomb body (used during fuse; not for cloud)
 *   - frame 1 ($70): cloud puff frame 1
 *   - frame 2 ($72): cloud puff frame 2
 *   - frame 3 ($74): cloud puff frame 3
 *
 * NES DrawSpark uses item slot $24, Anim_ItemFrameOffsets[$24]=$2E,
 * tiles k_anim_item_frame_tiles[$2E..$2F] = $64, $62. Spark alternates
 * frame 0=$64 / frame 1=$62 (low bit toggled per metastate). */

/* Sub-pal 1 biased cloud CHR for spawn anim. NES PT0 cloud tiles
 * $70-$75 (6 tiles, 96 NES 2bpp bytes -> 192 Genesis 4bpp bytes)
 * re-biased pixels 1->5, 2->6, 3->7 so they index PAL1[5/6/7] = NES
 * sub-pal 1 colors (dark-blue $02 / light-blue $22 / white $30).
 *
 * Source: build/probes/nes_cloud_tiles.lua dump at NES Z1 room $67
 * spawn moment (~102f post-scroll). NES OAM attr=$01 confirmed.
 *
 * Common SPR bank is 1x sub-pal 0 biased so cloud tiles in common
 * range render with brown/tan colors (wrong). These biased copies
 * live in free VRAM 1300..1305 (between SCENE_OBJ_LAST=1205 and
 * ITEM_TILE_BASE=1312).
 *
 * NES OAM tile $70 = stacked PT0 $70+$71 (8x16 mode). Cloud frames:
 *   meta-state $01 -> OAM tile $70 -> Genesis VRAM tile 1300 (+1)
 *   meta-state $02 -> OAM tile $72 -> Genesis VRAM tile 1302 (+1)
 *   meta-state $03 -> OAM tile $74 -> Genesis VRAM tile 1304 (+1)
 *
 * Routed via META_ATTR_MARKER bit (NES attr bit 4 is unused) so
 * translate_tile can detect meta entries and emit raw Genesis tile
 * index instead of going through common-bank translation. */
#define ENEMY_RENDER_META_VRAM_TILE ROOMROM_CLOUD_TILE_BASE
#define ENEMY_RENDER_META_SPARK_OFFSET (ROOMROM_SPARK_TILE_BASE - ENEMY_RENDER_META_VRAM_TILE)
#define META_ATTR_MARKER            0x10u

/* ITEM_ATTR_MARKER (NES OAM attr bit 3, unused). Set by anim_write_
 * item_sprites path when publishing item tiles to enemy_render cache.
 * translate_tile detects this + remaps NES item tile ID -> ITEM atlas
 * tile index via k_nes_item_tile_to_atlas_idx. Without this, item
 * tiles (arrows from Moblin, boomerangs from Goriya, etc.) route to
 * common SPR atlas where their NES tile ID coincides with enemy CHR
 * data → user sees octorok-rock pixels instead of arrow sprite. */
#define ITEM_ATTR_MARKER            0x08u
#include "../../../RoomRom/src/atlas/items_chr_x4.h"  /* ROOMROM_ATLAS_ITEMS_X4_* */
/* NES item tile ID -> items_chr_x4 atlas index ($FF = not in the atlas),
 * generated from the item manifest (tools/atlas/gen_item_tile_atlas_idx.py).
 * T-092: the hand-written table had drifted (42 of 76 entries wrong vs live
 * NES CHR). */
#include "item_tile_atlas_idx.h"
static const unsigned char k_cloud_chr_subpal1[6 * 32] = {
    /* NES tile $70 -> Genesis VRAM 1300 */
    0x00u, 0x00u, 0x07u, 0x77u, 0x00u, 0x00u, 0x77u, 0x77u,
    0x00u, 0x77u, 0x77u, 0x77u, 0x07u, 0x77u, 0x67u, 0x77u,
    0x07u, 0x76u, 0x77u, 0x77u, 0x07u, 0x76u, 0x77u, 0x77u,
    0x77u, 0x76u, 0x77u, 0x77u, 0x77u, 0x77u, 0x67u, 0x77u,
    /* NES tile $71 -> Genesis VRAM 1301 */
    0x77u, 0x77u, 0x77u, 0x77u, 0x77u, 0x77u, 0x77u, 0x77u,
    0x07u, 0x77u, 0x77u, 0x77u, 0x07u, 0x67u, 0x76u, 0x77u,
    0x00u, 0x67u, 0x76u, 0x67u, 0x00u, 0x67u, 0x77u, 0x66u,
    0x00u, 0x06u, 0x77u, 0x77u, 0x00u, 0x00u, 0x77u, 0x70u,
    /* NES tile $72 -> Genesis VRAM 1302 */
    0x00u, 0x00u, 0x00u, 0x77u, 0x00u, 0x00u, 0x77u, 0x70u,
    0x00u, 0x70u, 0x70u, 0x67u, 0x00u, 0x70u, 0x00u, 0x77u,
    0x00u, 0x07u, 0x00u, 0x07u, 0x00u, 0x77u, 0x00u, 0x00u,
    0x07u, 0x77u, 0x07u, 0x70u, 0x70u, 0x07u, 0x07u, 0x00u,
    /* NES tile $73 -> Genesis VRAM 1303 */
    0x76u, 0x70u, 0x70u, 0x00u, 0x06u, 0x70u, 0x70u, 0x67u,
    0x00u, 0x70u, 0x00u, 0x77u, 0x07u, 0x70u, 0x70u, 0x77u,
    0x00u, 0x77u, 0x00u, 0x07u, 0x00u, 0x07u, 0x70u, 0x00u,
    0x00u, 0x07u, 0x07u, 0x77u, 0x00u, 0x00u, 0x70u, 0x70u,
    /* NES tile $74 -> Genesis VRAM 1304 */
    0x00u, 0x00u, 0x00u, 0x70u, 0x00u, 0x00u, 0x00u, 0x00u,
    0x00u, 0x00u, 0x70u, 0x00u, 0x00u, 0x70u, 0x00u, 0x00u,
    0x00u, 0x00u, 0x00u, 0x77u, 0x00u, 0x07u, 0x00u, 0x00u,
    0x00u, 0x70u, 0x00u, 0x00u, 0x00u, 0x00u, 0x70u, 0x70u,
    /* NES tile $75 -> Genesis VRAM 1305 */
    0x70u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x70u, 0x07u,
    0x00u, 0x70u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x07u,
    0x00u, 0x70u, 0x00u, 0x00u, 0x00u, 0x00u, 0x07u, 0x00u,
    0x00u, 0x00u, 0x00u, 0x07u, 0x00u, 0x00u, 0x07u, 0x70u,
};
static unsigned char s_cloud_chr_uploaded = 0u;
static unsigned char s_spark_chr_uploaded = 0u;
extern const unsigned char common_chr[];
static unsigned char s_compass_chr_uploaded;

/* NES source: Z_01.asm:Anim_ItemFrameOffsets/Anim_ItemFrameTiles (slot $10).
 * Drained C: draw_dispatch.c item tables + translate_tile.
 * Coverage: FULL compass item publication; its common copy was overwritten.
 * Stance: EXTEND the existing protected common-pattern residency owner.
 * Live L4/L6 OAM selects $6A/$6B; both pairs match ROM-derived common_chr.
 * Preserve attrs (including the mirrored right half) and palette routing. */
static void ensure_compass_chr(void)
{
    if (!s_compass_chr_uploaded) {
        render_chr_upload((unsigned short)(ROOMROM_COMPASS_TILE_BASE * 32u),
                          common_chr + 0x6Au * 32u,
                          ROOMROM_COMPASS_TILE_COUNT * 32u);
        s_compass_chr_uploaded = 1u;
    }
}

/* PAL1[13..15] contains NES sprite sub-pal 3. Bias only opaque ROM-derived
 * pixels; tile index zero must stay transparent. */
static void upload_sprite_subpal3(unsigned short vram_tile,
                                  const unsigned char *source,
                                  unsigned char tile_count)
{
    unsigned char tile, i;
    unsigned char biased[32];
    for (tile = 0u; tile < tile_count; ++tile) {
        for (i = 0u; i < 32u; ++i) {
            unsigned char pixel_pair = source[(unsigned short)tile * 32u + i];
            unsigned char hi = (unsigned char)(pixel_pair >> 4);
            unsigned char lo = (unsigned char)(pixel_pair & 0x0Fu);
            biased[i] = (unsigned char)
                (((hi ? (unsigned char)(hi + 12u) : 0u) << 4) |
                 (lo ? (unsigned char)(lo + 12u) : 0u));
        }
        render_chr_upload((unsigned short)((vram_tile + tile) * 32u), biased, 32u);
    }
}

/* NES DrawSpark selects item-slot $24 frames $62/$64. The ROM-derived
 * common_chr contains both 8x16 pairs at $62..$65, but the transient
 * enemy bank overwrites their usual VRAM addresses. Copy them into a
 * stable slot. Hit invincibility can flash through all four sub-pals. */
static void spark_chr_ensure_uploaded(void)
{
    if (s_spark_chr_uploaded) return;
    render_chr_upload((unsigned short)(ROOMROM_SPARK_TILE_BASE * 32u),
                      common_chr + 0x62u * 32u,
                      ROOMROM_SPARK_TILE_COUNT * 32u);
    upload_sprite_subpal3(ROOMROM_SPARK_SUBPAL3_TILE_BASE,
                          common_chr + 0x62u * 32u,
                          ROOMROM_SPARK_SUBPAL3_TILE_COUNT);
    s_spark_chr_uploaded = 1u;
}

static void cloud_chr_ensure_uploaded(void)
{
    if (s_cloud_chr_uploaded) return;
    render_chr_upload((unsigned short)(ENEMY_RENDER_META_VRAM_TILE * 32u),
                      k_cloud_chr_subpal1,
                      (unsigned short)sizeof(k_cloud_chr_subpal1));
    s_cloud_chr_uploaded = 1u;
}

/* NES source: TransferCommonPatternBlocks + DrawCloud/DrawSpark.
 * Drained C: the existing one-time FX upload owners above.
 * Coverage: first cloud/death-spark draw; fixed reserved VRAM survives scenes.
 * Stance: EXTEND residency preparation before gameplay, retaining lazy guards. */
static void ensure_fireball_chr(void);
void enemy_render_prepare_fx_chr(void)
{
    cloud_chr_ensure_uploaded();
    spark_chr_ensure_uploaded();
    /* T-172: the first boss fireball uploaded it mid-fight (Gleeok
     * t171_boss_l8 t312 lag frame). Fixed VRAM, survives scene loads. */
    ensure_fireball_chr();
}

/* T-110 / T-116: weapon-slot ($0E, $10, $11) sprite cache. Owned by the
 * shot object (src/game/items/sword_shot.c) and the bomb / fire
 * object (src/game/items/bomb.c), which resets and refills its slot on
 * every update, so entries survive enemy_render_reset_oam (that runs
 * later in the frame than the weapon update). Entries hold NES OAM
 * fields; item tiles carry ITEM_ATTR_MARKER, cloud frames use the same
 * biased cloud bank as enemy_render_publish_meta (DrawCloud is the one
 * NES routine behind both). */
#define ENEMY_RENDER_WEAPON_SLOTS      4u
#define ENEMY_RENDER_WEAPON_MAX        8u   /* 4 clouds x mirrored pair */
static enemy_render_entry_t
    s_weapon_entries[ENEMY_RENDER_WEAPON_SLOTS][ENEMY_RENDER_WEAPON_MAX];
static unsigned char s_weapon_count[ENEMY_RENDER_WEAPON_SLOTS];

/* Cache index, in NES weapon update order: $0E, $10, $11. */
static unsigned char weapon_index(unsigned char slot)
{
    if (slot == 0x0Eu) return 0u;
    if (slot == 0x10u) return 1u;
    if (slot == 0x11u) return 2u;
    if (slot == 0x13u) return 3u;              /* room item (T-130) */
    return 0xFFu;
}

void enemy_render_weapon_reset(unsigned char slot)
{
    unsigned char w = weapon_index(slot);
    if (w < ENEMY_RENDER_WEAPON_SLOTS) s_weapon_count[w] = 0u;
}

/* NES HideAllSprites: no weapon or room-item sprite until its owner draws
 * again (an owner that is not updated, e.g. a fire across Modes 8/3,
 * would otherwise keep its last frame on screen). */
void enemy_render_weapon_reset_all(void)
{
    unsigned char w;
    for (w = 0u; w < ENEMY_RENDER_WEAPON_SLOTS; ++w) s_weapon_count[w] = 0u;
}

static void weapon_add(unsigned char slot, unsigned char tile,
                       unsigned char attrs, unsigned char x, unsigned char y)
{
    unsigned char w = weapon_index(slot);
    unsigned char n;
    if (w >= ENEMY_RENDER_WEAPON_SLOTS) return;
    n = s_weapon_count[w];
    if (n >= ENEMY_RENDER_WEAPON_MAX) return;
    s_weapon_entries[w][n].tile  = tile;
    s_weapon_entries[w][n].attrs = attrs;
    s_weapon_entries[w][n].x     = x;
    s_weapon_entries[w][n].y     = y;
    s_weapon_count[w] = (unsigned char)(n + 1u);
}

void enemy_render_weapon_add_item(unsigned char slot, unsigned char tile,
                                  unsigned char attrs, unsigned char x,
                                  unsigned char y)
{
    weapon_add(slot, tile, (unsigned char)(attrs | ITEM_ATTR_MARKER), x, y);
}

void enemy_render_weapon_add_obj(unsigned char slot, unsigned char tile,
                                 unsigned char attrs, unsigned char x,
                                 unsigned char y)
{
    /* DrawObjectWithType path (no item marker): fire $5C-$5F resolves
     * through the NES_FIRE_TILE route in translate_tile. */
    weapon_add(slot, tile, attrs, x, y);
}

void enemy_render_weapon_add_cloud(unsigned char slot, unsigned char frame,
                                   unsigned char attrs, unsigned char x,
                                   unsigned char y)
{
    /* Frame 1..3 = NES tiles $70/$72/$74 = biased bank offsets 0/2/4.
     * Mirrored pair, X separation 8 (Anim_WriteMirroredSpritePair). */
    unsigned char off = (unsigned char)((frame - 1u) * 2u);
    cloud_chr_ensure_uploaded();
    /* The biased cloud bank already encodes palette row 1 (see
     * k_cloud_chr_subpal1), so row 1 maps to marker palette bits 0 as in
     * enemy_render_publish_meta; any other row (invincibility flash)
     * passes through. */
    unsigned char pal = (unsigned char)((attrs & 0x03u) == 0x01u ? 0u : (attrs & 0x03u));
    weapon_add(slot, off, (unsigned char)(META_ATTR_MARKER | pal), x, y);
    weapon_add(slot, off, (unsigned char)(META_ATTR_MARKER | pal | 0x40u),
               (unsigned char)(x + 8u), y);
}

void enemy_render_publish_meta(unsigned int slot)
{
    if (slot > (unsigned int)ENEMY_LOOP_SLOT_LAST) return;
    unsigned char ms = (unsigned char)ENEMY_METASTATE(slot);
    if (ms == 0u) return;

    /* Map metastate -> Genesis VRAM tile offset within biased cloud bank.
     * Cloud frame 1 (ms=$01) = NES OAM tile $70 = stacked PT0 $70+$71
     * = Genesis VRAM 1300+1301 = offset 0 (Genesis SIZE(1,2) fetches
     * tile_base + tile_base+1 vertically).
     * Cloud frame 2 (ms=$02) = $72+$73 = Genesis 1302+1303 = offset 2.
     * Cloud frame 3 (ms=$03) = $74+$75 = Genesis 1304+1305 = offset 4. */
    unsigned char gen_tile_offset;
    if (ms >= 0x10u) {
        /* $10 is blank; $11/$13 use frame 1 ($62), $12 uses frame 0
         * ($64). The right side mirrors the same 8x16 tile. */
        if (ms == 0x10u) return;
        spark_chr_ensure_uploaded();
        gen_tile_offset = (unsigned char)(ROOMROM_SPARK_TILE_BASE
                         - ENEMY_RENDER_META_VRAM_TILE + ((ms & 1u) ? 0u : 2u));
    } else {
        cloud_chr_ensure_uploaded();
        unsigned char frame = (unsigned char)(ms & 0x03u);
        if (frame == 0u || frame == 1u) gen_tile_offset = 0u;  /* $70 */
        else if (frame == 2u)           gen_tile_offset = 2u;  /* $72 */
        else                            gen_tile_offset = 4u;  /* $74 ($03) */
    }

    /* NES DrawCloud (Z_07.asm:4912) writes frame param to $0C clobbering
     * DRAW_MIRRORED, so cloud frames 1-3 hit Anim_WriteMirroredSpritePair
     * (right tile = left, h_flip on right). Emit 2 entries with marker
     * bit so translate_tile uses ENEMY_RENDER_META_VRAM_TILE base. */
    unsigned char x = (unsigned char)ENEMY_RENDER_OBJ_X(slot);
    unsigned char y = (unsigned char)ENEMY_RENDER_OBJ_Y(slot);

    enemy_render_entry_t *eL = &s_enemy_entries[slot][0];
    eL->tile  = gen_tile_offset;
    eL->attrs = (unsigned char)(META_ATTR_MARKER | ((ms >= 0x10u) ? 1u : 0u));
    eL->x     = x;
    eL->y     = y;

    enemy_render_entry_t *eR = &s_enemy_entries[slot][1];
    eR->tile  = gen_tile_offset;                        /* same tile (mirrored) */
    eR->attrs = (unsigned char)(eL->attrs | 0x40u);  /* + h-flip */
    eR->x     = (unsigned char)(x + ((ms >= 0x10u) ? 7u : 8u));
    eR->y     = y;

    s_enemy_count[slot] = 2u;
}

void enemy_render_reset_oam(void)
{
    unsigned int i;
    /* Both render paths consume only this frame's writes. Boss rooms
     * read OAM; ordinary rooms read the native cache. Clearing only at
     * the end of the native sweep preserved old entries in boss rooms.
     * NES hides unused sprites by Y=$F0; only the 40 enemy Y cells need
     * clearing, not the complete 256-byte OAM mirror. */
    enemy_render_native_reset();
    {
        /* T-125: 8 stores per step (runs every frame). */
        unsigned char *p = &RAM(NES_SPRITES_BASE + 24u * 4u);
        for (i = 0u; i < 5u; ++i, p += 32) {
            p[0] = 0xF0u;  p[4] = 0xF0u;  p[8] = 0xF0u;  p[12] = 0xF0u;
            p[16] = 0xF0u; p[20] = 0xF0u; p[24] = 0xF0u; p[28] = 0xF0u;
        }
    }
    RAM(NES_ROLLING_SPR_INDEX) = 0u;
}

/* Genesis-native sweep: NES OAM mirror -> SGDK SAT slots 32-72.
 *
 * NES OAM record (4 bytes per sprite):
 *   +0 Y
 *   +1 tile
 *   +2 attrs (bit 7 = v_flip, bit 6 = h_flip, bit 5 = behind-BG,
 *             bits 1-0 = sub-palette)
 *   +3 X
 *
 * Genesis SAT (SGDK VDP_setSprite):
 *   y, x, size, link, tile + palette<<13 + h_flip<<11 + v_flip<<12 + priority<<15
 *
 * Tile-ID translation: nes_tile -> ROOMROM_SPR_TILE_BASE + nes_tile.
 * NES sprite CHR window is 256 tiles ($00..$FF) but Z1 uses $00..$BF
 * range for sprites in 8x16 mode. Our SPR_BASE (1025) + SCENE_OBJ
 * offset (44) puts enemy CHR at Genesis tile 1069. Initial mapping is
 * identity offset off SPR_BASE; per-enemy refined mapping lands in
 * follow-up commits.
 *
 * Sub-palette mapping (NES sub-pal $0..$3 -> Genesis PAL$0..$3):
 *   sub_pal 0 -> PAL0
 *   sub_pal 1 -> PAL1 (Link)
 *   sub_pal 2 -> PAL2 (items)
 *   sub_pal 3 -> PAL3
 * Items already live in PAL2 per memory project_pr2_vram_relocation.
 * Enemies typically use sub_pal 1 (red) or 3 (blue). Direct mapping
 * works as default; refined per-bank coloring lands later.
 */

/* SAT slot allocation 2026-05-15 post-HUD-backdrop-retirement
 * (H32 mode, 64 hardware slots total):
 *   0..9   = Link + sword + items + projectiles (sprite_render.c)
 *   10..63 = enemy render bridge (54 slots for NES OAM sprites)
 * Slots 64+ are not evaluated by VDP in H32 mode. Slot contract lives
 * in sprite_slots.h; never hard-code 10 / 63 here. */
#if ROOMROM_SPRITE_SLOT_LAST_H32 > 63u
#  error "H32 mode supports only 64 hardware SAT slots."
#endif

/* 2026-05-15 perf-finding: sweep + render_set_sprite_full chain costs
 * ~30% of frame budget when 11 enemies are alive. Capping the write
 * count did NOT recover proportionally; the per-call cost is irreducible
 * at this layer. Long-term fix = Genesis-native enemy renderer that
 * reads ENEMY_X/Y/TYPE/DRAW_FRAME directly and writes 1 SAT entry per
 * alive enemy (planned next commit). For now, sweep iterates the full
 * H32 enemy slot range. */
#define ENEMY_RENDER_SLOT_LAST ROOMROM_SPRITE_SLOT_LAST_H32
/* NES Z1 OAM mirror is 64 sprites x 4 bytes = 256 bytes at $0200..$02FF.
 * Drained Anim_WriteSprite (Z_01.asm:5365) writes via SpriteOffsets[] —
 * scattered offsets like $60, $BC, $64, $B8 not linear. Sweep must
 * iterate all 64 OAM slots so the scattered writes land in SAT. */
#define NES_OAM_SLOT_COUNT      64u
#define NES_HUD_Y_OFFSET        32u   /* HUD on Window plane covers top 4 rows */

/* SPR_TILE_BASE canonical value comes from roomrom_vram_map.h
 * (= 533u post-Phase-J.2 cleanup 2026-05-18). Was 1025u pre-cleanup
 * (1 + 4*256 4x sub-pal stride). Local re-#define removed: was causing
 * enemies (octorok/tektite/moblin/etc) to render INVISIBLE because SAT
 * wrote tile_ids 1025+N into empty VRAM region — VRAM atlas now ends
 * at slot ~1100. Use canonical macro from roomrom_vram_map.h. */

/* NES Z1 sprite CHR layout in our Genesis VRAM:
 *
 * Per reference/aldonunez/Z_03.asm:44 PatternBlockPpuAddrs:
 *   $1700 = BG block dest PPU addr  (BG sprites)
 *   $08E0 = SPRITE block dest PPU addr  (OWSP/UWSP banks)
 *
 * So OWSP/UWSP bank file BYTE 0 -> NES PPU byte $08E0 = PPU tile $8E.
 * NES OAM tile id $XX in 8x16 mode -> top 8x8 at PPU byte ($XX*$10 + bit 0).
 *
 * Genesis VRAM:
 *   NES PPU tile $00..$8D (CommonSpritePatterns + headroom, 142 tiles)
 *     -> SPR_BASE + nes_tile = Genesis 1025+nes_tile.
 *   NES PPU tile $8E..$FF (transient sprite bank, OWSP up to 114 tiles,
 *     UWSP up to 34 tiles x 4 sub-pal copies)
 *     -> SCENE_OBJ tile_base + (nes_tile - $8E)
 *     = Genesis 1069 + bank_tile.
 *
 * UW path adds sub_pal*34 offset to select the correct 4x copy.
 *
 * Pre-2026-05-15 bug: used $70 instead of $8E as the bank base,
 * landing every enemy tile 30 tiles too low in VRAM -> rendered
 * unrelated atlas data as Tektite (diagonal slash instead of spider). */
/* SCENE_OBJ overlay tile_base = SPR_BASE + 44 (post-Phase-J.2 = 577u).
 * Was 1069u (= 1025 + 44) pre-cleanup. Derived from canonical SPR base. */
#define ROOMROM_SCENE_OBJ_TILE_BASE  (ROOMROM_SPR_TILE_BASE + 44u)
#define NES_OWSP_BANK_FIRST          0x8Eu   /* PPU $08E0 / $10 */
#define UWSP_TILES_PER_SUBPAL        34u
#define NES_CUR_LEVEL_CELL           0x0010u

/* Cave bonfire / candle flame. NES CommonSpritePatterns $5C-$5F are the
 * always-loaded fire art ($1000-$16FF on NES; never bank-swapped). On
 * Genesis the common SPR slot for $5C ($2BD = SPR_BASE+$5C) is CLOBBERED
 * by the OWSP overlay: ROOMROM_SCENE_OBJ_TILE_BASE (SPR_BASE+44) overlaps
 * the common SPR bank, so loading the OW NPC/cave-dweller bank stomps
 * common tiles $2C-$9D (documented VRAM compaction, RoomRom/src/main.c
 * :1650 "last-writer wins"). VRAM is too tight to relocate SCENE_OBJ
 * (114-tile bank vs 75 free tiles before the table region).
 *
 * The IDENTICAL flame art is permanently resident in the ITEM atlas at
 * ITEM_BASE+38..41 ($3A6-$3A9) — byte-verified == NES $5C-$5F via
 * tools/parity/cave_golden f060 (uploaded by roomrom_sprites_upload_items
 * _chr at boot + every scene_load; ITEM bank lives above the SCENE_OBJ
 * overlap so it is never clobbered). Route the fire tiles there so the
 * StandingFire bonfire (NES Z_01 ObjType $40) renders the flame instead
 * of the blank clobbered common slot. NES $5C-$5F = fire art everywhere,
 * so this route is unconditional (any sprite using these tiles wants the
 * fire). */
#define NES_FIRE_TILE_FIRST          0x5Cu
#define NES_FIRE_TILE_LAST           0x5Fu
#define ITEM_ATLAS_FLAME_IDX         38u

/* Set per-frame from the CHR loader's resident bank, including after a
 * boss splits into ordinary child types or clears its type on death. Boss draw
 * routines emit raw NES tiles $C0+ (PPU $0C00 bank); translate_tile remaps
 * those to ROOMROM_BOSS_TILE_BASE when this is set. */
static unsigned char s_boss_bank_active = 0u;

/* CommonSpritePatterns is already extracted from the supplied ROM.
 * Its $44/$45 bytes match live NES boss CHR (bank 2 offset $04BF).
 * Keep these tiles outside the bank overlay at SPR_BASE+44. */
static unsigned char s_fireball_chr_uploaded;
static void ensure_fireball_chr(void)
{
    if (s_fireball_chr_uploaded) return;
    render_chr_upload((unsigned short)(ROOMROM_FIREBALL_TILE_BASE * 32u),
                      common_chr + 0x44u * 32u,
                      ROOMROM_FIREBALL_TILE_COUNT * 32u);
    upload_sprite_subpal3(ROOMROM_FIREBALL_SUBPAL3_TILE_BASE,
                          common_chr + 0x44u * 32u,
                          ROOMROM_FIREBALL_SUBPAL3_TILE_COUNT);
    s_fireball_chr_uploaded = 1u;
}

/* T-169: an odd tile id in NES 8x16 mode takes pattern table 1, the
 * background tiles: the UW push block is DrawBlock's BG pair $B0/$B1 +
 * $B2/$B3 with attr 3 (t054_uw_block42_nes NES OAM 38/57 at t880). The
 * generic path drew sprite-table art there (Genesis tiles $2D7/$2D9, an
 * enemy). The BG atlas holds each BG tile in four copies biased by 4 * k
 * for BG sub-pal k (bg_sparse_tile_lut); under PAL1, which holds the four
 * NES sprite palettes, the copy for k shows NES sprite palette k exactly.
 * A Genesis 8x16 sprite needs its two tiles adjacent, so the pair is
 * copied into ROOMROM_PT1_PAIR_TILE_BASE on demand (4 cached pairs, keyed
 * by tile, sub-pal and level). */
extern const unsigned short bg_sparse_tile_lut[256][4];
#define PT1_PAIR_SLOTS (ROOMROM_PT1_PAIR_TILE_COUNT / 2u)
static unsigned char s_pt1_tile[PT1_PAIR_SLOTS];
static unsigned char s_pt1_pal[PT1_PAIR_SLOTS];
static unsigned char s_pt1_lvl[PT1_PAIR_SLOTS];
static unsigned char s_pt1_used[PT1_PAIR_SLOTS];
static unsigned char s_pt1_next;

/* T-172: CHR cache fills (sub-palette 3 copies, PT1 pairs) are built in
 * one of these 64-byte buffers and queued for the next VBlank DMA with the
 * SAT, instead of an immediate DMA during the display. Buffers are free
 * again at the next tick (the queue drains in its VBlank). */
#define CHR_STAGE_BUFS 4u
static unsigned long s_chr_stage[CHR_STAGE_BUFS][16];
static unsigned char s_chr_stage_used;
static unsigned char s_chr_stage_stamp = 0xFFu;

static unsigned long *chr_stage_take(void)
{
    if (s_chr_stage_stamp != (unsigned char)RAM(0x0015u)) {
        s_chr_stage_stamp = (unsigned char)RAM(0x0015u);
        s_chr_stage_used = 0u;
    }
    if (s_chr_stage_used >= CHR_STAGE_BUFS) return (unsigned long *)0;
    return s_chr_stage[s_chr_stage_used];
}

static unsigned char chr_stage_queue(unsigned short vram_tile, const unsigned long *buf)
{
    if (!render_vram_queue_words((unsigned short)(vram_tile * 32u),
                                 (const unsigned short *)(const void *)buf, 32u))
        return 0u;
    s_chr_stage_used = (unsigned char)(s_chr_stage_used + 1u);
    return 1u;
}

/* Clear every 4bpp pixel equal to `zero` (1..15) in eight-pixel longs. */
static void chr_clear_color(unsigned long *d, unsigned char words, unsigned char zero)
{
    const unsigned long pat = (unsigned long)zero * 0x11111111UL;
    unsigned char w;
    for (w = 0u; w < words; ++w) {
        const unsigned long x = d[w];
        const unsigned long t = x ^ pat;
        const unsigned long eq = ~(t | (t >> 1) | (t >> 2) | (t >> 3)) & 0x11111111UL;
        d[w] = x & ~(eq * 0xFUL);
    }
}

static unsigned short pt1_pair(unsigned char nes_tile, unsigned char sub_pal)
{
    const unsigned char top = (unsigned char)(nes_tile & 0xFEu);
    const unsigned char lvl = nes_ram[0x0010u];
    unsigned char i, k;
    for (i = 0u; i < PT1_PAIR_SLOTS; ++i) {
        if (s_pt1_used[i] && s_pt1_tile[i] == top && s_pt1_pal[i] == sub_pal &&
            s_pt1_lvl[i] == lvl)
            return (unsigned short)(ROOMROM_PT1_PAIR_TILE_BASE + 2u * i);
    }
    i = (unsigned char)(s_pt1_next++ % PT1_PAIR_SLOTS);
    {
        unsigned long *buf = chr_stage_take();
        if (buf) {
            const unsigned short zero = (unsigned short)((sub_pal & 3u) << 2);
            for (k = 0u; k < 2u; ++k) {
                const unsigned short slot =
                    bg_sparse_tile_lut[(unsigned char)(top + k)][sub_pal & 3u];
                unsigned long *t = buf + 8u * k;
                if (slot == 0xFFFFu) {
                    unsigned char w;
                    for (w = 0u; w < 8u; ++w) t[w] = 0u;
                } else {
                    render_vram_read_run((unsigned short)((ROOMROM_BG_TILE_BASE + slot) * 32u),
                                         (unsigned short *)(void *)t, 16u);
                    /* BG pixel 0 is color 4k (opaque); a sprite's is clear. */
                    if (zero) chr_clear_color(t, 8u, (unsigned char)zero);
                }
            }
            if (chr_stage_queue((unsigned short)(ROOMROM_PT1_PAIR_TILE_BASE + 2u * i), buf))
                goto filled;
        }
    }
    for (k = 0u; k < 2u; ++k) {
        unsigned short words[16];
        const unsigned short slot = bg_sparse_tile_lut[(unsigned char)(top + k)][sub_pal & 3u];
        unsigned char w;
        if (slot == 0xFFFFu) {
            for (w = 0u; w < 16u; ++w) words[w] = 0u;
        } else {
            /* The BG copy stores pixel 0 as color 4k (opaque background);
             * a sprite's pixel 0 is transparent: 4k -> 0. */
            const unsigned short zero = (unsigned short)((sub_pal & 3u) << 2);
            render_vram_read_run((unsigned short)((ROOMROM_BG_TILE_BASE + slot) * 32u), words, 16u);
            if (zero) {
                for (w = 0u; w < 16u; ++w) {
                    unsigned short x = words[w], out = 0u;
                    unsigned char n;
                    for (n = 0u; n < 4u; ++n) {
                        unsigned short nib = (unsigned short)((x >> (n * 4u)) & 0xFu);
                        if (nib == zero) nib = 0u;
                        out = (unsigned short)(out | (nib << (n * 4u)));
                    }
                    words[w] = out;
                }
            }
        }
        render_chr_upload((unsigned short)((ROOMROM_PT1_PAIR_TILE_BASE + 2u * i + k) * 32u),
                          (const unsigned char *)words, 32u);
    }
filled:
    s_pt1_tile[i] = top;
    s_pt1_pal[i] = sub_pal;
    s_pt1_lvl[i] = lvl;
    s_pt1_used[i] = 1u;
    return (unsigned short)(ROOMROM_PT1_PAIR_TILE_BASE + 2u * i);
}

/* T-170/T-171: NES sprite sub-palette 3. PAL1 colors 12..15 hold it
 * (roomrom_bg_palette_load_palram_full, InitMode5Play's row-7 cue); the
 * routing table sends sub-palette 3 to PAL2 (sub-palette 1 colors), which
 * drew the Zora, Armos, Ghini, ... in the wrong colors (t123_slow_tiles
 * Zora: NES $0F/$1C/$16 row, Genesis $02/$22/$30). A copy of the 8x16
 * pair with its opaque pixels +12, drawn with PAL1, shows sub-palette 3
 * exactly. 32 cached pairs, keyed by the source tile; flushed when the
 * scene/boss CHR state machines start or finish a swap (the source tiles
 * change), and not filled while one is running. */
#define SP3_PAIRS (ROOMROM_SUBPAL3_PAIR_TILE_COUNT / 2u)
static unsigned short s_sp3_src[SP3_PAIRS];     /* source tile + 1; 0 = free */
static unsigned short s_sp3_key = 0xFFFFu;
/* 4bpp byte -> both pixels +12 when 1..3 (sub-palette 3 in PAL1 12..15). */
static const unsigned char k_sp3_lut[256] = {
    0x00u, 0x0Du, 0x0Eu, 0x0Fu, 0x04u, 0x05u, 0x06u, 0x07u,
    0x08u, 0x09u, 0x0Au, 0x0Bu, 0x0Cu, 0x0Du, 0x0Eu, 0x0Fu,
    0xD0u, 0xDDu, 0xDEu, 0xDFu, 0xD4u, 0xD5u, 0xD6u, 0xD7u,
    0xD8u, 0xD9u, 0xDAu, 0xDBu, 0xDCu, 0xDDu, 0xDEu, 0xDFu,
    0xE0u, 0xEDu, 0xEEu, 0xEFu, 0xE4u, 0xE5u, 0xE6u, 0xE7u,
    0xE8u, 0xE9u, 0xEAu, 0xEBu, 0xECu, 0xEDu, 0xEEu, 0xEFu,
    0xF0u, 0xFDu, 0xFEu, 0xFFu, 0xF4u, 0xF5u, 0xF6u, 0xF7u,
    0xF8u, 0xF9u, 0xFAu, 0xFBu, 0xFCu, 0xFDu, 0xFEu, 0xFFu,
    0x40u, 0x4Du, 0x4Eu, 0x4Fu, 0x44u, 0x45u, 0x46u, 0x47u,
    0x48u, 0x49u, 0x4Au, 0x4Bu, 0x4Cu, 0x4Du, 0x4Eu, 0x4Fu,
    0x50u, 0x5Du, 0x5Eu, 0x5Fu, 0x54u, 0x55u, 0x56u, 0x57u,
    0x58u, 0x59u, 0x5Au, 0x5Bu, 0x5Cu, 0x5Du, 0x5Eu, 0x5Fu,
    0x60u, 0x6Du, 0x6Eu, 0x6Fu, 0x64u, 0x65u, 0x66u, 0x67u,
    0x68u, 0x69u, 0x6Au, 0x6Bu, 0x6Cu, 0x6Du, 0x6Eu, 0x6Fu,
    0x70u, 0x7Du, 0x7Eu, 0x7Fu, 0x74u, 0x75u, 0x76u, 0x77u,
    0x78u, 0x79u, 0x7Au, 0x7Bu, 0x7Cu, 0x7Du, 0x7Eu, 0x7Fu,
    0x80u, 0x8Du, 0x8Eu, 0x8Fu, 0x84u, 0x85u, 0x86u, 0x87u,
    0x88u, 0x89u, 0x8Au, 0x8Bu, 0x8Cu, 0x8Du, 0x8Eu, 0x8Fu,
    0x90u, 0x9Du, 0x9Eu, 0x9Fu, 0x94u, 0x95u, 0x96u, 0x97u,
    0x98u, 0x99u, 0x9Au, 0x9Bu, 0x9Cu, 0x9Du, 0x9Eu, 0x9Fu,
    0xA0u, 0xADu, 0xAEu, 0xAFu, 0xA4u, 0xA5u, 0xA6u, 0xA7u,
    0xA8u, 0xA9u, 0xAAu, 0xABu, 0xACu, 0xADu, 0xAEu, 0xAFu,
    0xB0u, 0xBDu, 0xBEu, 0xBFu, 0xB4u, 0xB5u, 0xB6u, 0xB7u,
    0xB8u, 0xB9u, 0xBAu, 0xBBu, 0xBCu, 0xBDu, 0xBEu, 0xBFu,
    0xC0u, 0xCDu, 0xCEu, 0xCFu, 0xC4u, 0xC5u, 0xC6u, 0xC7u,
    0xC8u, 0xC9u, 0xCAu, 0xCBu, 0xCCu, 0xCDu, 0xCEu, 0xCFu,
    0xD0u, 0xDDu, 0xDEu, 0xDFu, 0xD4u, 0xD5u, 0xD6u, 0xD7u,
    0xD8u, 0xD9u, 0xDAu, 0xDBu, 0xDCu, 0xDDu, 0xDEu, 0xDFu,
    0xE0u, 0xEDu, 0xEEu, 0xEFu, 0xE4u, 0xE5u, 0xE6u, 0xE7u,
    0xE8u, 0xE9u, 0xEAu, 0xEBu, 0xECu, 0xEDu, 0xEEu, 0xEFu,
    0xF0u, 0xFDu, 0xFEu, 0xFFu, 0xF4u, 0xF5u, 0xF6u, 0xF7u,
    0xF8u, 0xF9u, 0xFAu, 0xFBu, 0xFCu, 0xFDu, 0xFEu, 0xFFu,
};

static unsigned short s_sp3_stamp = 0xFFFFu;   /* FrameCounter of the last check */
static unsigned char s_sp3_item_variant = 0xFFu;

/* The ITEM atlas (sword, beam, drop heart, ...) is resident for the whole
 * game and never touched by the scene/boss CHR swaps. */
#define SP3_IS_ITEM_SRC(src) ((src) >= ROOMROM_ITEM_TILE_BASE &&     (src) + 1u < ROOMROM_ITEM_TILE_BASE + ROOMROM_ITEM_TILE_COUNT_PER_PAL)

static void sp3_validate(void)
{
    const unsigned short key = (unsigned short)(
        (level_chr_swap_request_count() << 8) ^ level_chr_boss_request_count() ^
        ((unsigned short)level_chr_swap_state() << 4) ^
        ((unsigned short)level_chr_boss_state() << 12));
    const unsigned char variant = roomrom_sprites_item_chr_variant();
    unsigned char i;
    if (key == s_sp3_key && variant == s_sp3_item_variant) return;
    s_sp3_key = key;
    /* T-172: a swap keeps the ITEM-atlas copies; refilling the sword shot
     * and drop heart copies after every room load cost a lag frame in
     * busy rooms (t171_patra_sword t370). */
    for (i = 0u; i < SP3_PAIRS; ++i) {
        const unsigned short s = s_sp3_src[i];
        if (s == 0u) continue;
        if (variant != s_sp3_item_variant || !SP3_IS_ITEM_SRC((unsigned short)(s - 1u)))
            s_sp3_src[i] = 0u;
    }
    s_sp3_item_variant = variant;
}

/* T-172: ITEM-atlas copies are built from the ROM atlas and queued for the
 * next VBlank with the SAT (no VDP read and no DMA in active display; the
 * old path cost ~700 instructions plus VDP stalls mid-frame). Four buffers
 * per tick: the queue drains at the next tick's VBlank. */
#define SP3_QUEUE_BUFS 4u
static unsigned long s_sp3_dma_buf[SP3_QUEUE_BUFS][16];
static unsigned char s_sp3_dma_used;
static unsigned char s_sp3_dma_stamp = 0xFFu;

static unsigned char sp3_fill_item(unsigned short src, unsigned char i)
{
    const unsigned char *p;
    const unsigned long *s;
    unsigned long *d;
    unsigned char w;
    if (s_sp3_dma_stamp != (unsigned char)RAM(0x0015u)) {
        s_sp3_dma_stamp = (unsigned char)RAM(0x0015u);
        s_sp3_dma_used = 0u;
    }
    if (s_sp3_dma_used >= SP3_QUEUE_BUFS) return 0u;
    p = &roomrom_atlas_items_x4[roomrom_sprites_item_chr_variant()]
                               [(unsigned short)(src - ROOMROM_ITEM_TILE_BASE) * 32u];
    if ((unsigned long)p & 1u) return 0u;
    s = (const unsigned long *)(const void *)p;
    d = s_sp3_dma_buf[s_sp3_dma_used];
    for (w = 0u; w < 16u; ++w) {
        /* Eight 4bpp pixels: +12 where the pixel is 1..3 (= k_sp3_lut). */
        const unsigned long x = s[w];
        const unsigned long m = (x | (x >> 1)) & ~((x >> 2) | (x >> 3)) & 0x11111111UL;
        d[w] = x + (m << 3) + (m << 2);
    }
    if (!render_vram_queue_words((unsigned short)((ROOMROM_SUBPAL3_PAIR_TILE_BASE + 2u * i) * 32u),
                                 (const unsigned short *)(const void *)d, 32u))
        return 0u;
    s_sp3_dma_used = (unsigned char)(s_sp3_dma_used + 1u);
    return 1u;
}

/* Fill cache entry i with the biased copy of src; 0xFFFF = not now. */
static unsigned short __attribute__((noinline)) sp3_fill(unsigned short src, unsigned char i)
{
    const unsigned char boss = level_chr_boss_state();
    if (SP3_IS_ITEM_SRC(src) && sp3_fill_item(src, i)) {
        s_sp3_src[i] = (unsigned short)(src + 1u);
        return (unsigned short)(ROOMROM_SUBPAL3_PAIR_TILE_BASE + 2u * i);
    }
    if (!level_chr_swap_is_ready() ||
        (boss != LEVEL_CHR_SWAP_IDLE && boss != LEVEL_CHR_SWAP_READY))
        return 0xFFFFu;                          /* source tiles in flux */
    {
        unsigned long *buf = chr_stage_take();
        if (buf) {
            unsigned char w;
            render_vram_read_run((unsigned short)(src * 32u),
                                 (unsigned short *)(void *)buf, 32u);
            for (w = 0u; w < 16u; ++w) {
                /* Eight 4bpp pixels: +12 where the pixel is 1..3 (k_sp3_lut). */
                const unsigned long x = buf[w];
                const unsigned long m = (x | (x >> 1)) & ~((x >> 2) | (x >> 3)) & 0x11111111UL;
                buf[w] = x + (m << 3) + (m << 2);
            }
            if (chr_stage_queue((unsigned short)(ROOMROM_SUBPAL3_PAIR_TILE_BASE + 2u * i), buf)) {
                s_sp3_src[i] = (unsigned short)(src + 1u);
                return (unsigned short)(ROOMROM_SUBPAL3_PAIR_TILE_BASE + 2u * i);
            }
        }
    }
    {
        /* Both tiles of the pair are adjacent: one read, one write. */
        unsigned short words[32];
        unsigned char w;
        render_vram_read_run((unsigned short)(src * 32u), words, 32u);
        for (w = 0u; w < 32u; ++w) {
            const unsigned short x = words[w];
            words[w] = (unsigned short)(((unsigned short)k_sp3_lut[x >> 8] << 8) |
                                        k_sp3_lut[x & 0xFFu]);
        }
        /* render_chr_upload: interrupts off (an open data port can be
         * moved by the VBlank handler in a long frame). */
        render_chr_upload((unsigned short)((ROOMROM_SUBPAL3_PAIR_TILE_BASE + 2u * i) * 32u),
                          (const unsigned char *)words, 64u);
    }
    s_sp3_src[i] = (unsigned short)(src + 1u);
    return (unsigned short)(ROOMROM_SUBPAL3_PAIR_TILE_BASE + 2u * i);
}

/* Direct-mapped by the pair index (src / 2): one compare per sprite
 * (T-172: a linear search cost room $38 ~60 instructions a frame). */
static inline unsigned short sp3_pair(unsigned short src)
{
    const unsigned char i = (unsigned char)((src >> 1) & (SP3_PAIRS - 1u));
    /* Validated once per tick, on the first sub-palette 3 sprite: rooms
     * without one pay nothing. */
    if (s_sp3_stamp != (unsigned short)RAM(0x0015u)) {
        s_sp3_stamp = (unsigned short)RAM(0x0015u);
        sp3_validate();
    }
    if (s_sp3_src[i] == (unsigned short)(src + 1u))
        return (unsigned short)(ROOMROM_SUBPAL3_PAIR_TILE_BASE + 2u * i);
    return sp3_fill(src, i);
}

static inline unsigned short translate_tile(unsigned char nes_tile,
                                            unsigned char nes_attrs)
{
    /* Meta-cloud marker: nes_attrs bit 4 (unused in real NES OAM) flags
     * tile field as direct Genesis VRAM offset from ENEMY_RENDER_META_VRAM_TILE.
     * publish_meta uses this for sub-pal 1 biased cloud tiles. */
    if (nes_attrs & META_ATTR_MARKER) {
        if ((nes_attrs & 0x03u) == 3u &&
            nes_tile >= ENEMY_RENDER_META_SPARK_OFFSET &&
            nes_tile < ENEMY_RENDER_META_SPARK_OFFSET + ROOMROM_SPARK_TILE_COUNT)
            return (unsigned short)(ROOMROM_SPARK_SUBPAL3_TILE_BASE +
                                    nes_tile - ENEMY_RENDER_META_SPARK_OFFSET);
        return (unsigned short)(ENEMY_RENDER_META_VRAM_TILE +
                                (unsigned short)nes_tile);
    }
    if (nes_attrs & ITEM_ATTR_MARKER) {
        if (nes_tile == 0x6Au || nes_tile == 0x6Bu) {
            ensure_compass_chr();
            return (unsigned short)(ROOMROM_COMPASS_TILE_BASE + nes_tile - 0x6Au);
        }
        /* T-195: Original Magical Shield is the narrow $56/$57 pair,
         * not atlas sword_diag $48. Falling through to SPR_BASE+$56
         * selects scene-bank enemy art after a room load. */
        if (nes_tile == 0x56u || nes_tile == 0x57u)
            return (unsigned short)(ROOMROM_SHIELD_TILE_BASE + nes_tile - 0x56u);
        /* T-056: raft $6C (item slot $09) and ladder $76 (item slot $0C)
         * use the live-extracted pause tiles, resident from gameplay boot
         * (inventory_sprite_chr idx 4/5 and 6/7, sub-pal-0 encoding).
         * The atlas has no $6C and its $76 entry holds other art. */
        if (nes_tile == 0x6Cu)
            return (unsigned short)(ROOMROM_SUBSCREEN_SPRITE_TILE_BASE + 4u);
        if (nes_tile == 0x76u)
            return (unsigned short)(ROOMROM_SUBSCREEN_SPRITE_TILE_BASE + 6u);
        /* Compass (item slots $10/$12, Anim_ItemFrameTiles $6A, drawn by
         * @_Slim -> Anim_WriteMirroredSpritePair) uses the pause copy
         * (idx 10/11, byte-equal to common_chr $6A/$6B, resident from
         * gameplay boot). SPR_BASE+$6A is inside the scene/boss overlay
         * and held blank or enemy art in rooms that drop the compass. */
        if (nes_tile == 0x6Au)
            return (unsigned short)(ROOMROM_SUBSCREEN_SPRITE_TILE_BASE + 10u);
        /* 2026-05-22 — route item tiles to ITEM atlas. NES tile ID
         * maps to atlas index via lookup. Genesis VRAM slot =
         * ITEM_TILE_BASE + atlas_index. */
        unsigned char atlas_idx = k_nes_item_tile_to_atlas_idx[nes_tile];
        if (atlas_idx != 0xFFu)
            return (unsigned short)(ROOMROM_ITEM_TILE_BASE +
                                    (unsigned short)atlas_idx);
        /* Not an atlas tile: translate as an ordinary sprite tile. */
    }
    /* NES source: Z_04.asm:Ganon_DrawCloud/Ganon_DrawBurst ($70/$72/$74,
     * $30); Z_01 common/item sprite patterns.
     * Drained C: enemy_ganon_runtime + existing item-atlas translation.
     * Coverage: PARTIAL (T-203 ordinary burst tiles read scene overlays).
     * Stance: EXTEND the resident ROM-derived atlas owner. Live NES CHR
     * pairs match these atlas entries; SPR_BASE+tile is overwritten by
     * the scene/boss bank. Preserve ordinary attrs and sub-pal3 caching. */
    if (nes_tile == 0x30u || nes_tile == 0x70u ||
        nes_tile == 0x72u || nes_tile == 0x74u) {
        return (unsigned short)(ROOMROM_ITEM_TILE_BASE +
                                k_nes_item_tile_to_atlas_idx[nes_tile]);
    }
    /* T-050: an odd 8x16 OAM tile selects PT1 on the NES; $F3 is Zelda's
     * only one (PT1 $F2/$F3, the heart), drawn by DrawObject* paths too
     * (pond fairy hearts). Its art is the ITEM atlas drop heart, byte-
     * verified vs NES CHR (t050_pond_fairy). */
    if (nes_tile == 0xF3u) {
        return (unsigned short)(ROOMROM_ITEM_TILE_BASE + ROOMROM_ITEM_TILE_DROP_HEART);
    }
    if (nes_tile == 0x44u || nes_tile == 0x45u) {
        ensure_fireball_chr();
        if ((nes_attrs & 0x03u) == 3u)
            return (unsigned short)(ROOMROM_FIREBALL_SUBPAL3_TILE_BASE + nes_tile - 0x44u);
        return (unsigned short)(ROOMROM_FIREBALL_TILE_BASE + nes_tile - 0x44u);
    }
    if (nes_tile >= NES_FIRE_TILE_FIRST && nes_tile <= NES_FIRE_TILE_LAST) {
        /* Fire flame -> ITEM atlas (clobber-safe). See block comment above. */
        return (unsigned short)(ROOMROM_ITEM_TILE_BASE + ITEM_ATLAS_FLAME_IDX +
                                (unsigned short)(nes_tile - NES_FIRE_TILE_FIRST));
    }
    if (s_boss_bank_active && nes_tile >= 0xC0u) {
        if ((nes_attrs & 0x03u) == 3u) {
            return (unsigned short)(ROOMROM_BOSS_SUBPAL3_TILE_BASE +
                                    (unsigned short)(nes_tile - 0xC0u));
        }
        /* Boss CHR bank resident at ROOMROM_BOSS_TILE_BASE = NES PPU $0C00
         * (tile $C0) per z_03.asm:91 FetchPatternBlockUWBoss. Boss draw
         * routines (e.g. c_aquamentus_draw) emit raw NES tiles $C0+; the
         * generic $8E-relative bank math below would mis-map them by
         * ($C0-$8E)=50. Map NES $C0 -> boss bank tile 0. */
        return (unsigned short)(ROOMROM_BOSS_TILE_BASE +
                                (unsigned short)(nes_tile - 0xC0u));
    }
    /* NES source: Z_03.asm:PatternBlockPpuAddrs/PatternBlockPpuAddrsExtra.
     * Drained C: translate_tile + level_chr_swap enemy bank upload.
     * Coverage: PARTIAL scene enemy tiles; Stance: EXTEND.
     * OW starts at $08E0; per-level UWSP starts at $09E0. */
    if (nes_tile & 1u) {                         /* T-169: pattern table 1 */
        return pt1_pair(nes_tile, (unsigned char)(nes_attrs & 0x03u));
    }
    const unsigned char bank_first = nes_ram[0x0010u] ? 0x9Eu : NES_OWSP_BANK_FIRST;
    if (nes_tile < bank_first) {
        /* Common sprite pattern block at SPR_BASE 1:1. */
        return (unsigned short)(ROOMROM_SPR_TILE_BASE + (unsigned short)nes_tile);
    }
    /* Per-room transient bank: OW $8E+k / UW $9E+k -> scene tile k.
     *
     * Phase F (2026-05-18): UWSP banks collapsed from 4 sub-pal copies to
     * 1 sub-pal-0 copy. Both OW (OWSP) and UW (UWSP127/358/469) now use
     * the same single-copy resolution: tile k -> SCENE_OBJ_BASE + k. Sub-
     * pal selection routes via Genesis OAM pal field in translate_attrs
     * (PAL1/PAL2/PAL3 = NES SPR sub-pals 0/1/2 per
     * src/game/world/bg_palette.h CRAM target). */
    unsigned char bank_tile = (unsigned char)(nes_tile - bank_first);
    (void)nes_attrs;  /* sub_pal no longer needed for tile resolution */
    return (unsigned short)(ROOMROM_SCENE_OBJ_TILE_BASE + bank_tile);
}

static inline unsigned short translate_attrs(unsigned char nes_attrs,
                                             unsigned short tile_id)
{
    /* Phase F (2026-05-18): unified per-attr palette routing for OW and
     * UW. After Phase B+F, both OWSP and UWSP atlases are single-copy
     * (sub-pal-0 pixel encoding). CRAM target (bg_palette.h):
     *   PAL1[0..3] = NES SPR sub-pal 0 (Link, common, OWSP base)
     *   PAL2[0..3] = NES SPR sub-pal 1 (cloud sprite, some UW enemies)
     *   PAL3[0..3] = NES SPR sub-pal 2 (red enemies, OWSP red ramp,
     *                                   candle FX, magic shot)
     *
     * NES attr bits 0..1 select sub-pal -> route to PAL1/PAL2/PAL3.
     * Sub-pal 3 (rare, mostly unused) clamps to sub-pal 2 (PAL3).
     *
     * Pre-Phase-F behavior:
     *   OW: sub_pal 0 -> PAL1, all others -> PAL3 (sub-pal 1 was
     *       unmapped, fell back to red ramp).
     *   UW: always PAL1 with sub_pal*34 tile-offset replication.
     * Unification recovers correct sub-pal 1 colors in both contexts. */
    unsigned char h_flip   = (unsigned char)((nes_attrs >> 6) & 0x01u);
    unsigned char v_flip   = (unsigned char)((nes_attrs >> 7) & 0x01u);
    unsigned char prio     = (unsigned char)((nes_attrs >> 5) & 0x01u) ^ 0x01u;
    /* NES bit 5 = "behind BG" = priority LOW. Genesis bit = priority HIGH
     * (above plane A). Invert: NES prio=0 -> Genesis prio=1 (above). */
    /* Phase AA (2026-05-18 cleanup org): sub-pal routing centralized in
     * src/game/world/render/subpal_routing.h. */
    unsigned char sub_pal  = (unsigned char)(nes_attrs & 0x03u);
    unsigned short pal_bank = (unsigned short)roomrom_spr_subpal_to_pal(sub_pal);
    if (tile_id >= ROOMROM_BOSS_SUBPAL3_TILE_BASE &&
        tile_id < ROOMROM_BOSS_SUBPAL3_TILE_BASE + ROOMROM_BOSS_SUBPAL3_TILE_COUNT) {
        pal_bank = RENDER_PAL1;
    }
    if (tile_id >= ROOMROM_FIREBALL_SUBPAL3_TILE_BASE &&
        tile_id < ROOMROM_FIREBALL_SUBPAL3_TILE_BASE + ROOMROM_FIREBALL_SUBPAL3_TILE_COUNT) {
        pal_bank = RENDER_PAL1;
    }
    if (tile_id >= ROOMROM_SPARK_SUBPAL3_TILE_BASE &&
        tile_id < ROOMROM_SPARK_SUBPAL3_TILE_BASE + ROOMROM_SPARK_SUBPAL3_TILE_COUNT) {
        pal_bank = RENDER_PAL1;
    }
    if (tile_id >= ROOMROM_PT1_PAIR_TILE_BASE &&
        tile_id < ROOMROM_PT1_PAIR_TILE_BASE + ROOMROM_PT1_PAIR_TILE_COUNT) {
        pal_bank = RENDER_PAL1;                  /* T-169 */
    }

    unsigned short sat = (unsigned short)(tile_id & 0x07FFu);
    sat |= (unsigned short)(pal_bank << 13);
    sat |= (unsigned short)((v_flip  & 0x01u) << 12);
    sat |= (unsigned short)((h_flip  & 0x01u) << 11);
    sat |= (unsigned short)((prio    & 0x01u) << 15);
    return sat;
}

/* T-118: lookup tables for the per-sprite translate_tile/translate_attrs
 * work in emit_native_entries (PC profile, busy UW room: ~100 68000
 * instructions per sprite, 11% of the frame). s_xlat_tile holds
 * translate_tile(t, 0) for the current (boss bank, OW/UW) state and is
 * rebuilt when that state changes; XLAT_SLOW keeps the full function for
 * the fireball tiles (lazy CHR upload side effect); xlat_sat applies the
 * boss sub-pal 3 split itself. Marker attrs (META/ITEM) always take the
 * full function. s_xlat_attr holds translate_attrs(a, 0): the tile only
 * matters for the boss sub-pal 3 range, patched in xlat_sat. */
#define XLAT_SLOW 0xFFFFu
static unsigned short s_xlat_tile[256];
static unsigned short s_xlat_attr[256];
static unsigned char s_xlat_key = 0xFFu;

/* Tiles whose copy already holds the colors: drawn with sprite PAL1
 * whatever NES sub-palette the attrs name. */
static inline unsigned char xlat_force_pal1(unsigned short tid)
{
    return (unsigned char)(
        (tid >= ROOMROM_BOSS_SUBPAL3_TILE_BASE &&
         tid < ROOMROM_BOSS_SUBPAL3_TILE_BASE + ROOMROM_BOSS_SUBPAL3_TILE_COUNT) ||
        (tid >= ROOMROM_FIREBALL_SUBPAL3_TILE_BASE &&
         tid < ROOMROM_FIREBALL_SUBPAL3_TILE_BASE + ROOMROM_FIREBALL_SUBPAL3_TILE_COUNT) ||
        (tid >= ROOMROM_SPARK_SUBPAL3_TILE_BASE &&
         tid < ROOMROM_SPARK_SUBPAL3_TILE_BASE + ROOMROM_SPARK_SUBPAL3_TILE_COUNT) ||
        (tid >= ROOMROM_PT1_PAIR_TILE_BASE &&
         tid < ROOMROM_PT1_PAIR_TILE_BASE + ROOMROM_PT1_PAIR_TILE_COUNT));
}

/* Fast-path entries carry xlat_force_pal1 in bit 15 (tile ids are 11 bits),
 * so a sprite costs one table read instead of four range checks
 * (T-171: busy OW room $38 ran at the frame budget). */
#define XLAT_PAL1 0x8000u
#define XLAT_BOSS 0x4000u

static void xlat_refresh(void)
{
    unsigned char key = (unsigned char)((s_boss_bank_active ? 1u : 0u) |
                                        (nes_ram[0x0010u] ? 2u : 0u));
    unsigned int t, first = 0u;
    if (key == s_xlat_key) return;
    if (s_xlat_key == 0xFFu) {
        for (t = 0u; t < 256u; ++t)
            s_xlat_attr[t] = translate_attrs((unsigned char)t, 0u);
    } else if ((unsigned char)(key ^ s_xlat_key) == 1u) {
        /* T-172: the boss bank only remaps tiles $C0..$FF (translate_tile);
         * the full rebuild on a boss room's first ready tick cost a lag
         * frame (t013_route t7728, Aquamentus room entry). */
        first = 0xC0u;
    }
    s_xlat_key = key;
    for (t = first; t < 256u; ++t) {
        if (t == 0x44u || t == 0x45u ||
            ((t & 1u) && t != 0xF3u && !(t >= NES_FIRE_TILE_FIRST && t <= NES_FIRE_TILE_LAST))) {
            s_xlat_tile[t] = XLAT_SLOW;          /* attrs-dependent / lazy CHR */
        } else {
            unsigned short tid = translate_tile((unsigned char)t, 0u);
            s_xlat_tile[t] = (unsigned short)((tid & 0x07FFu) |
                (xlat_force_pal1(tid) ? XLAT_PAL1 : 0u) |
                ((s_boss_bank_active && t >= 0xC0u && t != 0xF3u) ? XLAT_BOSS : 0u));
        }
    }
}

/* == translate_attrs(attrs, translate_tile(tile, attrs)). */
static inline __attribute__((always_inline)) unsigned short xlat_sat(unsigned char tile, unsigned char attrs)
{
    const unsigned short e = s_xlat_tile[tile];
    unsigned short tid;
    unsigned char force;
    unsigned short sat;
    /* T-172: ordinary translated tiles need no lazy CHR/palette path. */
    if (e < 0x0800u && !(attrs & (META_ATTR_MARKER | ITEM_ATTR_MARKER)) &&
        (attrs & 3u) != 3u)
        return (unsigned short)(s_xlat_attr[attrs] | e);
    if ((attrs & (META_ATTR_MARKER | ITEM_ATTR_MARKER)) || e == XLAT_SLOW) {
        tid = translate_tile(tile, attrs);
        force = xlat_force_pal1(tid);
    } else if (e & XLAT_BOSS) {
        /* The resident boss bank has fixed copies for all sub-palettes.
         * Cache ownership in the tile word: no repeated range tests on
         * ordinary sprites, no dynamic-copy checks on Patra's children.
         * Markers and odd/PT1 tiles keep the generic path above. */
        if ((attrs & 3u) == 3u)
            return (unsigned short)((s_xlat_attr[attrs] & 0x9800u) |
                                    (RENDER_PAL1 << 13) |
                                    (ROOMROM_BOSS_SUBPAL3_TILE_BASE + tile - 0xC0u));
        return (unsigned short)(s_xlat_attr[attrs] | (e & 0x07FFu));
    } else {
        tid = e;
        force = (unsigned char)((e & XLAT_PAL1) != 0u);
    }
    if (!force && (attrs & 0x03u) == 3u) {
        const unsigned short c = sp3_pair((unsigned short)(tid & 0x07FFu));
        if (c != 0xFFFFu) { tid = c; force = 1u; }
    }
    sat = (unsigned short)(s_xlat_attr[attrs] | (tid & 0x07FFu));
    if (force) sat = (unsigned short)((sat & 0x9FFFu) | (RENDER_PAL1 << 13));
    return sat;
}

/* T-097: SAT attribute word for the death spark (NES tiles $62/$64,
 * item slot $24) drawn in Link's OAM slots by Mode 11 SubA: the stable
 * spark copy at ROOMROM_SPARK_TILE_BASE and the sprite palette of the NES
 * attrs (sub-pal 1 = PAL1 colors 4..7), as the monster death spark. */
unsigned short enemy_render_spark_sat(unsigned char nes_tile, unsigned char nes_attrs)
{
    unsigned char off;
    spark_chr_ensure_uploaded();
    xlat_refresh();
    off = (unsigned char)(ROOMROM_SPARK_TILE_BASE - ENEMY_RENDER_META_VRAM_TILE +
                          ((nes_tile == 0x62u) ? 0u : 2u));
    return xlat_sat(off, (unsigned char)(nes_attrs | META_ATTR_MARKER));
}

/* T-092: Genesis SAT attribute word (tile + palette + flips + priority)
 * for an item sprite drawn by the NES item path (status-bar boxes). */
unsigned short enemy_render_item_sat(unsigned char nes_tile, unsigned char nes_attrs)
{
    xlat_refresh();
    return xlat_sat(nes_tile, (unsigned char)(nes_attrs | ITEM_ATTR_MARKER));
}

/* NES source: Z_01.asm:Anim_WriteSprite / Z_04.asm:WriteBossSprite.
 * Drained C: enemy_render_native_sweep and draw_write_boss_sprite.
 * Coverage: PARTIAL (shared submission of existing per-frame producers).
 * Stance: EXTEND. Boss OAM and native projectile/item caches are disjoint;
 * both must feed the same bounded SAT chain in a mixed boss scene. */
/* In boss rooms, NES fireball OAM precedes the later boss body/neck OAM
 * (Gleeok: shot slot 28, neck slots 29..55).  Keep that order in the
 * Genesis SAT so the boss cannot cover its own projectile. */
#define NATIVE_PASS_ALL             0u
#define NATIVE_PASS_FIREBALLS       1u
#define NATIVE_PASS_OTHER           2u
/* All cursor/index values are bounded by the 64-entry SAT and 11 slots. */
static unsigned short emit_native_entries(unsigned short sat_slot, unsigned char pass,
                                        unsigned char *captured_hand_sat)
{
    unsigned short slot;
    xlat_refresh();
    for (slot = ENEMY_LOOP_SLOT_FIRST; slot <= ENEMY_LOOP_SLOT_LAST; ++slot) {
        unsigned char n = s_enemy_count[slot];
        unsigned char ei;
        /* Entries are this frame's draw submissions (reset with OAM).
         * A producer may clear its type and still draw its final form:
         * NES Digdogger @MakeChildren does exactly that. */
        if (n == 0u) continue;
        if (sat_slot > ENEMY_RENDER_SLOT_LAST) break;
        const unsigned char capture_hand = (unsigned char)(
            captured_hand_sat && *captured_hand_sat == 0u && n >= 2u &&
            ENEMY_TYPE(slot) == 0x27u && RAM(0x042Cu + slot) != 0u);

        /* Anim_WriteSprite flashes with FrameCounter bits (single-sprite
         * entries, ANIM_WRITE_SPRITE_MARKER, and meta clouds). The pair
         * writer flashes with ObjInvincibilityTimer bits, already in the
         * entry's attrs (T-171: a hit Stalfos showed palette 0, NES 3 =
         * timer & 3, t013_route t4702); the spark reads the timer here. */
        for (ei = 0u; ei < n; ++ei) {
            enemy_render_entry_t *e = &s_enemy_entries[slot][ei];
            unsigned char y = e->y;
            if (y == 0xF0u) continue;
            unsigned char fireball = (e->tile == 0x44u || e->tile == 0x45u);
            if ((pass == NATIVE_PASS_FIREBALLS && !fireball) ||
                (pass == NATIVE_PASS_OTHER && fireball)) continue;
            if (sat_slot > ENEMY_RENDER_SLOT_LAST) break;
            if (capture_hand && ei == 0u)
                *captured_hand_sat = (unsigned char)sat_slot;

            unsigned char render_attrs = (unsigned char)(e->attrs & ~ANIM_WRITE_SPRITE_MARKER);
            if (e->attrs & (ANIM_WRITE_SPRITE_MARKER | META_ATTR_MARKER)) {
                unsigned char inv_timer = ENEMY_RENDER_INV_TIMER(slot);
                if (inv_timer != 0u) {
                    unsigned char flash_pal = (unsigned char)(RAM(NES_FRAME_COUNTER) & 3u);
                    if ((e->attrs & META_ATTR_MARKER) &&
                        e->tile >= ENEMY_RENDER_META_SPARK_OFFSET &&
                        e->tile < ENEMY_RENDER_META_SPARK_OFFSET + ROOMROM_SPARK_TILE_COUNT)
                        flash_pal = (unsigned char)(inv_timer & 3u);
                    render_attrs = (unsigned char)((render_attrs & 0xFCu) | flash_pal);
                }
            }
            unsigned short sat_attrs = xlat_sat(e->tile, render_attrs);
            /* Phase E: each cache entry = one NES OAM (8x16). Render
             * as Genesis SIZE(1,2) for exact 1:1 mapping. Per-tile
             * h_flip preserved because each entry's attrs byte was
             * captured separately. Wide enemies (Aquamentus 24x16)
             * render as N entries (3 OAM = 3 SIZE(1,2) at successive
             * x positions), no special-case needed. */
            unsigned short size = RENDER_SPRITE_SIZE(1, 2);

            unsigned char link = (sat_slot < ENEMY_RENDER_SLOT_LAST)
                                     ? (unsigned char)(sat_slot + 1u) : 0u;
            render_set_sprite_inline((unsigned short)sat_slot,
                                     (signed short)e->x,
                                     (signed short)(y + ROOMROM_PLAY_SPRITE_DY),
                                     size, sat_attrs, link);
            ++sat_slot;
        }
    }

    /* T-110 / T-116: shot $0E and bomb / fire $10/$11, emitted after
     * the monsters in NES weapon update order (Z_07.asm UpdateMode5Play). */
    {
        unsigned char wi, ei;
        for (wi = 0u; wi < ENEMY_RENDER_WEAPON_SLOTS; ++wi) {
            for (ei = 0u; ei < s_weapon_count[wi]; ++ei) {
                if (sat_slot > ENEMY_RENDER_SLOT_LAST) break;
                enemy_render_entry_t *e = &s_weapon_entries[wi][ei];
                if (e->y == 0xF0u) continue;
                unsigned char fireball = (e->tile == 0x44u || e->tile == 0x45u);
                if ((pass == NATIVE_PASS_FIREBALLS && !fireball) ||
                    (pass == NATIVE_PASS_OTHER && fireball)) continue;
                unsigned short sat_attrs = xlat_sat(e->tile, e->attrs);
                unsigned char link = (sat_slot < ENEMY_RENDER_SLOT_LAST)
                                         ? (unsigned char)(sat_slot + 1u) : 0u;
                render_set_sprite_inline((unsigned short)sat_slot,
                                         (signed short)e->x,
                                         (signed short)(e->y + ROOMROM_PLAY_SPRITE_DY),
                                         RENDER_SPRITE_SIZE(1, 2), sat_attrs, link);
                ++sat_slot;
            }
        }
    }

    return sat_slot;
}

/* NES source: Z_04 UpdatePatra/UpdatePatraChild and Ganon draw routines.
 * Drained C: boss_patra, enemy_ganon_runtime and native pair publishers.
 * Coverage: PARTIAL (T-183 Ganon pairs previously swept twice).
 * Stance: EXTEND renderer ownership independently of resident CHR.
 * Patra and Ganon publish every part natively; other bosses retain
 * manual OAM. RoomObjTemplateType survives splits and boss death. */
unsigned char enemy_render_needs_oam_sweep(void)
{
    unsigned char room_type = RAM(0x035F);
    return (unsigned char)(level_chr_boss_is_ready() &&
                          room_type != 0x3Eu &&
                          room_type != 0x47u && room_type != 0x48u);
}

void enemy_render_sweep_oam_to_sat(void)
{
    /* $07FE belongs to native UW progress; rendering must not mutate it. */

    /* CHR residency survives the boss's type changes and death. In
     * particular Digdogger's children use type $18 and still draw tiles
     * $D8/$DA from the loaded boss bank (NES Z_04:Digdogger_Draw).
     * The bank loader clears this state when an enemy bank replaces it. */
    s_boss_bank_active = level_chr_boss_is_ready();

    unsigned int i;
    unsigned short sat_slot = ROOMROM_SPRITE_SLOT_ENEMY_FIRST;

    /* Anim_WriteSprite's SpriteOffsets reach slots 24..63, but Gleeok
     * draws its heads and the base neck segments in slots 0..15 (Sprites +
     * neck * 8, +32: Z_04.asm Gleeok_DrawHeadAndCheckCollisions; T-171
     * t171_boss_l4: both heads were missing). Slots 16..23 are Link's and
     * the status-bar marker's, which the Genesis draws itself (zero here).
     * WriteBlankPrioritySprites fills slots 0..15 with transparent tile
     * $1C behind the BG (only the NES 8-per-line limit made them matter):
     * skipped. */
    xlat_refresh();   /* T-125: table lookups, as the native path */
    sat_slot = emit_native_entries(sat_slot, NATIVE_PASS_FIREBALLS, 0);
    {
    /* T-172: one long read per OAM record (Y, tile, attrs, X). */
    const unsigned long *oam =
        (const unsigned long *)(unsigned long)&nes_ram[NES_SPRITES_BASE];
    for (i = 0u; i < NES_OAM_SLOT_COUNT; ++i) {
        const unsigned long rec = oam[i];
        unsigned char y = (unsigned char)(rec >> 24);

        /* Y == $F0 = NES hide-sprite convention. Skip. All-zero record =
         * unused OAM slot (NES Z1 leaves untouched OAM bytes at 0). */
        if (y == 0xF0u || rec == 0u) {
            continue;
        }
        unsigned char tile  = (unsigned char)(rec >> 16);
        unsigned char attrs = (unsigned char)(rec >> 8);
        unsigned char x     = (unsigned char)rec;

        if ((rec & 0x00FFFFFFUL) == 0x001C2000UL) {
            continue;                    /* blank priority sprite */
        }

        unsigned short sat_attrs  = xlat_sat(tile, attrs);
        /* NES Z1 uses 8x16 sprite mode (PPUCTRL bit 5 = 1). Each NES
         * sprite = 2 vertically-stacked CHR tiles. Genesis SPRITE_SIZE
         * (1, 2) = 1 column wide, 2 rows tall = 8x16. */
        unsigned short size = RENDER_SPRITE_SIZE(1, 2);

        /* NES OAM Y is absolute screen row (below NES HUD). Genesis
         * VDP screen is same coordinate system; SGDK applies its own
         * +128 offset internally on VDP_setSprite. NES OAM has the
         * standard +1 quirk (sprite Y is top - 1), so pass y as-is.
         * The HUD on Window plane covers rows 0..31 like the NES HUD
         * — no extra offset needed since NES OAM Y already accounts
         * for HUD region. */
        signed short gy = (signed short)(y + ROOMROM_PLAY_SPRITE_DY);
        signed short gx = (signed short)x;

        unsigned char link = (sat_slot < ENEMY_RENDER_SLOT_LAST)
                                 ? (unsigned char)(sat_slot + 1u) : 0u;
        render_set_sprite_inline((unsigned short)sat_slot, gx, gy,
                                 size, sat_attrs, link);
        ++sat_slot;
        if (sat_slot > ENEMY_RENDER_SLOT_LAST) break;
    }
    }

    sat_slot = emit_native_entries(sat_slot, NATIVE_PASS_OTHER, 0);

    /* 2026-05-15 perf fix: drop the up-to-54-slot pad loop. Write a
     * single terminator at the next slot with link=0, hiding it off-
     * screen. Genesis VDP sprite processing walks the link chain from
     * slot 0; once link=0 it stops scanning. Trailing SAT slots are
     * ignored regardless of their stale contents. Saves up to ~50
     * render_set_sprite_full calls per frame in sparse rooms (~3-5%
     * of frame budget on Tektite room). */
    if (sat_slot <= ENEMY_RENDER_SLOT_LAST) {
        render_set_sprite_inline((unsigned short)sat_slot,
                                 (signed short)-32, (signed short)-32,
                                 RENDER_SPRITE_SIZE(1, 1), 0u, 0u);
        /* The boss OAM path shares the bounded SAT upload with the native
         * path. Include every emitted part and the chain terminator. */
        g_enemy_render_last_sat_slot = (unsigned char)(sat_slot + 1u);
    } else {
        g_enemy_render_last_sat_slot = (unsigned char)sat_slot;
    }
    /* BUG2 FIX (2026-05-31): publish the last used SAT slot so main.c DMAs
     * EVERY sprite this boss sweep emitted. The native sweep sets this
     * (lines 829/831) but the boss-room OAM-shadow flush did not -> main.c
     * DMA'd only up to the stale native/init count (=ENEMY_FIRST) -> boss
     * slots beyond it were never uploaded to VRAM SAT -> Aquamentus showed
     * only its first sprite. +1 to include the terminator (matches native). */
    g_enemy_render_last_sat_slot = (unsigned char)(sat_slot + 1u);
}

/* 2026-05-15 Genesis-native enemy renderer.
 *
 * Replaces the per-frame iteration of 64 NES OAM entries (each
 * producing one Genesis SAT write) with a slot-keyed loop over the
 * up-to-11 alive enemies. Cost dropped from ~50 SAT writes/frame
 * to <= 11. Tile + attrs are latched into s_enemy_* by
 * anim_write_sprite_drained during enemy update; this function reads
 * the latch directly and emits one Genesis SIZE(1,2) SAT entry per
 * alive enemy. The OAM scatter still happens (cheap) for compat with
 * other consumers; enemy_render_sweep_oam_to_sat is retained for
 * fallback but no longer called from the gameplay tick. */
/* 2026-05-15 perf: published by native sweep so main.c can DMA only the
 * SAT entries actually used this frame (instead of all 64 H32 slots).
 * Initialized large enough for the boot fallback path; native sweep
 * updates each frame. */
unsigned char g_enemy_render_last_sat_slot = ROOMROM_SPRITE_SLOT_ENEMY_FIRST;

/* Phase B 2026-05-15 — per-ENEMY_TYPE size lookup. NES Z1 enemy types
 * are 1-byte ($00..$7F); table indexed by ENEMY_TYPE(slot). Default =
 * SIZE(2,2) (16x16) covers the vast majority. Overrides land here for
 * enemies whose visual width exceeds 16px (Aquamentus 24x16, etc.).
 *
 * Wider enemies (Gleeok body 32x32, Patra body w/ satellites) are
 * handled by the boss-room NES OAM sweep, not this table.
 *
 * Source for type IDs: src/game/enemies/enemy_loop.c per-type comments
 * + reference/aldonunez/Z_05.asm (Variables.inc enum). */
#define ENEMY_TYPE_SIZE_TABLE_LEN 0x80u

static const unsigned char k_enemy_type_size[ENEMY_TYPE_SIZE_TABLE_LEN] = {
    /* Aquamentus boss ($3D, Z_04.asm + boss_aquamentus): 24x16 wide
     * mouth + flanks. SIZE(3,2) renders 3 columns x 2 rows = 6 tiles
     * column-major from base tile $XX..$XX+5. */
    [0x3Du] = ((3u - 1u) << 2) | (2u - 1u),
    /* All other slots zero-initialized = sentinel "use default". */
};

static inline unsigned short enemy_type_to_size(unsigned char enemy_type)
{
    /* Default: SIZE(2,2). Encoding: (w-1)<<2 | (h-1) per RENDER_SPRITE_SIZE. */
    if (enemy_type < ENEMY_TYPE_SIZE_TABLE_LEN) {
        unsigned char e = k_enemy_type_size[enemy_type];
        if (e != 0u) return (unsigned short)e;
    }
    return RENDER_SPRITE_SIZE(2, 2);
}

extern unsigned char roomrom_is_scrolling(void);

void enemy_render_native_sweep(void)
{
    unsigned char captured_hand_sat = 0u;
    /* A captured hand temporarily takes the mask-to-Link SAT link below.
     * Restore the normal chain even if the next frame is a scroll or the
     * Wallmaster has finished its trip. Otherwise Link stays skipped after
     * the mode-3 return to the dungeon entrance. */
    g_render_sat_cache[ROOMROM_SPRITE_SLOT_MASK_S].link =
        ROOMROM_SPRITE_SLOT_LINK;
    s_boss_bank_active = level_chr_boss_is_ready();
    unsigned short sat_slot = ROOMROM_SPRITE_SLOT_ENEMY_FIRST;

    /* $07FE belongs to native UW progress; rendering must not mutate it. */

    /* 2026-05-22 — hide enemies during room scroll transition.
     * Without this, scroll-completion fires enemy_loop_room_init
     * which respawns enemies at NES spawn-list positions; user
     * sees them snap from old to new pos in one frame ("flying").
     * NES Z1 handles via sprite priority + door overlays during
     * scroll. Park enemy SAT slots off-screen + break chain. */
    if (roomrom_is_scrolling()) {
        render_set_sprite_inline((unsigned short)sat_slot,
                                 (signed short)-32, (signed short)-32,
                                 RENDER_SPRITE_SIZE(1, 1), 0u, 0u);
        g_enemy_render_last_sat_slot = (unsigned char)(sat_slot + 1u);
        return;
    }


    sat_slot = emit_native_entries(sat_slot, NATIVE_PASS_ALL,
                                   &captured_hand_sat);

    /* Terminator: hide remaining SAT slots via chain break (link=0). */
    if (sat_slot <= ENEMY_RENDER_SLOT_LAST) {
        render_set_sprite_inline((unsigned short)sat_slot,
                                 (signed short)-32, (signed short)-32,
                                 RENDER_SPRITE_SIZE(1, 1), 0u, 0u);
        /* Publish: DMA needs to include the terminator slot. */
        g_enemy_render_last_sat_slot = (unsigned char)(sat_slot + 1u);
    } else {
        g_enemy_render_last_sat_slot = (unsigned char)sat_slot;
    }

    /* NES DrawObjectNotMirroredOverLink writes the captured hand into OAM
     * slots 16/17, ahead of Link's 18/19. The Genesis SAT normally puts
     * all enemies after Link. Relink this one pair immediately before
     * Link; the hand then covers him while transparent pixels still show
     * his carried pose. Other enemy order stays intact. */
    if (captured_hand_sat >= ROOMROM_SPRITE_SLOT_ENEMY_FIRST &&
        (unsigned int)captured_hand_sat + 1u < sat_slot) {
        g_render_sat_cache[captured_hand_sat - 1u].link =
            (unsigned char)(captured_hand_sat + 2u);
        g_render_sat_cache[ROOMROM_SPRITE_SLOT_MASK_S].link =
            captured_hand_sat;
        g_render_sat_cache[captured_hand_sat + 1u].link =
            ROOMROM_SPRITE_SLOT_LINK;
    }

    /* Clear per-slot entry counts for next frame; entries arrays stay
     * populated (overwritten as new anim_write calls append). */
    {
        unsigned char i;
        for (i = 0u; i <= ENEMY_LOOP_SLOT_LAST; ++i) {
            s_enemy_count[i] = 0u;
        }
    }
}
