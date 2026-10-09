/* uw_door_state.c — UW door types, per-room door state, door faces.
 *
 * NES source:  Z_05.asm:FindDoorAttrByDoorBit, LayOutDoors, UpdateDoors,
 *              CheckShutters, TriggerOpenDoor, TouchDoor*, SetDoorFlag /
 *              ResetDoorFlag, AddDoorFlagsToCurOpenedDoors,
 *              SetEnteringDoorwayAsCurOpenedDoors (InitMode7_Sub1) and
 *              InitMode4 @Method2 (close the entered doorway).
 * Drained C:   NONE (drain_coverage.py reports 0 candidates in scope)
 * Coverage:    FULL for the routines above (T-119).
 * Stance:      GREENFIELD port of the NES door-state machine.
 *
 * State lives in NES RAM so lockstep diffs see it: CurOpenedDoors $EE,
 * TriggeredDoorCmd $54 / TriggeredDoorDir $55, DoorTimer $27, Link's
 * ObjTimer $28, ShutterTrigger $4CE, shutter-passed mask $519, and the
 * door flags (bits 0-3 = E/W/S/N, LevelMasks) in the level's world flags
 * reached through LevelInfo_WorldFlagsAddr ($6BAF/$6BB0), which the save
 * carries. s_cur_opened mirrors $EE (true doors only, as LayOutDoors keeps
 * it); s_false_open is Genesis walkability for false walls only.
 */

#include "door_state.h"
#include "uw_render.h"  /* Phase 12.2 promoted */
#include "platform_abi.h"
#include "render_abi.h"                      /* render_plane_defer (T-172) */
#include "../world/transfer_buf_drain.h"    /* transfer_buf_note_native_record */

extern const unsigned char rooms_dungeons[];

#define NES_GAME_MODE          0x0012u
#define NES_DOOR_TIMER         0x0027u
#define NES_LINK_OBJ_TIMER     0x0028u
#define NES_TRIG_DOOR_CMD      0x0054u
#define NES_TRIG_DOOR_DIR      0x0055u
#define NES_CUR_OPENED_DOORS   0x00EEu
#define NES_SHUTTER_TRIGGER    0x04CEu
#define NES_SHUTTER_PASSED     0x0519u
#define NES_PREV_OPENED_DOORS  0x0521u
#define NES_INV_MAGIC_KEY      0x0664u
#define NES_WORLD_FLAGS_PTR    0x6BAFu   /* LevelInfo_WorldFlagsAddr lo/hi */

/* DoorNextRoomIdOffsets (Z_05.asm), indexed E/W/S/N. */
static const signed char k_door_next_room[DOOR_DIR_COUNT] = { 1, -1, 16, -16 };

/* --- Per-room live state --- */
static unsigned char s_door_types[DOOR_DIR_COUNT]; /* DOOR_TYPE_* per dir */
static unsigned char s_cur_opened;                 /* == NES CurOpenedDoors */
static unsigned char s_false_open;                 /* false walls passed    */
static unsigned char s_false_timer;                /* $18..0 countdown      */
static unsigned char s_entering;                   /* entering doorway bit  */
static unsigned char s_cur_level;                  /* 1-based               */
static unsigned char s_cur_room_id;

static void layout_door(unsigned char dir, unsigned char plane);
static void publish_cur_opened_doors(void);
static const unsigned char k_door_face_tiles[DOOR_DIR_COUNT][60];
static const unsigned char k_door_face_origin[DOOR_DIR_COUNT][2];

/* Legacy metatile diagnostics per direction. Real Link collision uses the
 * exact 8px door-face tiles (layout_door), plus doorway-axis bypass in
 * main.c to match NES DoorwayDir behavior. */
static const unsigned char s_walk_mt_col[DOOR_DIR_COUNT]  = {14u, 1u, 8u, 8u};
static const unsigned char s_walk_mt_row[DOOR_DIR_COUNT]  = {5u,  5u, 9u, 1u};
static const unsigned char s_walk_mt_col2[DOOR_DIR_COUNT] = {15u, 0u, 8u, 8u};
static const unsigned char s_walk_mt_row2[DOOR_DIR_COUNT] = {5u,  5u, 10u, 0u};

