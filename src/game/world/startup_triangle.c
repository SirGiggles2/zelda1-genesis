/* NES SOURCE: NONE (user-requested MD Remix startup, BS Zelda video reference).
 * DRAINED: Original Z_07 UpdateMode3Unfurl remains in the classic curtain path.
 * STATUS: PARTIAL (custom reveal only; shared entry/load/song logic retained).
 * STANCE: REPLACE (MD Remix file startup only; never dungeon/cave curtains).
 */
#include "startup_triangle.h"
#include "../../abi/render_abi.h"

typedef struct {
    const unsigned long *patterns;
    const unsigned short *map;
    unsigned short count;
} triangle_frame_t;
#include "startup_triangle_masks.inc"

static unsigned char s_state;
static unsigned char s_frame;

void startup_triangle_arm(unsigned char enhanced)
{
    s_state = enhanced ? 1u : 0u;
    s_frame = 0u;
}

unsigned char startup_triangle_preserve_scene(void) { return s_state == 1u; }
unsigned char startup_triangle_active(void) { return s_state == 2u; }

void startup_triangle_prepare(short horizontal, short vertical)
{
    if (s_state != 1u) return;
    render_startup_triangle_begin(horizontal, vertical, tri_frames[0].patterns, tri_frames[0].map);
    s_state = 2u;
}

unsigned char startup_triangle_tick(void)
{
    const triangle_frame_t *frame;
    if (++s_frame >= 46u) {
        render_startup_triangle_finish();
        s_state = 0u;
        return 1u;
    }
    frame = &tri_frames[s_frame];
    render_startup_triangle_frame(frame->patterns, frame->map, frame->count);
    return 0u;
}
