/* NES source: user-approved Redux ring selection extends UpdateSubmenuSelection.
 * Drained C: hud_inventory_item_selectable, loadout_ring_step/filter.
 * Coverage: native party input/equipment and presentation handoff; world timing
 * is the host caller's responsibility. Stance: EXTEND existing inventory. */
#include "item_ring_runtime.h"
#include "inventory_render.h"
#include "../hud/hud_runtime.h"
#include "../../state/input_state.h"
#include "../../state/player_state.h"
#include "../../state/inventory.h"
#include "platform_abi.h"

LoadoutRing g_item_rings[4];

static void publish_primary(void)
{
    F_ram_0656=loadout_party_b(0u);
    g_inventory.selected_b_item=F_ram_0656;
    loadout_set_c_slot(loadout_party_c(0u));
}

void item_ring_reset_all(void)
{
    unsigned char i;
    for(i=0u;i<4u;i++)loadout_ring_reset(&g_item_rings[i]);
    inventory_ring_clear_all();
}
unsigned char item_ring_player_browsing(unsigned char player)
{
    return player<4u && loadout_ring_visible(&g_item_rings[player]);
}
unsigned char item_ring_single_frozen(void)
{
    return g_player_count==1u && item_ring_player_browsing(0u);
}

void item_ring_tick(unsigned char enabled)
{
    unsigned char i,slot;
    unsigned short owned=0u;
    inventory_ring_clear_all();
    if(enabled) {
        /* Temporary P1 legacy menu/HUD bridge; extra actors never import it. */
        /* Party storage is validated at every setter/reset/restore boundary.
         * Equal values need no repeated import validation or stores. */
        if((loadout_party_b(0u)!=F_ram_0656 ||
            loadout_party_c(0u)!=loadout_c_slot()) &&
           !loadout_party_set(0u,F_ram_0656,loadout_c_slot()))
            (void)loadout_party_set(0u,0u,LOADOUT_SLOT_NONE);
        /* T392: ordinary gameplay has no ring work. Keep the primary bridge
         * live, but enumerate ownership/process gestures only when a gesture
         * or a letter/potion alias can actually change the result. All four
         * records participate so inactive-player normalization stays intact. */
        for(i=0u;i<4u;i++) {
            unsigned char b=loadout_party_b(i),c=loadout_party_c(i);
            if(g_item_rings[i].phase!=LOADOUT_RING_IDLE ||
               ((input_state_held(i)|input_state_pressed(i))&0x30u)!=0u ||
               b==7u || b==15u || c==7u || c==15u)break;
        }
        if(i==4u) {
            for(i=0u;i<4u;i++)
                input_state_filter_game(i,input_state_held(i),input_state_pressed(i));
            publish_primary();
            return;
        }
        for(slot=0u;slot<16u;slot++)
            if(hud_inventory_item_selectable(slot))owned|=(unsigned short)(1u<<slot);
    }
    for(i=0u;i<4u;i++) {
        unsigned char b=loadout_party_b(i),c=loadout_party_c(i);
        LoadoutRingInput filtered;
        if(enabled) {
            if((b==15u || b==7u) && (owned & 0x8080u)) b=(owned & 0x80u)?7u:15u;
            if((c==15u || c==7u) && (owned & 0x8080u)) c=(owned & 0x80u)?7u:15u;
            if(loadout_same_cell(b,c))c=LOADOUT_SLOT_NONE;
        }
        filtered=loadout_ring_filter(&g_item_rings[i],input_state_held(i),input_state_pressed(i),
            owned,&b,&c,(unsigned char)(enabled && owned!=0u && i<g_player_count));
        input_state_filter_game(i,filtered.held,filtered.pressed);
        (void)loadout_party_set(i,b,c);
        if(enabled && i<g_player_count)
            inventory_ring_present(i,PLAYER(i).x,PLAYER(i).y,&g_item_rings[i],b,c,owned);
    }
    if(enabled)publish_primary();
}

unsigned char item_ring_cycle_inputs(void)
{
    const unsigned char primary_b=loadout_party_b(0u),primary_c=loadout_party_c(0u);
    unsigned char i;
    for(i=0u;i<g_player_count;i++) {
        unsigned short edges=input_state_game_pressed(i);
        if(g_item_rings[i].phase!=LOADOUT_RING_IDLE || (input_state_held(i)&0x80u))continue;
        hud_cycle_player_loadout(i,(edges&0x200u)!=0u,(edges&0x100u)!=0u);
    }
    /* The native status-item owner already ran after objects. Preserve a
     * same-frame refresh for its normalization or later P1 cycling only. */
    return (unsigned char)(primary_b!=F_ram_0656 || primary_c!=loadout_c_slot());
}

void item_ring_refresh(void)
{
    unsigned char i,slot;
    unsigned short owned=0u;
    inventory_ring_clear_all();
    for(i=0u;i<g_player_count;i++)if(item_ring_player_browsing(i))break;
    if(i==g_player_count)return;
    for(slot=0u;slot<16u;slot++)
        if(hud_inventory_item_selectable(slot))owned|=(unsigned short)(1u<<slot);
    for(i=0u;i<g_player_count;i++)
        inventory_ring_present(i,PLAYER(i).x,PLAYER(i).y,&g_item_rings[i],
            loadout_party_b(i),loadout_party_c(i),owned);
}