/* =========================================================================
 * Internal helpers
 * ========================================================================= */

static unsigned char dir_of_bit(unsigned char bit)
{
    return (bit & DOOR_BIT_E) ? DOOR_DIR_E :
           (bit & DOOR_BIT_W) ? DOOR_DIR_W :
           (bit & DOOR_BIT_S) ? DOOR_DIR_S : DOOR_DIR_N;
}

/* GetRoomFlags: address of the room's byte in the level's world flags
 * ($6FF / $77F blocks). 0 when LevelInfo holds no UW world-flags pointer. */
static unsigned short room_flags_addr(unsigned char room_id)
{
    unsigned short ptr = (unsigned short)(nes_ram[NES_WORLD_FLAGS_PTR] |
                          ((unsigned short)nes_ram[NES_WORLD_FLAGS_PTR + 1u] << 8));
    if (ptr < 0x06FFu || ptr > 0x077Fu) return 0u;
    return (unsigned short)(ptr + (room_id & 0x7Fu));
}

/* SetDoorFlag / ResetDoorFlag: LevelMasks[dir] in the room's flags. */
static void set_door_flag(unsigned char room_id, unsigned char dir)
{
    unsigned short a = room_flags_addr(room_id);
    if (a) nes_ram[a] = (unsigned char)(nes_ram[a] | DOOR_DIR_BIT(dir));
}

static void reset_door_flag(unsigned char room_id, unsigned char dir)
{
    unsigned short a = room_flags_addr(room_id);
    if (a) nes_ram[a] = (unsigned char)(nes_ram[a] & (unsigned char)~DOOR_DIR_BIT(dir));
}

/* LayOutDoors: clear CurOpenedDoors bits of doorways that are not true
 * doors, set the door flag of every opened key / bombable door, and copy
 * each door face into the play area (plane + PlayAreaTiles). */
static void lay_out_doors(unsigned char plane)
{
    unsigned char dir;
    for (dir = 0u; dir < DOOR_DIR_COUNT; dir++) {
        unsigned char t = s_door_types[dir];
        unsigned char bit = DOOR_DIR_BIT(dir);
        if (t < DOOR_TYPE_BOMBABLE) {
            s_cur_opened &= (unsigned char)~bit;
        } else if ((s_cur_opened & bit) && t != DOOR_TYPE_SHUTTER) {
            set_door_flag(s_cur_room_id, dir);
        }
    }
    for (dir = 0u; dir < DOOR_DIR_COUNT; dir++) layout_door(dir, plane);
    publish_cur_opened_doors();
    uw_door_state_apply_walkability();
}

/* TriggerOpenDoor. */
static void trigger_open_door(unsigned char bit)
{
    nes_ram[NES_TRIG_DOOR_DIR] = bit;
    nes_ram[NES_TRIG_DOOR_CMD] = 0x06u;
}

/* =========================================================================
 * Public API
 * ========================================================================= */

void uw_door_state_set_entering(unsigned char nes_dir)
{
    /* SetEnteringDoorwayAsCurOpenedDoors: the side opposite Link's
     * direction of travel. */
    s_entering = (unsigned char)(((nes_dir >> 1) & 0x05u) | ((nes_dir << 1) & 0x0Au));
}

