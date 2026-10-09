/* draw_dispatch.c — native sprite-descriptor / OAM draw pipeline
 * (Phase 4).
 *
 * Drain MATCH for the NES asm side; written from
 * reference/aldonunez/Z_01.asm:1958-2477 directly, since there's no
 * existing C drain (the chain only exists as transpiled M68K asm).
 *
 * NES sources (Z_01.asm):
 *   DrawObjectMirrored / DrawObjectNotMirrored (2058 / 2069)
 *   DrawObjectWithType (2094)
 *   DrawObjectWithAnim (2112)
 *   DrawObjectWithAnimAndSpecificSprites (2135)
 *   Anim_WriteHorizontallyFlippableSpritePair (2224)
 *   Anim_WriteMirroredSpritePair (2471)
 *   Anim_WriteSpritePair (2261)
 *   Anim_WriteSpritePairNotFlashing (2284)
 *
 * Tables baked inline (Z_01.asm:1958..2041):
 *   ObjAnimations[127], ObjAnimFrameHeap[228],
 *   ObjAnimAttrHeap[228], SpriteOffsets[41].
 */

#include "draw_dispatch.h"
#include <stdint.h>
#include "platform_abi.h"
#include "scratch_state.h"     /* ZP_TMP0..ZP_TMPF */
#include "object_state.h"      /* OBJ_TYPE */
#include "combat_state.h"      /* MON_STATUS_FLAGS, MON_HIT_REACTION */
#include "progress_state.h"    /* FRAME_COUNTER */
#include "core/core_dispatch.h"      /* core_anim_set_sprite_desc_attrs */
#include "world/sprite_dispatch.h"   /* sprite_cycle_cur_sprite_index,
                                      * sprite_anim_fetch_obj_pos */
#include "../options/options_consumer.h"  /* options_consumer_get_no_reduced_flashing */
#include "../enemies/enemy_render.h"      /* Phase A: enemy_render_publish_pair_left */

/* --------------------------------------------------------------- */
/* Zero-page scratch slot semantic aliases for the draw pipeline.   */
/* These are the exact NES ZP slots the asm uses.                  */
/* --------------------------------------------------------------- */
#define DRAW_X              ZP_TMP0   /* $00 = sprite X */
#define DRAW_Y              ZP_TMP1   /* $01 = sprite Y */
#define DRAW_LEFT_TILE      ZP_TMP2   /* $02 = left tile */
#define DRAW_RIGHT_TILE     ZP_TMP3   /* $03 = right tile */
#define DRAW_LEFT_ATTR      ZP_TMP4   /* $04 = left attrs */
#define DRAW_RIGHT_ATTR     ZP_TMP5   /* $05 = right attrs */
#define DRAW_HAS_TWO_SIDES  ZP_TMP7   /* $07 = has two sides (1 = yes) */
#define DRAW_OBJ_INDEX      ZP_TMP8   /* $08 = slot/object index */
#define DRAW_X_SEPARATION   ZP_TMPA   /* $0A = X separation */
#define DRAW_MIRRORED       ZP_TMPC   /* $0C = mirrored flag */
#define DRAW_FRAME          ZP_TMPD   /* $0D = frame */
#define DRAW_ANIM_INDEX     ZP_TMPE   /* $0E = animation index */
#define DRAW_FLIP_H         ZP_TMPF   /* $0F = horizontal flip flag */

/* OAM mirror + sprite-index registers (NES RAM). */
#define DRAW_CUR_SPRITE_INDEX     RAM(0x0341u)
#define DRAW_LEFT_SPRITE_OFFSET   RAM(0x0343u)
#define DRAW_RIGHT_SPRITE_OFFSET  RAM(0x0344u)
#define DRAW_OAM_X(off)           RAM(0x0203u + (unsigned short)(off))
#define DRAW_OAM_Y(off)           RAM(0x0200u + (unsigned short)(off))
#define DRAW_OAM_TILE(off)        RAM(0x0201u + (unsigned short)(off))
#define DRAW_OAM_ATTR(off)        RAM(0x0202u + (unsigned short)(off))

/* MON_HIT_REACTION lives at $04F0+slot in the asm (`$04F0,A4,D2.W`).
 * combat_state.h's MON_HIT_REACTION(slot) macro resolves to the same
 * RAM offset; use it directly. */

/* --------------------------------------------------------------- */
/* Tables — extracted from Z_01.asm:1958..2041.                    */
/* --------------------------------------------------------------- */

/* ObjAnimations[127] (Z_01.asm:1958). Indexes ObjAnimFrameHeap +
 * ObjAnimAttrHeap. */
static const unsigned char k_obj_animations[127] = {
    0x00u, 0x08u, 0x0Bu, 0x0Fu, 0x13u, 0x17u, 0x5Cu, 0x60u,
    0x1Bu, 0x1Bu, 0x21u, 0x21u, 0x64u, 0x6Au, 0x27u, 0x29u,
    0x2Bu, 0x35u, 0x3Fu, 0x70u, 0x74u, 0x76u, 0x76u, 0x78u,
    0x7Au, 0x7Eu, 0x80u, 0x49u, 0x82u, 0x84u, 0x86u, 0x4Bu,
    0x4Fu, 0x4Fu, 0x51u, 0x51u, 0x88u, 0x8Cu, 0x90u, 0x90u,
    0x92u, 0x94u, 0x96u, 0x98u, 0x99u, 0x99u, 0x99u, 0x53u,
    0x54u, 0x9Au, 0x9Bu, 0x9Bu, 0xA5u, 0xA5u, 0xABu, 0xABu,
    0xACu, 0xAEu, 0xAEu, 0xAFu, 0xAFu, 0xB2u, 0xB8u, 0xB8u,
    0x08u, 0x08u, 0xC6u, 0xC6u, 0xC6u, 0xC6u, 0xC6u, 0xC6u,
    0xC8u, 0xC8u, 0xC9u, 0xC9u, 0xCAu, 0xCAu, 0xCAu, 0xCAu,
    0xCAu, 0xCAu, 0xCAu, 0xCAu, 0x09u, 0x09u, 0x0Au, 0x0Au,
    0x0Bu, 0x0Bu, 0x0Bu, 0x0Bu, 0x0Bu, 0x0Bu, 0xCBu, 0x55u,
    0x55u, 0x55u, 0x55u, 0x55u, 0x55u, 0x55u, 0x56u, 0x57u,
    0x57u, 0xCBu, 0xCCu, 0x58u, 0x58u, 0x58u, 0x58u, 0x58u,
    0x58u, 0x58u, 0x58u, 0x58u, 0x59u, 0x59u, 0x59u, 0x59u,
    0x5Au, 0x5Au, 0x5Au, 0x5Au, 0x5Bu, 0x5Bu, 0x5Bu
};

