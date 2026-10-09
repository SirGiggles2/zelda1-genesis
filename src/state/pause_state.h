#ifndef ROOMROM_PAUSE_H
#define ROOMROM_PAUSE_H

/* ---------------------------------------------------------------------------
 * Phase 6 Task 6.10.1 + 6.10.2 — global pause flag.
 *
 * NES Z1 keeps `Paused` at zero-page $E0 with three states:
 *   0  not paused (gameplay runs)
 *   1  voluntary pause (Select-press toggle)
 *   2  involuntary pause (potion drink — World_IsFillingHearts also set)
 *
 * Z_07.asm:472 dispatch tests `Paused != 0` and skips the per-frame
 * gameplay update when paused (the status-mode draw + heart fill loop
 * still runs). RoomRom mirrors that gate via roomrom_pause_is_active().
 *
 * Genesis input mapping: bare START edge-press toggles voluntary pause
 * (NES Select equivalent). Modifier chords (Z/C/A+B+C with START) keep
 * their existing handlers; the pause toggle fires only when no other
 * button is held. Involuntary pause ($02) is set by potion-drink in
 * Task 6.9 / 6.10 and cleared when World_IsFillingHearts completes.
 * ------------------------------------------------------------------------ */

#define ROOMROM_PAUSE_OFF             0u
#define ROOMROM_PAUSE_VOLUNTARY       1u
#define ROOMROM_PAUSE_INVOLUNTARY     2u

extern unsigned char g_paused;

static inline unsigned char roomrom_pause_is_active(void) {
    return g_paused != ROOMROM_PAUSE_OFF;
}

static inline void roomrom_pause_toggle_voluntary(void) {
    /* NES semantics: Select-press toggles between 0 and 1; an involuntary
     * pause (=2) cannot be cleared by Select — only by the potion-drink
     * heart-fill completing. */
    if (g_paused == ROOMROM_PAUSE_INVOLUNTARY) return;
    g_paused = (g_paused == ROOMROM_PAUSE_OFF)
             ? ROOMROM_PAUSE_VOLUNTARY
             : ROOMROM_PAUSE_OFF;
}

static inline void roomrom_pause_set_involuntary(void) {
    g_paused = ROOMROM_PAUSE_INVOLUNTARY;
}

static inline void roomrom_pause_clear_involuntary(void) {
    /* Called by potion-drink when HeartPartial fill completes. */
    if (g_paused == ROOMROM_PAUSE_INVOLUNTARY) g_paused = ROOMROM_PAUSE_OFF;
}

#endif /* ROOMROM_PAUSE_H */