void uw_door_state_room_init(unsigned char level,
                              unsigned char quest,
                              unsigned char room_id)
{
    unsigned short base;
    unsigned char attrsA, attrsB;
    unsigned short flags_at;
    unsigned char entering = s_entering;

    s_entering = 0u;
    if (level == 0u || level > 9u || room_id >= 128u) return;

    s_cur_level   = level;
    s_cur_room_id = room_id;
    s_false_timer = 0u;
    s_false_open  = 0u;

    /* FindDoorAttrByDoorBit (Z_05.asm:4520) — packed attribute extraction.
     *
     * rooms_dungeons[] LevelBlock layout (768 bytes per block):
     *   [+0 .. +127]   AttrsA: N = (byte>>5)&7, S = (byte>>2)&7
     *   [+128 .. +255] AttrsB: W = (byte>>5)&7, E = (byte>>2)&7
     *
     * Block offsets: L1-6 Q1 = 0, L7-9 Q1 = 768,
     *                L1-6 Q2 = 1536, L7-9 Q2 = 2304. */
    base = (level <= 6u) ? 0u : 768u;
    if (quest == 2u) base += 1536u;

    attrsA = rooms_dungeons[base + (unsigned short)room_id];
    attrsB = rooms_dungeons[base + 128u + (unsigned short)room_id];

    s_door_types[DOOR_DIR_N] = (unsigned char)((attrsA >> 5) & 7u);
    s_door_types[DOOR_DIR_S] = (unsigned char)((attrsA >> 2) & 7u);
    s_door_types[DOOR_DIR_E] = (unsigned char)((attrsB >> 2) & 7u);
    s_door_types[DOOR_DIR_W] = (unsigned char)((attrsB >> 5) & 7u);

    /* InitMode7_Sub1: PrevOpenedDoors := CurOpenedDoors, CurOpenedDoors :=
     * entering doorway (scroll entries only; other entries start at 0);
     * LayOutRoom then ORs in the room's door flags
     * (AddDoorFlagsToCurOpenedDoors) and runs LayOutDoors. */
    if (entering) nes_ram[NES_PREV_OPENED_DOORS] = s_cur_opened;
    s_cur_opened = entering;
    flags_at = room_flags_addr(room_id);
    if (flags_at) s_cur_opened |= (unsigned char)(nes_ram[flags_at] & 0x0Fu);
    lay_out_doors(1u);

    /* InitMode4 @Method2 (room to room): if Link came through an opened
     * door, command it to close (only shutters change; UpdateDoors). */
    if (entering) {
        unsigned char d = (unsigned char)(entering & s_cur_opened);
        nes_ram[NES_TRIG_DOOR_DIR] = d;
        if (d) nes_ram[NES_TRIG_DOOR_CMD] = 0x02u;
    }
}

/* Re-lay every door face (after the plane is repainted, e.g. relight). */
void uw_door_state_layout_all(void)
{
    unsigned char dir;
    for (dir = 0u; dir < DOOR_DIR_COUNT; dir++) layout_door(dir, 1u);
    uw_door_state_apply_walkability();
}

unsigned char uw_door_state_get_type(unsigned char dir)
{
    if (dir >= DOOR_DIR_COUNT) return DOOR_TYPE_WALL;
    return s_door_types[dir];
}

unsigned char uw_door_state_get_opened(void)
{
    return s_cur_opened;
}

unsigned char uw_door_state_is_open(unsigned char dir)
{
    if (dir >= DOOR_DIR_COUNT) return 0u;
    return (s_cur_opened & DOOR_DIR_BIT(dir)) ? 1u : 0u;
}

/* TouchDoor (Z_05.asm:3879). Returns 1 when Link may pass. */
unsigned char uw_door_state_touch(unsigned char dir, unsigned char *keys)
{
    unsigned char t;
    unsigned char bit;

    if (dir >= DOOR_DIR_COUNT) return 0u;
    t   = s_door_types[dir];
    bit = DOOR_DIR_BIT(dir);

    switch (t) {
    case DOOR_TYPE_OPEN:
        return 1u;

    case DOOR_TYPE_WALL:
        return 0u;

    case DOOR_TYPE_FALSE:
    case DOOR_TYPE_FALSE2:
        /* Once the $18-frame timer has expired the wall is passable. */
        if (s_false_open & bit) return 1u;
        if (s_false_timer == 0u) s_false_timer = 0x18u;
        return 0u;

    case DOOR_TYPE_KEY:
    case DOOR_TYPE_KEY2:
        /* TouchDoorKey. */
        if (s_cur_opened & bit) return 1u;
        if (nes_ram[NES_TRIG_DOOR_CMD] != 0u) {
            /* BlockUntilTime: blocked while Link's timer runs. */
            return (nes_ram[NES_LINK_OBJ_TIMER] != 0u) ? 0u : 1u;
        }
        if (nes_ram[NES_INV_MAGIC_KEY] == 0u) {
            if (keys == (unsigned char *)0 || *keys == 0u) return 0u;
            (*keys)--;
        }
        trigger_open_door(bit);
        nes_ram[NES_LINK_OBJ_TIMER] = 0x20u;
        return 0u;

    case DOOR_TYPE_BOMBABLE:
        /* TouchDoorBombable. */
        return (s_cur_opened & bit) ? 1u : 0u;

    case DOOR_TYPE_SHUTTER:
        /* TouchDoorShutter. */
        if (nes_ram[NES_TRIG_DOOR_CMD] != 0u) return 0u;
        if (!(s_cur_opened & bit)) return 0u;
        if (nes_ram[NES_SHUTTER_PASSED] & bit)
            return (nes_ram[NES_LINK_OBJ_TIMER] != 0u) ? 0u : 1u;
        nes_ram[NES_SHUTTER_PASSED] = (unsigned char)(nes_ram[NES_SHUTTER_PASSED] | bit);
        return 1u;

    default:
        return 0u;
    }
}

