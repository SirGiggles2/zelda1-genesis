#include "enemy_state.h"
#include "combat_state.h"

extern void c_obj_shove(unsigned int slot);
extern void c_wanderer_target_player(unsigned int slot);

void wanderer_update_common(unsigned int turn_rate, unsigned int slot) {
    ENEMY_AIR_SPEED(slot) = (unsigned char)turn_rate;

    if (MON_SHOVE_DIR(slot) != 0) {
        c_obj_shove(slot);
        return;
    }

    if ((ENEMY_PAUSE_FLAG | MON_STUN_TIMER(slot)) != 0) {
        return;
    }

    c_wanderer_target_player(slot);
}
