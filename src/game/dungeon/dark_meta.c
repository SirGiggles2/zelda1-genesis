/* Task 5.8: UW dark-room manifest lookup + lit-state.
 *
 * NES authority: Z_05.asm:7795-7801 (IsDarkRoom_Bank5).
 * Manifest:      RoomRom/data/uw_dark_rooms.{c,h} (master, all 18
 *                quest-levels, generated from blob AttrsE & $80).
 *
 * RoomRom never calls dispatch room_is_dark_room() at runtime — it
 * reads $0A7E in NES SRAM, which is OOB on standalone RoomRom's 2 KB
 * nes_ram (G1).
 */

#include "dark_meta.h"
#include "../../../RoomRom/data/uw_dark_rooms.h"

static unsigned char s_room_lit[256];
static unsigned char s_candle_used_count;

unsigned char roomrom_uw_room_is_dark(unsigned char level,
                                      unsigned char quest,
                                      unsigned char room_id)
{
    if (level >= 10u || quest >= 3u || room_id >= 128u) return 0u;
    return uw_dark_room_lookup[level][quest][room_id] ? 1u : 0u;
}

unsigned char roomrom_uw_room_lit(unsigned char room_id)
{
    return s_room_lit[room_id];
}

void roomrom_uw_room_set_lit(unsigned char room_id)
{
    s_room_lit[room_id] = 1u;
}

void roomrom_uw_room_clear_lit(void)
{
    unsigned short i;
    for (i = 0u; i < 256u; i++) s_room_lit[i] = 0u;
    s_candle_used_count = 0u;
}

unsigned char roomrom_uw_dark_candle_used_count(void)
{
    return s_candle_used_count;
}

void roomrom_uw_dark_note_candle_used(void)
{
    if (s_candle_used_count < 0xFFu) s_candle_used_count++;
}

void roomrom_uw_dark_publish_persist(void)
{
    volatile unsigned char *dst =
        (volatile unsigned char *)ROOMROM_DEBUG_DARK_LIT_BASE;
    unsigned short i;
    for (i = 0u; i < 256u; i++) dst[i] = s_room_lit[i];
}