void uw_door_state_tick(void)
{
    unsigned char dir;

    if (s_false_timer == 0u) return;
    s_false_timer--;
    if (s_false_timer != 0u) return;

    /* Timer expired: false walls become passable (Genesis walkability
     * only; NES false walls keep their wall art). Re-close on next visit. */
    for (dir = 0u; dir < DOOR_DIR_COUNT; dir++) {
        unsigned char t = s_door_types[dir];
        if (t == DOOR_TYPE_FALSE || t == DOOR_TYPE_FALSE2) {
            s_false_open |= DOOR_DIR_BIT(dir);
        }
    }
    uw_door_state_apply_walkability();
}

/* CheckShutters (Z_05.asm:2133): while ShutterTrigger is set, trigger the
 * first unopened shutter (N, S, W, E order) once no command is pending;
 * clear the trigger when none is left. */
static void check_shutters(void)
{
    unsigned char bit;
    if (nes_ram[NES_SHUTTER_TRIGGER] == 0u) return;
    for (bit = DOOR_BIT_N; bit != 0u; bit = (unsigned char)(bit >> 1)) {
        if (s_cur_opened & bit) continue;
        if (s_door_types[dir_of_bit(bit)] != DOOR_TYPE_SHUTTER) continue;
        if (nes_ram[NES_TRIG_DOOR_CMD] != 0u) return;
        trigger_open_door(bit);
        return;
    }
    nes_ram[NES_SHUTTER_TRIGGER] = 0u;
}

/* One UpdateDoors step's nametable write: the door face's middle 2x2
 * (HorizontalDoorFaceIndexes, two 2-tile transfer records at
 * DoorVramAddrs) for the door type and open/closed state, plane only;
 * PlayAreaTiles changes at the final LayOutDoors. Indexes are into the
 * 12-tile face in layout_door copy order. */
static const unsigned char k_anim_face_idx[DOOR_DIR_COUNT][4] = {
    { 1u, 3u, 6u, 8u }, { 3u, 5u, 8u, 10u }, { 3u, 6u, 4u, 7u }, { 4u, 7u, 5u, 8u }
};