/* ObjAnimFrameHeap[228] (Z_01.asm:1977). Per-frame left tile index. */
static const unsigned char k_obj_anim_frame_heap[228] = {
    0x00u, 0x04u, 0x08u, 0x0Cu, 0x10u, 0x10u, 0x14u, 0x18u,
    0x5Cu, 0x9Eu, 0x44u, 0xCEu, 0xD2u, 0xD6u, 0xDAu, 0xCEu,
    0xD2u, 0xD6u, 0xDAu, 0xF0u, 0xF4u, 0xF8u, 0xFCu, 0xF0u,
    0xF4u, 0xF8u, 0xFCu, 0xB4u, 0xB0u, 0xB0u, 0xB8u, 0xB2u,
    0xB2u, 0xB4u, 0xB0u, 0xB0u, 0xB8u, 0xB2u, 0xB2u, 0xCAu,
    0xCCu, 0xCAu, 0xCCu, 0xBCu, 0xBEu, 0xC0u, 0xC0u, 0xC2u,
    0xC4u, 0xC0u, 0xC0u, 0xBCu, 0xBEu, 0xBCu, 0xBEu, 0xC0u,
    0xC0u, 0xC2u, 0xC4u, 0xC0u, 0xC0u, 0xBCu, 0xBEu, 0xBCu,
    0xBEu, 0xECu, 0xEEu, 0xECu, 0xEEu, 0xECu, 0xEEu, 0xBCu,
    0xBEu, 0xC6u, 0xC8u, 0xA0u, 0xA8u, 0xA4u, 0xACu, 0x90u,
    0xE8u, 0xE4u, 0xE0u, 0x94u, 0xF3u, 0xC9u, 0xBDu, 0xC1u,
    0x98u, 0x9Au, 0x9Cu, 0xF8u, 0xB8u, 0xBCu, 0xB0u, 0xB4u,
    0xB8u, 0xBCu, 0xB0u, 0xB4u, 0xB8u, 0xACu, 0xB4u, 0xBCu,
    0xB0u, 0xB4u, 0xB8u, 0xACu, 0xB4u, 0xBCu, 0xB0u, 0xB4u,
    0xACu, 0xAEu, 0xB0u, 0xB2u, 0xA8u, 0xAAu, 0x92u, 0x94u,
    0xA0u, 0xA2u, 0xA6u, 0xA4u, 0xA2u, 0xA4u, 0xD8u, 0xDAu,
    0x00u, 0x00u, 0x9Au, 0x9Cu, 0x9Au, 0x9Cu, 0x9Au, 0x9Cu,
    0xB4u, 0xB8u, 0xBCu, 0xBEu, 0xB4u, 0xB8u, 0xBCu, 0xBEu,
    0xFCu, 0xFEu, 0xACu, 0x9Cu, 0xA0u, 0xA4u, 0xA0u, 0xA4u,
    0xA8u, 0x8Eu, 0xA4u, 0xDCu, 0xE0u, 0xE4u, 0xE8u, 0xECu,
    0xF0u, 0xF4u, 0xF8u, 0xFAu, 0xFEu, 0xF4u, 0xF6u, 0xFEu,
    0xFCu, 0xF0u, 0xF8u, 0xB0u, 0xF6u, 0xF0u, 0xD4u, 0xFCu,
    0xFEu, 0xF8u, 0xE8u, 0xEAu, 0xE0u, 0xE4u, 0xECu, 0xECu,
    0xD0u, 0xD4u, 0xD8u, 0xDCu, 0xE0u, 0xE4u, 0xC0u, 0xC8u,
    0xC4u, 0xCCu, 0xE8u, 0xEAu, 0x72u, 0x74u, 0xDEu, 0xEEu,
    0xF8u, 0x96u, 0x98u, 0xB1u
};

/* ObjAnimAttrHeap[228] (Z_01.asm:2006). Per-frame attr byte (palette
 * row + flip flags). */
static const unsigned char k_obj_anim_attr_heap[228] = {
    0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
    0x02u, 0x00u, 0x00u, 0x01u, 0x01u, 0x01u, 0x01u, 0x02u,
    0x02u, 0x02u, 0x02u, 0x03u, 0x03u, 0x03u, 0x03u, 0x02u,
    0x02u, 0x02u, 0x02u, 0x02u, 0x82u, 0x02u, 0x02u, 0x82u,
    0x02u, 0x01u, 0x81u, 0x01u, 0x01u, 0x81u, 0x01u, 0x01u,
    0x01u, 0x02u, 0x02u, 0x02u, 0x02u, 0x01u, 0x01u, 0x01u,
    0x01u, 0x01u, 0x01u, 0x02u, 0x02u, 0x02u, 0x02u, 0x02u,
    0x02u, 0x02u, 0x02u, 0x02u, 0x02u, 0x02u, 0x02u, 0x03u,
    0x03u, 0x03u, 0x03u, 0x03u, 0x03u, 0x03u, 0x03u, 0x03u,
    0x03u, 0x02u, 0x02u, 0x02u, 0x02u, 0x02u, 0x02u, 0x02u,
    0x02u, 0x01u, 0x01u, 0x01u, 0x02u, 0x03u, 0x03u, 0x03u,
    0x02u, 0x02u, 0x00u, 0x02u, 0x01u, 0x01u, 0x01u, 0x01u,
    0x02u, 0x02u, 0x02u, 0x02u, 0x02u, 0x02u, 0x02u, 0x02u,
    0x02u, 0x02u, 0x01u, 0x01u, 0x01u, 0x01u, 0x01u, 0x01u,
    0x01u, 0x01u, 0x01u, 0x01u, 0x03u, 0x03u, 0x03u, 0x03u,
    0x00u, 0x00u, 0x02u, 0x02u, 0x02u, 0x02u, 0x03u, 0x03u,
    0x03u, 0x03u, 0x01u, 0x01u, 0x02u, 0x02u, 0x03u, 0x03u,
    0x01u, 0x01u, 0x01u, 0x01u, 0x02u, 0x02u, 0x02u, 0x02u,
    0x01u, 0x01u, 0x01u, 0x01u, 0x02u, 0x02u, 0x02u, 0x02u,
    0x02u, 0x02u, 0x01u, 0x03u, 0x03u, 0x03u, 0x03u, 0x03u,
    0x03u, 0x03u, 0x03u, 0x03u, 0x03u, 0x01u, 0x01u, 0x01u,
    0x01u, 0x01u, 0x01u, 0x02u, 0x00u, 0x00u, 0x03u, 0x01u,
    0x01u, 0x01u, 0x01u, 0x01u, 0x01u, 0x01u, 0x01u, 0x01u,
    0x03u, 0x03u, 0x03u, 0x03u, 0x03u, 0x03u, 0x03u, 0x03u,
    0x03u, 0x03u, 0x03u, 0x03u, 0x03u, 0x03u, 0x02u, 0x02u,
    0x01u, 0x01u, 0x02u, 0x03u
};

/* Anim_ItemFrameOffsets[37] (Z_01.asm:2320). Indexed by item slot;
 * produces offset into k_anim_item_frame_tiles for first frame. */
static const unsigned char k_anim_item_frame_offsets[37] = {
    0x00u, 0x03u, 0x07u, 0x0Au, 0x0Bu, 0x0Cu, 0x0Du, 0x0Eu,
    0x0Fu, 0x11u, 0x12u, 0x13u, 0x14u, 0x15u, 0x16u, 0x17u,
    0x18u, 0x17u, 0x18u, 0x17u, 0x19u, 0x1Bu, 0x1Cu, 0x1Du,
    0x1Eu, 0x1Fu, 0x20u, 0x21u, 0x1Cu, 0x22u, 0x22u, 0x26u,
    0x27u, 0x28u, 0x29u, 0x2Bu, 0x2Eu
};

/* Anim_ItemFrameTiles[48] (Z_01.asm:2329). Per-item left tile. */
static const unsigned char k_anim_item_frame_tiles[48] = {
    0x20u, 0x82u, 0x3Cu, 0x34u, 0x70u, 0x72u, 0x74u, 0x28u,
    0x86u, 0x3Cu, 0x2Au, 0x26u, 0x24u, 0x22u, 0x40u, 0x4Au,
    0x8Au, 0x6Cu, 0x42u, 0x46u, 0x76u, 0x2Cu, 0x4Eu, 0x4Cu,
    0x6Au, 0x50u, 0x52u, 0x66u, 0x32u, 0x2Eu, 0x68u, 0xF3u,
    0x6Eu, 0xF2u, 0x36u, 0x38u, 0x3Au, 0x3Cu, 0x56u, 0x48u,
    0x78u, 0x20u, 0x82u, 0x7Au, 0x7Cu, 0x30u, 0x64u, 0x62u
};

