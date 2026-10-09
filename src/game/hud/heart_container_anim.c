/* Plan v5b T2.7 — heart-container scale-up animation. See header. */

#include "heart_container_anim.h"

#define HCA_TILE_EMPTY_HEART 0xF4u
#define HCA_TILE_HALF_HEART  0xF3u
#define HCA_TILE_FULL_HEART  0xF2u

/* Frames per phase (60Hz vblank ticks). 3 phases * 10 frames = 30 frames
 * = ~500ms total scale-up. Long enough to read, short enough to not
 * feel sluggish during item-pickup combat. */
#define HCA_FRAMES_PER_PHASE 10u

/* s_phase semantics:
 *   0 = idle (no animation)
 *   3 = phase 1: EMPTY tile (fade in start)
 *   2 = phase 2: HALF tile
 *   1 = phase 3: FULL tile (last frame before completing)
 * After s_phase decrements past 1 -> back to 0 = idle. */
static unsigned char s_phase = 0u;
static unsigned char s_tick  = 0u;
static unsigned char s_slot  = 0u;

void hud_heart_container_anim_start(unsigned char slot_index)
{
    s_slot  = slot_index;
    s_phase = 3u;
    s_tick  = 0u;
}

void hud_heart_container_anim_tick(void)
{
    if (s_phase == 0u) return;

    s_tick++;
    if (s_tick >= HCA_FRAMES_PER_PHASE) {
        s_tick = 0u;
        s_phase--;
    }
}

unsigned char hud_heart_container_anim_active(void)
{
    return (s_phase != 0u) ? 1u : 0u;
}

unsigned char hud_heart_container_anim_override_tile(unsigned char slot_index,
                                                     unsigned char fallback)
{
    if (s_phase == 0u || slot_index != s_slot) {
        return fallback;
    }
    switch (s_phase) {
    case 3u: return HCA_TILE_EMPTY_HEART;
    case 2u: return HCA_TILE_HALF_HEART;
    case 1u: return HCA_TILE_FULL_HEART;
    default: return fallback;
    }
}
