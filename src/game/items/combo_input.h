#ifndef COMBO_INPUT_H
#define COMBO_INPUT_H

/* T-260: Remix ordered B+C combo press resolver (per player, pure).
 *
 * NES source: none (enhancement). Drained C: none. Coverage: NONE.
 * Stance: GREENFIELD. Spec: docs/superpowers/specs/2026-10-31-T260-item-combos-design.md
 *
 * Runs on the gameplay edge mask after the item ring filter. A B or C press
 * whose ordered pair has a recipe is held for up to COMBO_WINDOW_UPDATES
 * updates; the other button inside the window (or same update, or X) fires
 * the combo once. On expiry the held press is re-emitted as an ordinary
 * press. Button bits are supplied by the caller; no SGDK header. */
#define COMBO_WINDOW_UPDATES 3u

typedef struct { unsigned short pressed; unsigned char fire; } ComboInputOut;

void combo_input_set_masks(unsigned short b, unsigned short c, unsigned short x);
void combo_input_reset(unsigned char player);
void combo_input_reset_all(void);
ComboInputOut combo_input_step(unsigned char player, unsigned short pressed,
                               unsigned char has_recipe);

#endif /* COMBO_INPUT_H */