/* ItemIdToSlot[36] (Z_01.asm:1811). Maps item id -> item slot for
 * AnimateItemObject. */
static const unsigned char k_item_id_to_slot[36] = {
    0x01u, 0x00u, 0x00u, 0x00u, 0x06u, 0x05u, 0x04u, 0x04u,
    0x02u, 0x02u, 0x03u, 0x0Du, 0x09u, 0x0Cu, 0x1Bu, 0x1Cu,
    0x08u, 0x0Au, 0x0Bu, 0x0Bu, 0x0Eu, 0x0Fu, 0x10u, 0x11u,
    0x16u, 0x17u, 0x18u, 0x1Au, 0x1Fu, 0x1Du, 0x1Eu, 0x07u,
    0x07u, 0x15u, 0x19u, 0x14u
};

/* ItemIdToDescriptor[36] (Z_01.asm:1835). High nibble = item type
 * (0=individual, 1=amount, 2=grade, 3=error); low nibble = value. */
static const unsigned char k_item_id_to_descriptor[36] = {
    0x14u, 0x21u, 0x22u, 0x23u, 0x01u, 0x01u, 0x21u, 0x22u,
    0x21u, 0x22u, 0x01u, 0x01u, 0x01u, 0x01u, 0x01u, 0x15u,
    0x01u, 0x01u, 0x21u, 0x22u, 0x01u, 0x01u, 0x01u, 0x01u,
    0x11u, 0x11u, 0x10u, 0x01u, 0x01u, 0x01u, 0x01u, 0x11u,
    0x22u, 0x01u, 0x10u, 0x12u
};

/* ItemSlotToPaletteOffsetsOrValues[32] (Z_01.asm:1852). Per-slot
 * sprite-attribute lookup: for most items the attribute value;
 * for slots $00/$04/$02/$07/$0B it's an offset added to the item
 * value in TMP4. */
static const unsigned char k_item_slot_to_palette_offsets_or_values[32] = {
    0xFFu, 0x01u, 0xFFu, 0x00u, 0x00u, 0x02u, 0x02u, 0x00u,
    0x01u, 0x00u, 0x02u, 0x00u, 0x00u, 0x02u, 0x02u, 0x01u,
    0x02u, 0x02u, 0x02u, 0x02u, 0x02u, 0x02u, 0x02u, 0x02u,
    0x02u, 0x02u, 0x02u, 0x02u, 0x01u, 0x00u, 0x01u, 0x00u
};

/* SpriteOffsets[41] (Z_01.asm:2035). Indexed by CUR_SPRITE_INDEX
 * ($0341); produces left+right OAM byte offsets.
 *
 * Externally visible via draw_sprite_offset_at() so the wallmaster
 * bridge can patch left/right tile bytes after DrawObjectNotMirrored
 * (Z_04.asm:4356-4383, the "fix Keese-tile bug" path). */
const unsigned char k_sprite_offsets[41] = {
    0x60u, 0xBCu, 0x64u, 0xB8u, 0x68u, 0xB4u, 0x6Cu, 0xB0u,
    0x70u, 0xCCu, 0x74u, 0xC8u, 0x78u, 0xC4u, 0x7Cu, 0xC0u,
    0x80u, 0xDCu, 0x84u, 0xD8u, 0x88u, 0xD4u, 0x8Cu, 0xD0u,
    0x90u, 0xECu, 0x94u, 0xE8u, 0x98u, 0xE4u, 0x9Cu, 0xE0u,
    0xA0u, 0xFCu, 0xA4u, 0xF8u, 0xA8u, 0xF4u, 0xACu, 0xF0u,
    0x60u
};

/* --------------------------------------------------------------- */
/* Forward decls.                                                  */
/* --------------------------------------------------------------- */
static void anim_write_sprite_pair_not_flashing(void);
static void anim_write_sprite_pair(unsigned int slot);
static void anim_write_horizontally_flippable_sprite_pair(unsigned int slot);
static void anim_write_mirrored_sprite_pair(unsigned int slot);
static void draw_object_with_anim_and_specific_sprites(unsigned int slot);
static void draw_object_with_anim(unsigned char frame, unsigned int slot);

/* 2026-05-22 — item-context flag for ITEM_ATTR_MARKER routing. */
extern unsigned char g_draw_in_item_context;
static void draw_object_with_type(unsigned char frame, unsigned int slot,
                                  unsigned char anim_idx);

