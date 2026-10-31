/* NES source: Z_05 WieldSword/WieldWeapon; Z_07 UpdateSwordOrRod;
 * Redux sword_draw.asm sword_detect and magic_beam_align.asm.
 * Drained C: combat_runtime.c:roomrom_combat_try_swing/update_sword_or_rod.
 * Coverage: FULL native sword lifecycle/pose; collision/render remain adapters.
 * Stance: EXTEND the drained rules through explicit owner and coordinates. */
#include "sword_runtime.h"
static const signed char redux_x[4][8] = {
    /* DOWN  */ { -9, -9, -5, -3, -2, -1,  0,  1 },
    /* UP    */ { 10, 10,  7,  6,  4,  2,  0, -1 },
    /* LEFT  */ {  1, -1, -5, -6, -7, -8,-10,-11 },
    /* RIGHT */ {  1,  3,  5,  6,  7,  8, 10, 11 },
};

static const signed char redux_y[4][8] = {
    /* DOWN  */ {  3,  5,  9, 10, 11, 12, 13, 13 },
    /* UP    */ {  2,  0, -5, -6, -7, -8, -9,-10 },
    /* LEFT  */ {-10,-10, -9, -8, -6, -4,  2,  3 },
    /* RIGHT */ {-10,-10, -8, -7, -5, -3,  2,  3 },
};

static const unsigned char redux_face[4][8] = {
    {2,2,4,4,4,4,4,4}, {1,1,8,8,8,8,8,8},
    {8,8,2,2,2,2,2,2}, {8,8,1,1,1,1,1,1}
};

static const unsigned char k_pw_off_x[16] = {
    0xFFu, 0x01u, 0x00u, 0xF8u, 0xFFu, 0x01u, 0xF5u, 0x0Bu,
    0xFFu, 0x01u, 0xF9u, 0x07u, 0xFFu, 0x01u, 0xFDu, 0x03u
};
static const unsigned char k_pw_off_y[16] = {
    0xF7u, 0xF2u, 0xF5u, 0xF5u, 0xF6u, 0x0Du, 0x03u, 0x03u,
    0xF7u, 0x09u, 0x03u, 0x03u, 0xFFu, 0x05u, 0x03u, 0x03u
};

static unsigned char face_of(unsigned char dir)
{
    return (dir & 8u) ? 1u : (dir & 4u) ? 0u : (dir & 2u) ? 2u : 3u;
}
static unsigned char rdir_of(unsigned char dir)
{
    return (dir & 8u) ? 0u : (dir & 4u) ? 1u : (dir & 2u) ? 2u : 3u;
}
void sword_runtime_start(PlayerSwordState *sw, unsigned char owner,
    unsigned char x, unsigned char y, unsigned char dir)
{
    sw->timer = 5u;
    sw->state = 1u;
    sw->dir = dir;
    sw->face = face_of(dir);
    sw->owner = owner;
    sw->x = (short)(unsigned char)(x + ((dir & 1u) ? 16 : (dir & 2u) ? -16 : 0));
    sw->y = (short)(unsigned char)(y + ((dir & 4u) ? 16 : (dir & 8u) ? -16 : 0));
    if (dir & 12u) sw->x = (short)(unsigned char)(sw->x + 3);
}
unsigned char sword_runtime_tick(PlayerSwordState *sw)
{
    unsigned char count;
    if (sw->state == 0u) return 0u;
    sw->timer = (unsigned char)(sw->timer - 1u); /* NES DEC wraps. */
    if (sw->timer != 0u) return 0u;
    count = (sw->state == 1u) ? 8u : 1u;
    sw->timer = count;
    sw->state = (unsigned char)(sw->state + 1u);
    return count;
}
void sword_runtime_pose(PlayerSwordState *sw, unsigned char x,
    unsigned char y, unsigned char dir, unsigned char remix)
{
    unsigned char idx;
    if (sw->state == 0u || sw->state >= 5u) return;
    if (remix && sw->state == 2u) {
        unsigned char fr = (unsigned char)(8u - sw->timer);
        unsigned char fi = face_of(dir);
        sw->x = (short)(unsigned char)(x + redux_x[fi][fr]);
        sw->y = (short)(unsigned char)(y + redux_y[fi][fr]);
        sw->dir = redux_face[fi][fr];
        sw->face = face_of(sw->dir);
        return;
    }
    sw->dir = dir;
    sw->face = face_of(dir);
    idx = (unsigned char)((sw->state - 1u) * 4u + rdir_of(dir));
    sw->x = (short)(unsigned char)(x + k_pw_off_x[idx]);
    sw->y = (short)(unsigned char)(y + k_pw_off_y[idx]);
    if (remix && sw->state == 1u) {
        unsigned char dummy = (rdir_of(dir) == 1u) ? 0xC0u : 0u;
        sw->x = (short)(unsigned char)(x + dummy);
        sw->y = (short)(unsigned char)(y + dummy + 1u);
    }
}
