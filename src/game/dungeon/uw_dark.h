#ifndef UW_DARK_H
#define UW_DARK_H

/* T-111: NES UW dark rooms by palette (FadeCycle / AnimateWorldFading /
 * UpdateCandle / CandleState), replacing the Genesis plane blanking. */

/* IsDarkRoom (Z_05.asm:7795): CurLevel != 0 and LevelBlockAttrsE bit 7. */
unsigned char uw_dark_is_dark_room(unsigned char room_id);

/* Write LevelInfo_PaletteCycles row for fade cycle value `cycle` (the
 * value AnimateWorldFading would have used) straight to BG palette rows
 * 2-3 ($3F08-$3F0F). Used after a palette reload so the room shows the
 * fade state it is in. */
void uw_dark_apply_cycle_row(unsigned char cycle);

/* UpdateCandle (Z_04.asm:11426). */
void uw_dark_update_candle(void);

/* BrighteningRoom ($51E) != 0: UpdateMode5Play runs only UpdateCandle. */
unsigned char uw_dark_brightening(void);

#endif
