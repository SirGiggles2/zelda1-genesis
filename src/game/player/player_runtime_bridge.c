/* NES source: Z_07.asm Obj_Shove / Z_01 BoundByRoom/CheckPersonBlocking.
 * Drained C: enemy_walker_bridge.c c_obj_shove, explicit player_runtime kernel.
 * Coverage: Link shove update; damage producers/observer scratch still legacy.
 * Stance: ADOPT declared inputs/outputs; no whole player context saved here. */
#include "player_runtime.h"
#include "platform_abi.h"
#include "game_profile.h"
void player_runtime_shove_legacy_bridge(PlayerState *p)
{
    unsigned char person = F_CAVE_ROOM_TYPE;
    PlayerShoveWorld world = {
        { &nes_ram[0x6530u], F_ram_0010, F_ram_00EB }, /* native_ram bulk:src/game/player/player_runtime_bridge.c:12 */
        F_NES_BOUND_LEFT, F_NES_BOUND_RIGHT, F_NES_BOUND_TOP, F_NES_BOUND_BOTTOM,
        F_ram_034A, game_profile_is_remix(),
        (unsigned char)(person == 0x36u || (person >= 0x4Bu && person < 0x53u))
    };
    PlayerShoveTrace trace;
    /* Temporary imports: unmigrated contact/damage still produces these. */
    p->shove_dir = F_ram_00C0;
    p->shove_distance = F_ram_00D3;
    player_runtime_shove(p, &world, &trace);
    F_ram_0070 = (unsigned char)p->x;
    F_ram_0084 = (unsigned char)p->y;
    F_ram_0394 = (unsigned char)p->grid_offset;
    F_ram_00C0 = p->shove_dir;
    F_ram_00D3 = p->shove_distance;
    if (!trace.started) return;
    if (trace.tile_queried) {
        unsigned short column = (unsigned short)(0x6530u + trace.tile_query.column * 22u);
        F_ram_0000_N2 = (unsigned char)column;
        F_ram_0001_N2 = (unsigned char)(column >> 8);
        F_ram_0004_N = trace.hotspot;
        F_ram_049E = trace.tile_query.tile;
    }
    if (trace.bounds_sampled) F_ram_0000_N2 = trace.boundary_y;
    F_ram_000F = trace.moving_bits;
    F_ram_0003_N = trace.pixel_budget;
    if (trace.stepped) F_ram_0002_N2 = trace.delta;
}