/* --------------------------------------------------------------- */
/* Anim_WriteSpritePairNotFlashing (Z_01.asm:2284).                 */
/* Writes 2 sprites to OAM mirror at $0200..$02FF.                  */
/*                                                                  */
/* 2026-05-17 perf: enemy slots (DRAW_OBJ_INDEX in 1..11) skip the  */
/* 4 OAM byte writes per sprite — enemy_render_native_sweep reads   */
/* the side-channel cache populated by enemy_render_publish_pair_   */
/* left, and no other gameplay-tick consumer reads NES OAM mirror   */
/* $0200..$02FF for enemy slots. Slot 0 (Link) still writes because */
/* sprite_dispatch.c:152 / sprite_runtime.c:56 read OAM slot 18+19  */
/* for the show-Link-behind-horizontal-doors logic; those slots are */
/* populated by Link's draw path. Saves ~8 RAM writes × 11 enemies  */
/* × ~12 cyc = ~1000 cyc/frame on busy rooms.                       */
/* --------------------------------------------------------------- */
static void anim_write_sprite_pair_not_flashing(void)
{
    unsigned char off = (unsigned char)DRAW_LEFT_SPRITE_OFFSET;
    unsigned char d3  = 0u;     /* loop counter (left=0, right=1) */
    const unsigned char obj_idx_initial = (unsigned char)DRAW_OBJ_INDEX;
    /* NES source: Z_04.asm:Ganon_DrawBody/Ganon_DrawCloud/Ganon_DrawAshes.
     * Drained C: enemy_ganon_runtime.c and existing native pair publisher.
     * Coverage: PARTIAL (T-183 duplicate body/cloud/ashes publication).
     * Stance: EXTEND the native owner, whose cache holds 20 parts per slot.
     * Ganon's eight body parts or eighteen ashes/cloud parts fit that
     * cache. Only Link needs the OAM mirror for doorway-priority readers;
     * Ganon's room now consumes the native submissions once. */
    const unsigned char write_oam = (obj_idx_initial == 0u);

    /* NES source: Anim_WriteSpritePairNotFlashing, narrow/wide descriptor.
     * Drained C: the generic loop below and existing pair publisher.
     * Coverage: native single/pair enemies (Patra children are narrow).
     * Stance: EXTEND; publish final scratch/cursor state without a loop. */
    if (!write_oam && obj_idx_initial <= 11u &&
        (unsigned char)DRAW_HAS_TWO_SIDES <= 1u) {
        const unsigned char x = (unsigned char)DRAW_X;
        const unsigned char y = (unsigned char)DRAW_Y;
        const unsigned char sep = (unsigned char)DRAW_X_SEPARATION;
        const unsigned char right_x = (unsigned char)(x + sep);
        const unsigned char marker = g_draw_in_item_context ? 0x08u : 0u;
        if ((unsigned char)DRAW_HAS_TWO_SIDES == 0u) {
            enemy_render_publish_pair_left(
                (unsigned char)DRAW_LEFT_TILE,
                (unsigned char)((unsigned char)DRAW_LEFT_ATTR | marker), x, y);
            DRAW_X = right_x;
            sprite_cycle_cur_sprite_index();
        } else {
            enemy_render_publish_native_pair(
                (unsigned char)DRAW_LEFT_TILE,
                (unsigned char)((unsigned char)DRAW_LEFT_ATTR | marker),
                (unsigned char)DRAW_RIGHT_TILE,
                (unsigned char)((unsigned char)DRAW_RIGHT_ATTR | marker),
                x, right_x, y);
            DRAW_X = (uint8_t)(right_x + sep);
            sprite_cycle_cur_sprite_index_twice();
        }
        DRAW_HAS_TWO_SIDES = 0xFFu;
        return;
    }

    do {
        const unsigned char tile =
            (unsigned char)RAM(0x0002u + d3); /* TMP2/3 */
        const unsigned char y = (unsigned char)DRAW_Y;
        unsigned char x = (unsigned char)DRAW_X;
        const unsigned char xsep = (unsigned char)DRAW_X_SEPARATION;
        const unsigned char attr =
            (unsigned char)RAM(0x0004u + d3); /* TMP4/5 */

        if (write_oam) {
            DRAW_OAM_TILE(off) = tile;
            DRAW_OAM_Y(off)    = y;
            DRAW_OAM_X(off)    = x;
            DRAW_OAM_ATTR(off) = attr;
        }

        DRAW_X = (uint8_t)(x + xsep);   /* TMP0 += xsep for next sprite */

        /* Phase A/E cache feeder. Native draw_dispatch path writes OAM
         * directly above; the enemy renderer cache is normally fed by
         * anim_write_sprite_drained (oracle-only). Publish BOTH halves
         * (LEFT d3=0 + RIGHT d3=1) into the multi-latch cache so
         * natively-dispatched enemies render via enemy_render_native_sweep
         * with full 1:1 NES OAM mapping. Per-tile h_flip preserved via
         * per-entry attrs storage.
         *
         * 2026-05-22 — OR ITEM_ATTR_MARKER ($08) into attr when we're
         * in item-render context. translate_tile in enemy_render.c
         * detects + routes the tile to ITEM atlas via lookup. */
        unsigned char publish_attr = attr;
        if (g_draw_in_item_context) publish_attr |= 0x08u;
        enemy_render_publish_pair_left(tile, publish_attr, x, y);

        off = (unsigned char)DRAW_RIGHT_SPRITE_OFFSET;

        if (obj_idx_initial != 0u) {
            sprite_cycle_cur_sprite_index();
        }

        ++d3;
        DRAW_HAS_TWO_SIDES =
            (uint8_t)((unsigned char)DRAW_HAS_TWO_SIDES - 1u);
    } while ((signed char)(unsigned char)DRAW_HAS_TWO_SIDES >= 0);
}

/* Anim_WriteSpritePair (Z_01.asm:2261). Apply hit-flash palette
 * override if MON_HIT_REACTION(slot) is non-zero, then call
 * not-flashing writer. */
static void anim_write_sprite_pair(unsigned int slot)
{
    const unsigned char hit = (unsigned char)MON_HIT_REACTION(slot);
    if (hit != 0u) {
        for (signed char d3 = 1; d3 >= 0; --d3) {
            unsigned char attr =
                (unsigned char)RAM(0x0004u + (unsigned char)d3);
            attr = (unsigned char)(attr & 0xFCu);
            attr = (unsigned char)(attr | (hit & 0x03u));
            RAM(0x0004u + (unsigned char)d3) = attr;
        }
    }
    anim_write_sprite_pair_not_flashing();
}

/* Anim_WriteHorizontallyFlippableSpritePair (Z_01.asm:2224). If
 * DRAW_FLIP_H is set, swap tiles + toggle attr bit 6 on both sides
 * before writing. */
static void anim_write_horizontally_flippable_sprite_pair(unsigned int slot)
{
    if ((unsigned char)DRAW_FLIP_H != 0u) {
        const unsigned char tmp_left = (unsigned char)DRAW_LEFT_TILE;
        DRAW_LEFT_TILE = (uint8_t)DRAW_RIGHT_TILE;
        DRAW_RIGHT_TILE = tmp_left;

        DRAW_LEFT_ATTR =
            (uint8_t)((unsigned char)DRAW_LEFT_ATTR ^ 0x40u);
        DRAW_RIGHT_ATTR =
            (uint8_t)((unsigned char)DRAW_RIGHT_ATTR ^ 0x40u);
    }
    anim_write_sprite_pair(slot);
}

/* Anim_WriteMirroredSpritePair (Z_01.asm:2471). Set right tile =
 * left tile, toggle right attr flip-H bit, then write. */
static void anim_write_mirrored_sprite_pair(unsigned int slot)
{
    DRAW_RIGHT_TILE = (uint8_t)DRAW_LEFT_TILE;
    DRAW_RIGHT_ATTR =
        (uint8_t)((unsigned char)DRAW_RIGHT_ATTR ^ 0x40u);
    anim_write_sprite_pair(slot);
}

/* DrawObjectWithAnimAndSpecificSprites (Z_01.asm:2135).
 * Looks up frame tiles + attrs from heaps, sets sprite descriptors,
 * dispatches to write writer. */
static void draw_object_with_anim_and_specific_sprites(unsigned int slot)
{
    DRAW_OBJ_INDEX = (uint8_t)slot;
    DRAW_HAS_TWO_SIDES = 1u;
    DRAW_X_SEPARATION = 8u;

    const unsigned char anim_idx = (unsigned char)DRAW_ANIM_INDEX;
    const unsigned char frame = (unsigned char)DRAW_FRAME;

    /* d0 = ObjAnimations[anim_idx] + frame ; d3 = d0; left tile */
    unsigned char tile_idx =
        (unsigned char)(k_obj_animations[anim_idx] + frame);
    const unsigned char left_tile = k_obj_anim_frame_heap[tile_idx];
    DRAW_LEFT_TILE = left_tile;
    DRAW_RIGHT_TILE = (uint8_t)(left_tile + 2u);

    /* If object is Link or weapon/room-item slot >= $D, force
     * UseTableAttr (skip the half-width / ignore-attr branches). */
    int use_table_attr = 0;
    if (slot == 0u || slot >= 0x0Du) {
        use_table_attr = 1;
    } else {
        const unsigned char status =
            (unsigned char)MON_STATUS_FLAGS(slot);
        if (status & 0x02u) {
            /* Half-width draw branch: -- DRAW_HAS_TWO_SIDES; jmp
             * Anim_WriteSpritePair (skip attr setup). */
            DRAW_HAS_TWO_SIDES =
                (uint8_t)((unsigned char)DRAW_HAS_TWO_SIDES - 1u);
            anim_write_sprite_pair(slot);
            return;
        }
        if (status & 0x08u) {
            /* Ignore sprite attribute table — drop straight to
             * mirror/flip dispatch. */
            goto dispatch_mirror_flip;
        }
        use_table_attr = 1;
    }

    if (use_table_attr) {
        const unsigned char attr_byte = k_obj_anim_attr_heap[tile_idx];
        core_anim_set_sprite_desc_attrs((unsigned int)attr_byte);
    }

dispatch_mirror_flip:
    /* If Link (slot 0), always horizontally-flippable (never
     * mirrored). Else respect DRAW_MIRRORED. */
    if (slot == 0u) {
        anim_write_horizontally_flippable_sprite_pair(slot);
        return;
    }
    if ((unsigned char)DRAW_MIRRORED != 0u) {
        anim_write_mirrored_sprite_pair(slot);
        return;
    }
    anim_write_horizontally_flippable_sprite_pair(slot);
}

