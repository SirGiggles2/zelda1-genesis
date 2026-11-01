/* pause_map.h — T-257 Remix pause MAP page: explorable Hyrule overworld.
 *
 * NES source: NONE (Genesis extension, user 2026-10-30: "Real terrain
 * visited only. Scrollable and zoomable like alttp"). Behaviour model:
 * snesrev/zelda3 messaging.c WorldMap_PlayerControl — opens zoomed in on
 * Link, D-pad pans (2 px/frame across, 1 px/frame up/down) clamped to the
 * world, a button toggles the whole-world view, the marker blinks every
 * 16 frames. No Mode 7: two fixed scales.
 * Art: tools/gen_pause_map.py from live captures of every Remix overworld
 * room (tools/audit/t257_ow_room_capture.py), quest 1 and 2.
 * Drained C: save_game.c discovery bits (visited rooms), pause_actions.c.
 * Coverage: NEW presentation. Stance: EXTEND, Remix-only.
 */
#ifndef PAUSE_MAP_H
#define PAUSE_MAP_H

/* Take over the pause screen with the map. */
void pause_map_open(void);
/* One pause tick while the map page is up. joy = held, pressed = new
 * edges (SGDK low byte). Returns 1 once the map has closed and the pause
 * sheet is back. */
unsigned char pause_map_tick(unsigned char joy, unsigned char pressed);

/* T-258 Remix flute destination picker (NES source: Z_01 SummonWhirlwind /
 * UpdateWhirlwind_Full choose by LevelMasks cycling; Remix replaces the
 * automatic choice). The whirlwind's capture requests it; the pause opens on
 * the far map with a number on every level whose Triforce piece is held,
 * the summon's choice selected. D-pad moves, A or C confirms (sets
 * TeleportingLevelIndex + WhirlwindPrevRoomId); B/Start do nothing, the
 * tornado must go somewhere. */
void pause_map_flute_request(void);
/* Requested and the map not open yet (pause_actions opens it). */
unsigned char pause_map_flute_pending(void);
/* Nonzero from request until pause_map_flute_take_done(). */
unsigned char pause_map_flute_busy(void);
/* 1 once (destination set, map closed): the host closes the pause. */
unsigned char pause_map_flute_take_done(void);

#endif /* PAUSE_MAP_H */