static void door_anim_step(unsigned char dir, unsigned char open)
{
    unsigned char t = s_door_types[dir];
    unsigned char horiz = (dir == DOOR_DIR_E || dir == DOOR_DIR_W) ? 1u : 0u;
    unsigned char rows = horiz ? 2u : 3u;
    unsigned char prov, face, i;
    const unsigned char *src;
    /* T-172: the NES writes these cells as DynTileBuf records (shown after
     * the next NMI, and the buffer is busy for World_ChangeRupees). */
    transfer_buf_note_native_record();
    /* PrepareWriteHorizontalDoorTransferRecords: keys and shutter play
     * the door sound. */
    if (t >= DOOR_TYPE_KEY) nes_ram[0x0601u] |= 0x04u;   /* PlaySample $04: door */
    prov = (t == DOOR_TYPE_BOMBABLE) ? 8u : (t == DOOR_TYPE_WALL) ? 4u : t;
    face = (unsigned char)(prov - 3u);
    if (open) {
        if (face != 5u) face = 1u;
    } else if (face >= 3u) {
        face--;
    }
    if (face == 0u || face > 5u) return;
    src = &k_door_face_tiles[dir][(unsigned short)(face - 1u) * 12u];
    render_plane_defer(1u);
    for (i = 0u; i < 4u; ++i) {
        unsigned char k = k_anim_face_idx[dir][i];
        unsigned char half = (unsigned char)(k / 6u);
        unsigned char rem = (unsigned char)(k % 6u);
        unsigned char c = (unsigned char)(rem / rows);
        unsigned char r = (unsigned char)(rem % rows);
        unsigned char col = k_door_face_origin[dir][0];
        unsigned char row = k_door_face_origin[dir][1];
        if (horiz) {
            col = (unsigned char)(col + c);
            row = (unsigned char)(row + 2u * half + r);
        } else {
            col = (unsigned char)(col + 2u * half + c);
            row = (unsigned char)(row + r);
        }
        roomrom_uw_room_render_write_tile_nt(col, row, src[k],
            roomrom_uw_room_render_palette_at(col, (unsigned char)(row + 8u)));
    }
    render_plane_defer(0u);
}

/* UpdateDoors (Z_05.asm:5006). Commands: 6 -> 7 -> opened, 2 -> 3 ->
 * closed, one step per DoorTimer expiry (8 frames). */
static void update_doors(void)
{
    unsigned char cmd, a, open, bit, dir, t;
    if (nes_ram[NES_GAME_MODE] == 0x12u) return;
    if (nes_ram[NES_DOOR_TIMER] != 0u) return;
    cmd = nes_ram[NES_TRIG_DOOR_CMD];
    if (cmd == 0u) return;
    a = (unsigned char)(cmd & 0x07u);
    if (a & 0x01u) a = (unsigned char)(a >> 1);
    if (a == 0x02u) nes_ram[NES_LINK_OBJ_TIMER] = 0x30u;
    open = (unsigned char)(((unsigned char)((a & 0x03u) - 1u)) & 0x02u);
    bit = nes_ram[NES_TRIG_DOOR_DIR];
    dir = dir_of_bit(bit);
    t = s_door_types[dir];
    if (cmd < 0x05u && t != DOOR_TYPE_SHUTTER) {
        /* Closing a key door or bombable wall does nothing. */
        nes_ram[NES_TRIG_DOOR_CMD] = 0u;
        lay_out_doors(0u);
        return;
    }
    door_anim_step(dir, open);
    cmd = (unsigned char)(cmd + 1u);
    nes_ram[NES_TRIG_DOOR_CMD] = cmd;
    if (cmd & 0x03u) {
        nes_ram[NES_DOOR_TIMER] = 0x08u;
        return;
    }
    if (cmd == 0x04u) {
        reset_door_flag(s_cur_room_id, dir);
        s_cur_opened &= (unsigned char)(bit ^ 0x0Fu);
    } else {
        if (t != DOOR_TYPE_SHUTTER) {
            /* This door's flag and the matching door of the next room. */
            set_door_flag(s_cur_room_id, dir);
            set_door_flag((unsigned char)(s_cur_room_id + k_door_next_room[dir]),
                          (unsigned char)(dir ^ 0x01u));
        }
        s_cur_opened |= bit;
    }
    nes_ram[NES_TRIG_DOOR_CMD] = 0u;
    /* NES LayOutDoors writes PlayAreaTiles only; the nametable already
     * shows the final face (door_anim_step wrote its middle 2x2, the only
     * cells any door state change alters). */
    lay_out_doors(0u);
}

/* Per UW play frame: CheckShutters then UpdateDoors. */
void uw_door_state_update(void)
{
    check_shutters();
    update_doors();
}