/* DrawObjectWithAnim (Z_01.asm:2112). Sets DRAW_FRAME + DRAW_ANIM_INDEX +
 * DRAW_OBJ_INDEX, picks left/right SpriteOffset by CUR_SPRITE_INDEX,
 * Link-special-cases sprite offsets to fixed $48/$4C, then dispatches. */
static void draw_object_with_anim(unsigned char frame, unsigned int slot)
{
    DRAW_FRAME = frame;
    DRAW_ANIM_INDEX = (uint8_t)DRAW_ANIM_INDEX; /* unchanged */
    DRAW_OBJ_INDEX = (uint8_t)slot;

    const unsigned char cur_idx = (unsigned char)DRAW_CUR_SPRITE_INDEX;
    DRAW_LEFT_SPRITE_OFFSET = k_sprite_offsets[cur_idx & 0x3Fu];
    if (slot == 0u) {
        /* Link: hardcode $48 / $4C. */
        DRAW_LEFT_SPRITE_OFFSET = 0x48u;
        DRAW_RIGHT_SPRITE_OFFSET = 0x4Cu;
    } else {
        DRAW_RIGHT_SPRITE_OFFSET =
            k_sprite_offsets[(cur_idx + 1u) & 0x3Fu];
    }
    draw_object_with_anim_and_specific_sprites(slot);
}

/* DrawObjectWithType (Z_01.asm:2094). DRAW_ANIM_INDEX = obj_type + 1. */
static void draw_object_with_type(unsigned char frame, unsigned int slot,
                                  unsigned char anim_idx)
{
    DRAW_ANIM_INDEX = (uint8_t)(anim_idx + 1u);
    draw_object_with_anim(frame, slot);
}

/* DrawObjectMirrored (Z_01.asm:2058). mirrored=1; anim_idx = OBJ_TYPE.
 * NES uses RAM($034F + slot) — that's OBJ_TYPE(slot). */
void draw_object_mirrored(unsigned char frame, unsigned int slot)
{
    DRAW_MIRRORED = 1u;
    const unsigned char anim_idx = (unsigned char)OBJ_TYPE(slot);
    draw_object_with_type(frame, slot, anim_idx);
}

/* DrawObjectNotMirrored (Z_01.asm:2069). mirrored=0; anim_idx=OBJ_TYPE. */
void draw_object_not_mirrored(unsigned char frame, unsigned int slot)
{
    DRAW_MIRRORED = 0u;
    /* NES DrawObjectNotMirrored deliberately preserves $0F: callers such
     * as UpdateOctorock set it immediately before this call to face right.
     * Clearing it here made right-moving Octoroks render left-facing and
     * appear to walk backward. Static callers must clear their own flag. */
    const unsigned char anim_idx = (unsigned char)OBJ_TYPE(slot);
    draw_object_with_type(frame, slot, anim_idx);
}

/* DrawObjectMirroredWithFrame (Z_01.asm:4520-ish). Same as mirrored
 * but caller supplies frame. */
void draw_object_mirrored_with_frame(unsigned char frame, unsigned int slot)
{
    draw_object_mirrored(frame, slot);
}

void draw_object_not_mirrored_with_frame(unsigned char frame,
                                         unsigned int slot)
{
    draw_object_not_mirrored(frame, slot);
}

/* DrawObjectMirroredOverLink (Z_04.asm:763). Same shape as
 * DrawObjectMirrored, but bypasses the rolling sprite cursor and
 * hardcodes LeftSpriteOffset=$40 / RightSpriteOffset=$44 — sprites
 * $10 and $11 in the OAM mirror. Like-Like uses this when capturing
 * Link so its body draws on top of him. anim_idx = ObjType+1 (matches
 * DrawObjectWithType branch). */
void draw_object_mirrored_over_link(unsigned char frame, unsigned int slot)
{
    DRAW_MIRRORED = 1u;
    DRAW_ANIM_INDEX = (uint8_t)((unsigned char)OBJ_TYPE(slot) + 1u);
    DRAW_FRAME = frame;
    DRAW_OBJ_INDEX = (uint8_t)slot;
    DRAW_LEFT_SPRITE_OFFSET = 0x40u;
    DRAW_RIGHT_SPRITE_OFFSET = 0x44u;
    draw_object_with_anim_and_specific_sprites(slot);
}

/* DrawObjectNotMirroredOverLink (Z_04.asm:756). Same as the mirrored
 * variant above but DRAW_MIRRORED=0 — used by Wallmaster when drawing
 * its hand on top of captured Link (the hand is asymmetric). */
void draw_object_not_mirrored_over_link(unsigned char frame, unsigned int slot)
{
    DRAW_MIRRORED = 0u;
    DRAW_ANIM_INDEX = (uint8_t)((unsigned char)OBJ_TYPE(slot) + 1u);
    DRAW_FRAME = frame;
    DRAW_OBJ_INDEX = (uint8_t)slot;
    DRAW_LEFT_SPRITE_OFFSET = 0x40u;
    DRAW_RIGHT_SPRITE_OFFSET = 0x44u;
    draw_object_with_anim_and_specific_sprites(slot);
}

/* --------------------------------------------------------------- */
/* Item-draw chain — Anim_WriteSpecificItemSprites + helpers.      */
/* Z_01.asm:2399-2477 + Z_07.asm AnimateItemObject + DrawItemBySlot.*/
/* --------------------------------------------------------------- */

/* RAM($0052) = ProcessedNarrowObj, $0504 = StatusBarItemDrawingFlag
 * (suppress narrow X-shift when set), $0657+slot = item value. */
#define DRAW_PROCESSED_NARROW_OBJ      RAM(0x0052u)
#define DRAW_STATUS_BAR_DRAW_FLAG      RAM(0x0504u)
#define DRAW_INVENTORY_ITEM(slot)      RAM(0x0657u + (unsigned char)(slot))
#define DRAW_ITEM_LIFETIME(slot)       RAM(0x03A8u + (unsigned char)(slot))

/* Anim_WriteSpecificItemSprites (Z_01.asm:2399). Computes left/right
 * tiles from item-frame tables, then dispatches narrow/wide/slim. */
