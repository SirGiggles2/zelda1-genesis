/* Phase 9.7 — Gameplay-mode dispatcher.
 *
 * NES source: reference/aldonunez/Z_07.asm:1608 UpdateMode + 1613
 * UpdateMode_JumpTable (20 modes, $00 Demo .. $13 WinGame).
 *
 * Per master plan Phase 9.7, the dispatcher fires UpdateModeN_Full
 * based on GameMode ($FF0012). Each handler updates its sub-state
 * machine then returns. Phase 9.7 scaffold lands the dispatcher with
 * Mode 8 ContinueQuestion wired to its native body (commit 46cf0581);
 * other modes stay as stubs until their bodies port.
 *
 * Stance: PARTIAL — Mode 8 ADOPT (calls mode8_continue_question_update);
 * Modes 0/1/2/3/4/5/6/7/9/A/B/C/D/E/F/10/11/12/13 stubbed.
 */

#include "mode_dispatch.h"
#include "mode_continue_question.h"
#include "mode_death.h"
#include "mode_endlevel.h"
#include "mode_wingame.h"   /* Tier 4 — Mode 0x13 WinGame scaffold */
#include "mode_save.h"      /* Mode 0x0D Save — native, 2026-08-04 */
#include "platform_abi.h"

/* NES Variables.inc GameMode := $12. */
#define MODE_DISPATCH_GAME_MODE RAM(0x0012u)

/* Forward decls for not-yet-drained mode bodies. Each stub returns
 * without state change so caller's loop continues. Bodies port per
 * Phase 9.7 follow-up. */
static void mode_stub(void)
{
    /* no-op — body deferred. */
}

/* Mode 8 ContinueQuestion — Phase 9.7 native body (commit 46cf0581). */
extern void mode8_continue_question_update(void);

/* Entry: dispatch on GameMode value. Mirrors NES JSR TableJump at
 * Z_07.asm:1611. */
void mode_dispatch_update(void)
{
    unsigned char mode = MODE_DISPATCH_GAME_MODE;

    switch (mode) {
    /* Modes 0x00 - 0x02, 0x0D - 0x0F = FRONTEND MODES.
     * These run before the title->gameplay handoff completes. Debug.md
     * boots directly into gameplay via the A+B+C debug-enter chord,
     * which bypasses the entire mode 0/1/2/D/E/F sequence. Wiring them
     * requires the full title-screen->FileSelect->Load handoff path
     * (src/frontend/*, fs_handoff.c trampoline). That handoff is a
     * separate multi-session phase; see plan v6 Tier 3 sketch.
     *
     * Until then these stay stubbed — they are unreachable from
     * Debug.md and wiring drained frontdemo_xxx and frontname_xxx
     * would have no observable effect. */
    case 0x00: mode_stub(); break;  /* Mode 0 Demo — frontend (see comment) */
    case 0x01: mode_stub(); break;  /* Mode 1 Menu (FileSelect) — frontend */
    case 0x02: mode_stub(); break;  /* Mode 2 Load — frontend */
    case 0x03: mode_stub(); break;  /* Mode 3 Unfurl */
    case 0x04: mode_stub(); break;  /* Mode 4 Enter (between rooms) */
    case 0x05: mode_stub(); break;  /* Mode 5 Play — RoomRom owns gameplay tick */
    case 0x06: mode_stub(); break;  /* Mode 6 Leave (between rooms) */
    case 0x07: mode_stub(); break;  /* Mode 7 Scroll */
    case 0x08: mode8_continue_question_update(); break;  /* Phase 9.7 native */
    case 0x09: mode_stub(); break;  /* Mode 9 Play variant */
    case 0x0A: mode_stub(); break;  /* Mode A Play variant */
    case 0x0B: mode_stub(); break;  /* Mode B Play variant */
    case 0x0C: mode_stub(); break;  /* Mode C Play variant */
    case 0x0D: mode13_save_update(); break;  /* Mode D Save — native */
    case 0x0E: mode_stub(); break;  /* Mode E Register — frontend */
    case 0x0F: mode_stub(); break;  /* Mode F Elimination — frontend */
    case 0x10: mode_stub(); break;  /* Mode 10 Stairs */
    case 0x11: mode_stub(); break;  /* T-097: run by roomrom_debug_tick's own mode $11 branch */
    case 0x12: mode_stub(); break;  /* T-013: run by roomrom_debug_tick's own mode $12 branch */
    case 0x13: mode13_wingame_update(); break; /* Native host owns exclusive ending ticks. */
    default:   mode_stub(); break;
    }
}