/* Debug / probe path: open at once (no command animation). */
void uw_door_state_open_by_mask(unsigned char dir_mask)
{
    unsigned char dir;
    unsigned char any_new = 0u;

    for (dir = 0u; dir < DOOR_DIR_COUNT; dir++) {
        unsigned char bit = DOOR_DIR_BIT(dir);
        unsigned char t = s_door_types[dir];
        if (!(dir_mask & bit))     continue;
        if (s_cur_opened & bit)    continue; /* already open */
        if (t < DOOR_TYPE_BOMBABLE) continue;
        s_cur_opened |= bit;
        if (t != DOOR_TYPE_SHUTTER) {
            set_door_flag(s_cur_room_id, dir);
            set_door_flag((unsigned char)(s_cur_room_id + k_door_next_room[dir]),
                          (unsigned char)(dir ^ 0x01u));
        }
        any_new = 1u;
    }
    if (any_new) {
        lay_out_doors(1u);
        nes_ram[0x0601u] |= 0x04u;   /* PlaySample $04: door */
    }
}

/* Bomb detonation at a bombable wall (Z_07.asm): TriggerOpenDoor. */
void uw_door_state_trigger_open(unsigned char dir_bit)
{
    trigger_open_door(dir_bit);
}

void uw_door_state_apply_walkability(void)
{
    unsigned char dir;

    for (dir = 0u; dir < DOOR_DIR_COUNT; dir++) {
        unsigned char t = s_door_types[dir];
        unsigned char bit = DOOR_DIR_BIT(dir);
        unsigned char open;
        unsigned char mt_col_a, mt_row_a, mt_col_b, mt_row_b;

        if (t == DOOR_TYPE_WALL) {
            open = 0u; /* wall always blocks */
        } else if (t == DOOR_TYPE_OPEN) {
            open = 1u;
        } else {
            open = ((s_cur_opened | s_false_open) & bit) ? 1u : 0u;
        }
        mt_col_a = s_walk_mt_col[dir];
        mt_row_a = s_walk_mt_row[dir];
        mt_col_b = s_walk_mt_col2[dir];
        mt_row_b = s_walk_mt_row2[dir];

        roomrom_uw_room_render_set_walkable(mt_col_a, mt_row_a, open);
        roomrom_uw_room_render_set_walkable(mt_col_b, mt_row_b, open);

        /* Propagate to BG-tile-grain cache. UW Link collision reads via
         * roomrom_uw_room_render_walkable_tile_at(); the metatile cache
         * alone never reaches link_walkable_at. Each metatile covers a
         * 2x2 BG block at (col*2, row*2). */
        {
            unsigned char dr, dc;
            for (dr = 0u; dr < 2u; dr++) {
                for (dc = 0u; dc < 2u; dc++) {
                    roomrom_uw_room_render_set_walkable_tile(
                        (unsigned char)(mt_col_a * 2u + dc),
                        (unsigned char)(mt_row_a * 2u + dr),
                        open);
                    roomrom_uw_room_render_set_walkable_tile(
                        (unsigned char)(mt_col_b * 2u + dc),
                        (unsigned char)(mt_row_b * 2u + dr),
                        open);
                }
            }
        }
    }
}

/* T-114: NES LayOutDoors (Z_05.asm) door faces.
 *
 * Face sets per direction (DoorFaceTilesE/W/S/N, 5 x 12 tiles):
 *   1 open / opened key / opened shutter   2 locked (key, key 2)
 *   3 shutter closed   4 bombable (looks like wall)   5 bombed hole
 * Type -> face (DT DFC DFO): 0 1 1, 4 4 5, 5 2 1, 6 2 1, 7 3 1; walls
 * (1..3) keep the captured wall art. Cells from PlayAreaDoorFaceAddrs
 * ($67A1 E col 28 row 9, $654F W col 1 row 9, $6676 S col 14 row 18,
 * $6665 N col 14 row 1) in the LayOutDoors copy order: E/W 3 cols x 2
 * rows per half (second half 2 rows lower), N/S 2 cols x 3 rows per half
 * (second half 2 cols right). Indexed by DOOR_DIR_E/W/S/N. */