static void anim_write_specific_item_sprites(unsigned int slot,
                                             unsigned int item_slot)
{
    DRAW_OBJ_INDEX = (uint8_t)slot;
    DRAW_HAS_TWO_SIDES = 1u;
    DRAW_X_SEPARATION = 8u;

    unsigned char tile_idx = k_anim_item_frame_offsets[item_slot & 0x3Fu];
    /* tile_idx + DRAW_MIRRORED ($0C); $0C is reused as "frame image"
     * in this chain (item draws don't use the mirrored flag). */
    tile_idx = (unsigned char)(tile_idx + (unsigned char)DRAW_MIRRORED);

    const unsigned char left_tile =
        k_anim_item_frame_tiles[tile_idx & 0x3Fu];
    DRAW_LEFT_TILE = left_tile;
    DRAW_RIGHT_TILE = (uint8_t)(left_tile + 2u);

    /* Narrow / wide / slim dispatch by left tile range. NES Z_01.asm:
     * 5279-5288 — narrow = ($F3) OR ($20 <= tile < $62). Previously
     * this condition was inverted (`== $F3 || < $20 || >= $62`),
     * sending all in-range tiles (e.g. arrow tile $28) to slim/wide
     * dispatch which mirrored them into 2 sprites = user-visible
     * "doubled" arrow with wrong dir. */
    if (left_tile == 0xF3u || (left_tile >= 0x20u && left_tile < 0x62u)) {
        /* Narrow: half-width object. If status-bar flag clear,
         * shift X by +4. */
        if ((unsigned char)DRAW_STATUS_BAR_DRAW_FLAG == 0u) {
            DRAW_X = (uint8_t)((unsigned char)DRAW_X + 4u);
        }
        DRAW_PROCESSED_NARROW_OBJ =
            (uint8_t)((unsigned char)DRAW_PROCESSED_NARROW_OBJ + 1u);
        DRAW_HAS_TWO_SIDES = 0u;
        anim_write_sprite_pair(slot);
        return;
    }
    if (left_tile < 0x6Cu) {
        /* Slim wide: X separation = 7, mirrored draw. */
        DRAW_X_SEPARATION = 7u;
        anim_write_mirrored_sprite_pair(slot);
        return;
    }
    if (left_tile < 0x7Cu) {
        anim_write_mirrored_sprite_pair(slot);
        return;
    }
    anim_write_horizontally_flippable_sprite_pair(slot);
}

/* Anim_WriteItemSprites (Z_01.asm:2365). Setup sprite offsets by
 * cur sprite index, then dispatch. */
/* 2026-05-22 — set during anim_write_item_sprites call. Read by
 * anim_write_sprite_pair_not_flashing to tag cache entries with
 * ITEM_ATTR_MARKER so enemy_render's translate_tile routes them to
 * ITEM atlas instead of common SPR (where NES tile IDs coincide
 * with enemy CHR data → user saw rocks instead of arrows). */
unsigned char g_draw_in_item_context = 0u;

/* NES Anim_WriteItemSprites. External linkage: the asm_equiv harness
 * replaces it with a logged stub to compare the callers' draw setup. */
void anim_write_item_sprites(unsigned int slot, unsigned int item_slot);
void anim_write_item_sprites(unsigned int slot,
                             unsigned int item_slot)
{
    DRAW_PROCESSED_NARROW_OBJ = 0u;
    const unsigned char cur_idx = (unsigned char)DRAW_CUR_SPRITE_INDEX;
    DRAW_LEFT_SPRITE_OFFSET = k_sprite_offsets[cur_idx & 0x3Fu];
    DRAW_RIGHT_SPRITE_OFFSET =
        k_sprite_offsets[(cur_idx + 1u) & 0x3Fu];
    g_draw_in_item_context = 1u;
    anim_write_specific_item_sprites(slot, item_slot);
    g_draw_in_item_context = 0u;
}

/* Anim_WriteStaticItemSpritesWithAttributes (Z_01.asm:2338).
 * Calls core_anim_set_sprite_desc_attrs with attrs (write TMP4/5),
 * clears DRAW_FLIP_H + DRAW_MIRRORED, then anim_write_item_sprites. */
static void anim_write_static_item_sprites_with_attributes(
    unsigned char attrs, unsigned int slot, unsigned int item_slot)
{
    (void)core_anim_set_sprite_desc_attrs((unsigned int)attrs);
    DRAW_FLIP_H = 0u;
    DRAW_MIRRORED = 0u;
    anim_write_item_sprites(slot, item_slot);
}

/* T-056: Anim_WriteStaticItemSpritesWithAttributes for callers that
 * staged [00]/[01] (Anim_FetchObjPosForSpriteDescriptor): ladder (item
 * slot $0C) and raft (item slot $09), both attribute 0. */
void draw_static_item_sprites(unsigned char attrs, unsigned int slot,
                              unsigned int item_slot)
{
    anim_write_static_item_sprites_with_attributes(attrs, slot, item_slot);
}

/* T-050: Anim_WriteItemSprites entry for callers that staged [00]/[01]
 * position, [04]/[05] attributes, [0C] frame and [0F] flip themselves
 * (DrawFairy). */
void draw_anim_write_item_sprites(unsigned int slot, unsigned int item_slot)
{
    anim_write_item_sprites(slot, item_slot);
}

/* NES Z_04.asm:11508 DrawFairy (pond fairy, dropped fairy item $23):
 * Anim_FetchObjPosForSpriteDescriptor, Anim_SetSpriteDescriptorRedPaletteRow
 * ([04]/[05] = 2), frame [0C] = ((2 << 1) & FrameCounter) >> 2 (flips
 * every 4 frames), item slot $14 written at the object's own slot. */
void draw_fairy(unsigned int slot)
{
    RAM(0x0000u) = RAM(0x0070u + slot);
    RAM(0x0001u) = RAM(0x0084u + slot);
    RAM(0x000Fu) = 0u;
    RAM(0x0004u) = 0x02u;
    RAM(0x0005u) = 0x02u;
    RAM(0x000Cu) = (uint8_t)((((unsigned char)(0x02u << 1)) &
                              (unsigned char)FRAME_COUNTER) >> 2);
    anim_write_item_sprites(slot, 0x14u);
}

/* NES source: Z_02 DrawLinkZeldaTriforces; Z_01 Anim_WriteSpecificItemSprites.
 * Drained C: existing sprite descriptor and specific-item writer above.
 * Coverage: ending Link's fixed OAM slots and two-handed lift pose.
 * Stance: EXTEND; retain the writer's shared tile/attribute rules. */
void draw_link_ending_pose(void)
{
    (void)sprite_anim_fetch_obj_pos(0u);
    DRAW_MIRRORED = core_anim_set_sprite_desc_attrs(0u);
    DRAW_LEFT_SPRITE_OFFSET = 0x48u;
    DRAW_RIGHT_SPRITE_OFFSET = 0x4Cu;
    anim_write_specific_item_sprites(0u, 0x21u);
}

/* T-116: Anim_WriteSpecificItemSprites left tile for an item slot and
 * frame image ([0C]). */
unsigned char draw_item_frame_tile(unsigned char item_slot, unsigned char frame)
{
    unsigned char i = (unsigned char)(k_anim_item_frame_offsets[item_slot & 0x3Fu] + frame);
    return k_anim_item_frame_tiles[i & 0x3Fu];
}

/* T-092: DrawItemBySlot's frame-0 tile and sprite attributes for an item
 * slot and inventory value, computed without writing sprites (status-bar
 * A/B boxes). Same rules as draw_item_by_slot below. */
unsigned char draw_item_icon(unsigned char item_slot, unsigned char item_value,
                             unsigned char *attr_out)
{
    unsigned char slot = item_slot;
    unsigned char attrs = k_item_slot_to_palette_offsets_or_values[item_slot & 0x1Fu];
    if (item_slot == 0x16u || item_slot == 0x1Au ||
        item_slot == 0x1Bu || item_slot == 0x19u) {
        attrs = options_consumer_get_no_reduced_flashing()
            ? 1u
            : (unsigned char)((((unsigned char)FRAME_COUNTER >> 3) & 0x01u) + 1u);
    } else if (item_slot == 0x00u || item_slot == 0x04u || item_slot == 0x02u ||
               item_slot == 0x07u || item_slot == 0x0Bu) {
        attrs = (unsigned char)(attrs + item_value);
        if (item_slot == 0u && attrs == 0x02u) slot = 32u;
    }
    *attr_out = attrs;
    return k_anim_item_frame_tiles[k_anim_item_frame_offsets[slot & 0x3Fu] & 0x3Fu];
}

