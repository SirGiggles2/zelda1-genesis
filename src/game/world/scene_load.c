/* Phase 12.2 SGDK-1 cleanup: dropped unused <genesis.h>. */
#include "scene_load.h"
#include "../world/render/sprite_render.h"
#include "../../../RoomRom/src/atlas/level_chr_swap.h"
#include "bg_palette.h"  /* Phase 12.2 promoted */
#include "render_abi.h"                      /* render_chr_upload */
#include "../../../RoomRom/src/roomrom_vram_map.h"   /* ROOMROM_SPR_TILE_BASE */

void roomrom_scene_load(roomrom_scene_id_t scene_id, unsigned char variant)
{
    /* Variant flows through to each category's redux flag. */
    roomrom_sprites_set_redux(variant);

    /* PR-4a: enqueue scene-bank DMA. Empty contracts (PR-4a default)
     * collapse REQUESTED → READY in one tick. PR-4b populates content. */
    level_chr_swap_request(scene_id);

    switch (scene_id) {
    case ROOMROM_SCENE_OVERWORLD:
    case ROOMROM_SCENE_UW_L1:
    case ROOMROM_SCENE_UW_L2:
    case ROOMROM_SCENE_UW_L3:
    case ROOMROM_SCENE_UW_L4:
    case ROOMROM_SCENE_UW_L5:
    case ROOMROM_SCENE_UW_L6:
    case ROOMROM_SCENE_UW_L7:
    case ROOMROM_SCENE_UW_L8:
    case ROOMROM_SCENE_UW_L9:
        /* Active gameplay scenes: re-upload variant-dependent items
         * atlas (4x sub-pal). Persistent CHR (common + Link walk/attack)
         * stays at boot upload — re-uploading clobbers SCENE_OBJ region
         * (1069..1204) populated by level_chr_swap. PR-4 fix. */
        roomrom_sprites_upload_items_chr();
        break;

    case ROOMROM_SCENE_BOOT:
    case ROOMROM_SCENE_TITLE:
    case ROOMROM_SCENE_FILESELECT:
        /* Aspirational scenes: not yet implemented in RoomRom.
         * RoomRom boots directly into a UW room without title or FS.
         * No-op until those scenes get hand-written renderers. */
        break;

    default:
        break;
    }
}

/* T-129: NES TransferLevelPatternBlocksUW loads PatternBlockUWSP (16 tiles,
 * PPU $08E0 = sprite tiles $8E-$9D: keese, gel/zol, bubble, ...) for every
 * dungeon besides the level bank. It was never uploaded, so those
 * monsters drew as blank sprites (lockstep t129_enemy_sweep: SAT entries
 * on tiles without pixels). translate_tile maps UW $8E-$9D to SPR base +
 * tile = SCENE_OBJ tiles 98..113, above the UWSP (34) and boss (64) banks;
 * only an OW bank (114) overwrites it, so upload after each completed
 * dungeon bank swap.
 * data/chr/sprites.c (tools/extract_chr.py): OWSP 114 tiles, UWSP358 34,
 * UWSP469 34, PatternBlockUWSP 16, UWSP127 34 (Genesis 4bpp, the UWSP
 * atlas encoding). */
extern const unsigned char sprites_chr[];
#define UWSP_BASE_BLOB_OFFSET ((114u + 34u + 34u) * 32u)
#define UWSP_BASE_TILES       16u

void roomrom_scene_uw_sprite_base_tick(void)
{
    static unsigned short s_done_request = 0xFFFFu;
    roomrom_scene_id_t active;
    unsigned short req;
    if (!level_chr_swap_is_ready()) return;
    req = level_chr_swap_request_count();
    if (req == s_done_request) return;
    s_done_request = req;
    active = level_chr_swap_active_scene();
    if (active < ROOMROM_SCENE_UW_L1 || active > ROOMROM_SCENE_UW_L9) return;
    render_chr_upload((unsigned short)((ROOMROM_SPR_TILE_BASE + 0x8Eu) * 32u),
                      sprites_chr + UWSP_BASE_BLOB_OFFSET,
                      (unsigned short)(UWSP_BASE_TILES * 32u));
}