static const unsigned char k_door_face_tiles[DOOR_DIR_COUNT][60] = {
    { 0x88u, 0x74u, 0x8Au, 0x24u, 0x87u, 0x87u, 0x75u, 0x89u, 0x24u, 0x8Bu, 0x87u, 0x87u,
      0x88u, 0xA4u, 0x8Au, 0xA6u, 0x87u, 0x87u, 0xA5u, 0x89u, 0xA7u, 0x8Bu, 0x87u, 0x87u,
      0x88u, 0xACu, 0x8Au, 0xAEu, 0x87u, 0x87u, 0xADu, 0x89u, 0xAFu, 0x8Bu, 0x87u, 0x87u,
      0xDFu, 0xDFu, 0xDFu, 0xDFu, 0xF5u, 0xF5u, 0xDFu, 0xDFu, 0xDFu, 0xDFu, 0xF5u, 0xF5u,
      0xDFu, 0x24u, 0xDFu, 0x92u, 0xF5u, 0xF5u, 0x24u, 0xDFu, 0x93u, 0xDFu, 0xF5u, 0xF5u },
    { 0x82u, 0x82u, 0x83u, 0x24u, 0x85u, 0x76u, 0x82u, 0x82u, 0x24u, 0x84u, 0x77u, 0x86u,
      0x82u, 0x82u, 0x83u, 0xA0u, 0x85u, 0xA2u, 0x82u, 0x82u, 0xA1u, 0x84u, 0xA3u, 0x86u,
      0x82u, 0x82u, 0x83u, 0xACu, 0x85u, 0xAEu, 0x82u, 0x82u, 0xADu, 0x84u, 0xAFu, 0x86u,
      0xF5u, 0xF5u, 0xDEu, 0xDEu, 0xDEu, 0xDEu, 0xF5u, 0xF5u, 0xDEu, 0xDEu, 0xDEu, 0xDEu,
      0xF5u, 0xF5u, 0xDEu, 0x90u, 0xDEu, 0x24u, 0xF5u, 0xF5u, 0x91u, 0xDEu, 0x24u, 0xDEu },
    { 0x7Eu, 0x7Fu, 0x7Du, 0x76u, 0x24u, 0x7Du, 0x74u, 0x24u, 0x7Du, 0x80u, 0x81u, 0x7Du,
      0x7Eu, 0x7Fu, 0x7Du, 0x9Cu, 0x9Du, 0x7Du, 0x9Eu, 0x9Fu, 0x7Du, 0x80u, 0x81u, 0x7Du,
      0x7Eu, 0x7Fu, 0x7Du, 0xA8u, 0xA9u, 0x7Du, 0xAAu, 0xABu, 0x7Du, 0x80u, 0x81u, 0x7Du,
      0xDDu, 0xDDu, 0xF5u, 0xDDu, 0xDDu, 0xF5u, 0xDDu, 0xDDu, 0xF5u, 0xDDu, 0xDDu, 0xF5u,
      0xDDu, 0xDDu, 0xF5u, 0x24u, 0x8Eu, 0xF5u, 0x24u, 0x8Fu, 0xF5u, 0xDDu, 0xDDu, 0xF5u },
    { 0x78u, 0x79u, 0x7Au, 0x78u, 0x24u, 0x77u, 0x78u, 0x24u, 0x75u, 0x78u, 0x7Bu, 0x7Cu,
      0x78u, 0x79u, 0x7Au, 0x78u, 0x98u, 0x99u, 0x78u, 0x9Au, 0x9Bu, 0x78u, 0x7Bu, 0x7Cu,
      0x78u, 0x79u, 0x7Au, 0x78u, 0xA8u, 0xA9u, 0x78u, 0xAAu, 0xABu, 0x78u, 0x7Bu, 0x7Cu,
      0xF5u, 0xDCu, 0xDCu, 0xF5u, 0xDCu, 0xDCu, 0xF5u, 0xDCu, 0xDCu, 0xF5u, 0xDCu, 0xDCu,
      0xF5u, 0xDCu, 0xDCu, 0xF5u, 0x8Cu, 0x24u, 0xF5u, 0x8Du, 0x24u, 0xF5u, 0xDCu, 0xDCu },
};
static const unsigned char k_door_face_origin[DOOR_DIR_COUNT][2] = {
    { 28u, 9u }, { 1u, 9u }, { 14u, 18u }, { 14u, 1u }
};