void draw_item_by_slot(unsigned int item_slot, unsigned int slot)
{
    /* drain Z_07.asm:2023-2090. */
    /* NES DrawItemBySlot:@WriteSprites loads X=$0F before the item
     * writer. The dropped item's former monster slot can still have a
     * hit timer; using it here flashes the item with that stale timer.
     * CurObjIndex remains the native publication owner, independently
     * of this descriptor/flash index. Live Manhandla drop t1059:
     * NES tile $F3 attr 2, Genesis incorrectly attr 0 (timer & 3).
     * Stance: EXTEND the existing draw; room/inventory items share it. */
    const unsigned int draw_slot = 0x0Fu;
    (void)slot;
    const unsigned char idx = (unsigned char)(item_slot & 0x1Fu);
    unsigned char attrs = k_item_slot_to_palette_offsets_or_values[idx];

    /* Slots $16/$1A/$1B/$19 flash-cycle palette via FRAME_COUNTER. */
    int flash =
        (item_slot == 0x16u || item_slot == 0x1Au ||
         item_slot == 0x1Bu || item_slot == 0x19u);

    if (flash) {
        /* Phase 9 Task 9.4 NO_REDUCED_FLASHING consumer.
         * Default (option off): NES per-8-frame palette cycle —
         *   attrs = (FRAME_COUNTER & $08) >> 3; +1 with carry-clear.
         * Photosensitive guard (option on): freeze attrs at 1 so the
         * palette no longer toggles. Static appearance instead of
         * 7.5 Hz strobe — keeps the item visible without strobing. */
        if (options_consumer_get_no_reduced_flashing()) {
            attrs = 1u;
        } else {
            attrs = (unsigned char)(((unsigned char)FRAME_COUNTER >> 3) & 0x01u);
            attrs = (unsigned char)(attrs + 1u);
        }
        anim_write_static_item_sprites_with_attributes(
            attrs, draw_slot, item_slot);
        return;
    }

    /* Slots $00/$04/$02/$07/$0B: add item value in TMP4 to attr offset.
     * TMP4 is set by DrawItemInInventory or AnimateItemObject. */
    int add_item_and_table =
        (item_slot == 0x00u || item_slot == 0x04u ||
         item_slot == 0x02u || item_slot == 0x07u ||
         item_slot == 0x0Bu);

    if (add_item_and_table) {
        attrs = (unsigned char)(attrs + (unsigned char)DRAW_LEFT_ATTR);
        /* Special case: item_slot=0 + attrs=2 => ItemFrameOffsets
         * lookup uses item_slot=32 (red sword frame). */
        if (item_slot == 0u && attrs == 0x02u) {
            anim_write_static_item_sprites_with_attributes(
                attrs, draw_slot, 32u);
            return;
        }
    }

    anim_write_static_item_sprites_with_attributes(
        attrs, draw_slot, item_slot);
}

void draw_item_in_inventory(unsigned int item_slot, unsigned int slot)
{
    /* drain Z_07.asm:2011-2014. */
    const unsigned char val = (unsigned char)DRAW_INVENTORY_ITEM(item_slot);
    DRAW_LEFT_ATTR = val;
    draw_item_by_slot(item_slot, slot);
}

void draw_write_boss_sprite(unsigned char tile, unsigned char x,
                            unsigned char y, unsigned char attr)
{
    /* WriteBossSprite (Z_04.asm:5844):
     *     PHA / LDY RollingSpriteIndex / LDA SpriteOffsets,Y / TAY
     *     PLA / STA Sprites+1,Y / LDA $00 / STA Sprites+3,Y / LDA $01
     *     JMP Anim_EndWriteSprite
     * Anim_EndWriteSprite (Z_01.asm:5393):
     *     STA Sprites,Y / LDA $03 / STA Sprites+2,Y / JMP CycleCurSpriteIndex
     *
     * Net: write y/tile/attr/x to the rolling OAM record + cycle cursor. */
    const unsigned char cur_idx = (unsigned char)DRAW_CUR_SPRITE_INDEX;
    const unsigned char off = k_sprite_offsets[cur_idx & 0x3Fu];
    DRAW_OAM_TILE(off) = tile;
    DRAW_OAM_X(off)    = x;
    DRAW_OAM_Y(off)    = y;
    DRAW_OAM_ATTR(off) = attr;
    sprite_cycle_cur_sprite_index();
}

void draw_animate_item_object(unsigned char item_id, unsigned int slot)
{
    /* drain Z_07.asm:1955-2003. */
    const unsigned char timer = (unsigned char)DRAW_ITEM_LIFETIME(slot);
    if (timer >= 0xF0u) {
        /* Lifetime flash — skip draw on even ticks (LSR-with-carry
         * 6502 idiom: `lsr; bcs continue / bcc skip`).
         * Phase 9 Task 9.4 NO_REDUCED_FLASHING consumer: photosensitive
         * guard always draws (no 30 Hz strobe near despawn). The item
         * still despawns at lifetime 0; only the strobe warning is
         * suppressed. */
        if (!options_consumer_get_no_reduced_flashing()
            && (timer & 1u) == 0u) {
            return;
        }
    }
    sprite_anim_fetch_obj_pos(slot);

    /* Map item id -> descriptor; $30 sentinel => value $FF. */
    const unsigned char idx = (unsigned char)(item_id & 0x3Fu);
    unsigned char desc;
    if (idx >= 36u) {
        desc = 0x01u;
    } else {
        desc = k_item_id_to_descriptor[idx];
    }
    unsigned char value;
    if (desc == 0x30u) {
        value = 0xFFu;
    } else {
        value = (unsigned char)(desc & 0x0Fu);
    }
    DRAW_LEFT_ATTR = value;

    const unsigned char item_slot =
        (idx < 36u) ? k_item_id_to_slot[idx] : 0x00u;
    draw_item_by_slot((unsigned int)item_slot, slot);
}

/* --------------------------------------------------------------- */
/* Weapon draw — DrawArrow + DrawSwordShotOrMagicShot.             */
/* Z_07.asm:3437 / 3908 / 3795-3811 / 4293-4320.                   */
/* Phase 7 Task 7.2 step 13 — replaces enemy_projectile_bridge.c   */
/* stubs c_draw_arrow + c_draw_sword_shot_or_magic_shot.           */
/* --------------------------------------------------------------- */

/* RDirectionToWeaponFrame (Z_07.asm:3795). Indexed by reverse-dir
 * Y order: up, down, left, right. */
static const unsigned char k_r_dir_to_weapon_frame[4] = {
    0x00u, 0x00u, 0x01u, 0x01u
};

/* RDirectionToWeaponBaseAttribute (Z_07.asm:3804). Same order. */
static const unsigned char k_r_dir_to_weapon_base_attr[4] = {
    0x00u, 0x80u, 0x00u, 0x00u
};

/* RDirectionToOffsetsX (Z_07.asm:3807). */
static const unsigned char k_r_dir_to_offsets_x[4] = {
    0xFCu, 0xFCu, 0x00u, 0x00u
};

/* RDirectionToOffsetsY (Z_07.asm:3810). */
static const unsigned char k_r_dir_to_offsets_y[4] = {
    0x00u, 0x00u, 0x03u, 0x03u
};

/* NES Z_07.asm:3779/3783 BoomerangFrameCycle and
 * BoomerangBaseSpriteAttrCycle. State $28 uses the ninth spark entry. */
