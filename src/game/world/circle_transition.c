/* NES SOURCE: NONE (user-requested MD Remix iris; supplied BS Zelda video).
 * DRAINED: cave_fade and native level/cave load/entry owners retained.
 * COVERAGE: PARTIAL (OW/cave/dungeon entrance presentation only).
 * STANCE: REPLACE (Original and the file-start triangle stay unchanged).
 */
#include "circle_transition.h"
#include "../../abi/render_abi.h"

typedef struct {
    const unsigned long *patterns;
    const unsigned short *map;
    unsigned short count;
} circle_frame_t;
#include "circle_transition_masks.inc"

static unsigned char s_circle_enabled, s_circle_phase, s_circle_tick, s_circle_radius;
static unsigned char s_circle_col, s_circle_row;

void circle_transition_set_mode(unsigned char enhanced)
{
    s_circle_enabled = enhanced;
    s_circle_phase = 0u;
}
unsigned char circle_transition_enabled(void) { return s_circle_enabled; }
unsigned char circle_transition_active(void) { return s_circle_phase == 1u || s_circle_phase == 3u; }
unsigned char circle_transition_closing(void) { return s_circle_phase == 1u; }
unsigned char circle_transition_waiting(void) { return s_circle_phase == 2u; }

static const unsigned short *frame_map(unsigned char frame)
{
    return circle_frames[frame].map + (32u - s_circle_row) * 64u + 32u - s_circle_col;
}

static void begin(unsigned char phase, short x, short y, short horizontal, short vertical)
{
    unsigned short dx, dy;
    unsigned long distance;
    if (x < 0) x = 0;
    if (x > 256) x = 256;
    if (y < 0) y = 0;
    if (y > 224) y = 224;
    s_circle_col = (unsigned char)((x + 4) >> 3);
    s_circle_row = (unsigned char)((y + 4) >> 3);
    dx = s_circle_col * 8u;
    dy = s_circle_row * 8u;
    if (dx < 128u) dx = (unsigned short)(256u - dx);
    if (dy < 112u) dy = (unsigned short)(224u - dy);
    distance = (unsigned long)dx * dx + (unsigned long)dy * dy;
    for (s_circle_radius = 1u; s_circle_radius < 43u; ++s_circle_radius) {
        unsigned short radius = s_circle_radius * 8u;
        if ((unsigned long)radius * radius >= distance) break;
    }
    s_circle_phase = phase;
    s_circle_tick = 0u;
    {
        unsigned char frame = phase == 1u ? s_circle_radius : 0u;
        render_circle_begin(horizontal, vertical, circle_frames[frame].patterns,
                            frame_map(frame), circle_frames[frame].count);
    }
}

void circle_transition_close(short x, short y, short horizontal, short vertical)
{
    if (s_circle_enabled && s_circle_phase == 0u) begin(1u,x,y,horizontal,vertical);
}

void circle_transition_open(short x, short y, short horizontal, short vertical)
{
    if (s_circle_phase == 2u) begin(3u,x,y,horizontal,vertical);
}

void circle_transition_tick(void)
{
    unsigned char frame;
    /* Match the native sixteen-step stairs walk's sixty-four frame span. */
    unsigned char total = 64u;
    if (++s_circle_tick >= total) {
        render_circle_finish(s_circle_phase == 1u);
        s_circle_phase = s_circle_phase == 1u ? 2u : 0u;
        return;
    }
    frame = (unsigned char)((unsigned short)s_circle_radius * s_circle_tick / total);
    if (s_circle_phase == 1u) frame = (unsigned char)(s_circle_radius - frame);
    render_circle_frame(circle_frames[frame].patterns, frame_map(frame), circle_frames[frame].count);
}