/* Face index 1..5 for the door's type and opened state; 0 = keep wall. */
static unsigned char door_face_index(unsigned char dir)
{
    unsigned char t = s_door_types[dir];
    unsigned char opened = (s_cur_opened & DOOR_DIR_BIT(dir)) ? 1u : 0u;
    switch (t) {
    case DOOR_TYPE_OPEN:     return 1u;
    case DOOR_TYPE_BOMBABLE: return opened ? 5u : 4u;
    case DOOR_TYPE_KEY:
    case DOOR_TYPE_KEY2:     return opened ? 1u : 2u;
    case DOOR_TYPE_SHUTTER:  return opened ? 1u : 3u;
    default:                 return 0u;
    }
}

/* plane: 1 = nametable + PlayAreaTiles (room layout), 0 = PlayAreaTiles
 * and walkability only (door command completion). */
static void layout_door(unsigned char dir, unsigned char plane)
{
    unsigned char face = door_face_index(dir);
    const unsigned char *src;
    unsigned char half, c, r, k;
    if (face == 0u) return;
    src = &k_door_face_tiles[dir][(unsigned short)(face - 1u) * 12u];
    k = 0u;
    for (half = 0u; half < 2u; ++half) {
        unsigned char cols = (dir == DOOR_DIR_E || dir == DOOR_DIR_W) ? 3u : 2u;
        unsigned char rows = (dir == DOOR_DIR_E || dir == DOOR_DIR_W) ? 2u : 3u;
        for (c = 0u; c < cols; ++c) {
            for (r = 0u; r < rows; ++r) {
                unsigned char col = k_door_face_origin[dir][0];
                unsigned char row = k_door_face_origin[dir][1];
                unsigned char pal;
                if (dir == DOOR_DIR_E || dir == DOOR_DIR_W) {
                    col = (unsigned char)(col + c);
                    row = (unsigned char)(row + 2u * half + r);
                } else {
                    col = (unsigned char)(col + 2u * half + c);
                    row = (unsigned char)(row + r);
                }
                if (plane) {
                    pal = roomrom_uw_room_render_palette_at(col, (unsigned char)(row + 8u));
                    roomrom_uw_room_render_write_tile(col, row, src[k], pal);
                } else {
                    roomrom_uw_room_render_write_tile_pat(col, row, src[k]);
                }
                ++k;
            }
        }
    }
}


/* CurOpenedDoors ($EE) as the NES keeps it: only true doors (bombable,
 * key, key 2, shutter) keep their bit (LayOutDoors clears the rest). */
static void publish_cur_opened_doors(void)
{
    nes_ram[NES_CUR_OPENED_DOORS] = s_cur_opened;
}

void uw_door_state_patch_open_tiles(unsigned char dir)
{
    if (dir >= DOOR_DIR_COUNT) return;
    layout_door(dir, 1u);
}

unsigned char uw_door_state_has_shutters(void)
{
    unsigned char dir;
    for (dir = 0u; dir < DOOR_DIR_COUNT; dir++) {
        if (s_door_types[dir] == DOOR_TYPE_SHUTTER) return 1u;
    }
    return 0u;
}

/* Room clear / last boss: set ShutterTrigger; check_shutters opens the
 * shutters one command at a time. */
void uw_door_state_trigger_shutters(void)
{
    nes_ram[NES_SHUTTER_TRIGGER] = 1u;
}

unsigned char uw_door_state_false_timer(void)
{
    return s_false_timer;
}

unsigned char uw_door_state_current_level(void)
{
    return s_cur_level;
}

/* Probe block: the active level's door flags (world flags bits 0-3). */
void uw_door_state_copy_persist_for_active_level(unsigned char *dst,
                                                  unsigned short dst_size)
{
    unsigned short i;
    if (dst == 0 || dst_size == 0u) return;
    for (i = 0u; i < dst_size; i++) {
        unsigned short a = (i < 128u) ? room_flags_addr((unsigned char)i) : 0u;
        dst[i] = a ? (unsigned char)(nes_ram[a] & 0x0Fu) : 0u;
    }
}