static const unsigned char k_boomerang_frame_cycle[9] = {
    0x00u, 0x01u, 0x02u, 0x01u, 0x00u, 0x01u, 0x02u, 0x01u, 0x03u
};
static const unsigned char k_boomerang_base_attr_cycle[9] = {
    0x00u, 0x00u, 0x00u, 0x40u, 0x40u, 0xC0u, 0x80u, 0x80u, 0x01u
};

void draw_boomerang(unsigned int slot)
{
    const unsigned char state = (unsigned char)OBJ_STATE(slot);
    const unsigned char phase = (unsigned char)(state & 0x0Fu);
    unsigned char attr = k_boomerang_base_attr_cycle[phase];

    DRAW_X = (uint8_t)(unsigned char)OBJ_X(slot);
    DRAW_Y = (uint8_t)(unsigned char)OBJ_Y(slot);
    DRAW_MIRRORED = k_boomerang_frame_cycle[phase];
    /* NES CalcBoomerangFrame: [04] = base attr + InvMagicBoomerang
     * (Z_07.asm:4274-4282), sprite palette 0 or 1. A Goriya's boomerang
     * is OAM attr $00/$40 (T-057 t057_food_bait t605); the old +2 drew
     * it with the red sub-palette 2. */
    attr = (unsigned char)(attr + (unsigned char)RAM(0x0675u));
    DRAW_LEFT_ATTR = attr;      /* NES writes [04] only; [05] stays (T-171) */
    if ((state & 0xF0u) == 0x20u) {
        (void)core_anim_set_sprite_desc_attrs(1u);
    }
    anim_write_item_sprites(slot, 0x1Du);
}

void draw_sword_shot_or_magic_shot(unsigned int slot)
{
    /* drain Z_07.asm:3437. */
    sprite_anim_fetch_obj_pos(slot);

    const unsigned char dir = (unsigned char)OBJ_DIR(slot);
    if ((dir & 0x03u) != 0u) {
        DRAW_Y = (uint8_t)((unsigned char)DRAW_Y + 3u);
    }

    const unsigned int opp = core_get_opposite_dir((unsigned int)dir);
    const unsigned char y_idx = (unsigned char)((opp >> 8) & 0xFFu);

    const unsigned char attrs =
        (unsigned char)((unsigned char)FRAME_COUNTER & 0x03u) |
        k_r_dir_to_weapon_base_attr[y_idx & 0x03u];
    (void)core_anim_set_sprite_desc_attrs((unsigned int)attrs);

    /* 2026-05-23 — same bug class as draw_arrow (see comment below).
     * NES DrawSwordShotOrMagicShot (Z_07.asm:3437) stores frame in $0C
     * (DRAW_MIRRORED), which anim_write_specific_item_sprites reads as
     * the "frame image" param. Writing to DRAW_FRAME ($0D) left $0C
     * uninitialized → tile_idx always reads frame 0 = vertical sword.
     * User report: "Lynels sword sprites are always vertical. When
     * facing horizontal the sprite should also face horizontal".
     * Probe-confirmed via probe_gen_lynel_octo_surgical.lua showing
     * Lynel sword shots with dir=$01/$02 (horizontal) all rendering
     * tile $0281/$0283 (vertical pair) instead of horizontal pair. */
    DRAW_MIRRORED = k_r_dir_to_weapon_frame[y_idx & 0x03u];

    /* Clear stale DRAW_FLIP_H ($0F) — sword/magic shot frame 1 routes
     * through anim_write_horizontally_flippable_sprite_pair which honors
     * $0F. Without this reset, a right-facing shot inherits whatever
     * flip flag the previous drawn sprite left in $0F (e.g. a left-
     * facing Lynel body), producing draw-order-dependent mirroring.
     * Mirrors draw_arrow line 789 pattern. */
    DRAW_FLIP_H = 0u;
    if (y_idx == 2u) {
        DRAW_FLIP_H = (uint8_t)((unsigned char)DRAW_FLIP_H + 1u);
    }

    unsigned int item_slot;
    if (slot >= 0x0Du) {
        const unsigned char st = (unsigned char)OBJ_STATE(slot);
        item_slot = ((st & 0x80u) != 0u) ? 0x23u : 0x22u;
    } else {
        const unsigned char ot = (unsigned char)OBJ_TYPE(slot);
        item_slot = (ot == 0x57u) ? 0x22u : 0x23u;
    }
    anim_write_item_sprites(slot, item_slot);
}

/* SetAttrAndDrawArrow + OffsetAndDrawArrow + L_DrawArrowOrBoomerang
 * (Z_07.asm:3928..). [0C]/[0F] are set by the caller; y_idx is the
 * reverse direction index, base_attr the attribute before the arrow kind. */
static void set_attr_and_draw_arrow(unsigned int slot, unsigned char y_idx,
                                    unsigned char attr)
{
    if (slot < 0x0Du && (unsigned char)OBJ_TYPE(slot) == 0x5Bu) {
        attr = (unsigned char)(attr + 2u);
    } else {
        const unsigned char inv_arrow = (unsigned char)RAM(0x0659u);
        attr = (unsigned char)(attr + inv_arrow - 1u);
    }
    DRAW_LEFT_ATTR = attr;
    DRAW_RIGHT_ATTR = attr;

    DRAW_X = (uint8_t)((unsigned char)OBJ_X(slot) +
                       k_r_dir_to_offsets_x[y_idx & 0x03u]);
    DRAW_Y = (uint8_t)((unsigned char)OBJ_Y(slot) +
                       k_r_dir_to_offsets_y[y_idx & 0x03u]);

    /* L_DrawArrowOrBoomerang: state $2x (spark) draws with palette row 1. */
    if (((unsigned char)OBJ_STATE(slot) & 0xF0u) == 0x20u)
        (void)core_anim_set_sprite_desc_attrs(1u);

    anim_write_item_sprites(slot, 0x02u);  /* arrow item slot */
}

void draw_arrow(unsigned int slot)
{
    /* NES DrawArrow (Z_07.asm:3913): flip when facing left, weapon frame
     * and base attribute by reverse direction index. [0C] is the frame
     * image Anim_WriteSpecificItemSprites reads (DRAW_MIRRORED here). A
     * sparking arrow drawn through this entry (Z_04 UpdateMonsterArrow
     * @CheckShooter) keeps the arrow frame, as on the NES. */
    DRAW_FLIP_H = 0u;
    if ((unsigned char)OBJ_DIR(slot) == 0x02u) {
        DRAW_FLIP_H = (uint8_t)((unsigned char)DRAW_FLIP_H + 1u);
    }
    const unsigned int opp =
        core_get_opposite_dir((unsigned int)OBJ_DIR(slot));
    const unsigned char y_idx = (unsigned char)((opp >> 8) & 0xFFu);
    DRAW_MIRRORED = k_r_dir_to_weapon_frame[y_idx & 0x03u];
    set_attr_and_draw_arrow(slot, y_idx, k_r_dir_to_weapon_base_attr[y_idx & 0x03u]);
}

void draw_arrow_spark(unsigned int slot)
{
    /* NES DrawArrowOrBoomerangAndCheckCollisions @PrepareArrow
     * (Z_07.asm:4033): frame 2 (spark), no flip, base attribute 0. */
    DRAW_MIRRORED = 0x02u;
    DRAW_FLIP_H = 0u;
    const unsigned int opp =
        core_get_opposite_dir((unsigned int)OBJ_DIR(slot));
    set_attr_and_draw_arrow(slot, (unsigned char)((opp >> 8) & 0xFFu), 0u);
}
