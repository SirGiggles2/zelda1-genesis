#include "loadout.h"

/* B remains NES $0656; C is native and persisted by save_game. */
static unsigned char s_c_slot = LOADOUT_SLOT_NONE;

unsigned char loadout_c_slot(void)
{
    return s_c_slot;
}

void loadout_set_c_slot(unsigned char slot)
{
    s_c_slot = (slot < 9u || slot == 0x0Fu) ? slot : LOADOUT_SLOT_NONE;
}

unsigned char loadout_same_cell(unsigned char a, unsigned char b)
{
    if (a == LOADOUT_SLOT_NONE || b == LOADOUT_SLOT_NONE) return 0u;
    if (a == 0x0Fu) a = 7u;
    if (b == 0x0Fu) b = 7u;
    return a == b;
}

/* NES source: SelectedItemSlot; drained C: loadout_c_slot.
 * Coverage: separate player selections, inventory validation caller-owned.
 * Stance: EXTEND the single-player equipment model. */
static unsigned char s_party_b[4];
static unsigned char s_party_c[4] = {255u, 255u, 255u, 255u};

static unsigned char party_valid(unsigned char b, unsigned char c)
{
    if (b == 3u || (b > 8u && b != 15u)) return 0u;
    if (c == 3u || (c > 8u && c != 15u && c != LOADOUT_SLOT_NONE)) return 0u;
    return !loadout_same_cell(b, c);
}

unsigned char loadout_party_set(unsigned char player, unsigned char b, unsigned char c)
{
    if (player >= 4u || !party_valid(b,c)) return 0u;
    s_party_b[player]=b; s_party_c[player]=c;
    return 1u;
}
void loadout_party_reset(unsigned char b, unsigned char c)
{
    unsigned char i;
    if (!party_valid(b,c)) { b=0u; c=LOADOUT_SLOT_NONE; }
    for (i=0u;i<4u;++i) (void)loadout_party_set(i,b,c);
}
unsigned char loadout_party_b(unsigned char player)
{
    return player<4u ? s_party_b[player] : LOADOUT_SLOT_NONE;
}
unsigned char loadout_party_c(unsigned char player)
{
    return player<4u ? s_party_c[player] : LOADOUT_SLOT_NONE;
}


/* NES source: approved Redux ring UI intentionally extends NES item selection.
 * Drained C: existing loadout_same_cell and caller-owned inventory selection.
 * Coverage: independent native gesture/selection/animation intent; integration
 * remains separate. Stance: EXTEND, no world state, IO or allocation here. */
void loadout_ring_reset(LoadoutRing *r)
{
    if (!r) return;
    r->phase=LOADOUT_RING_IDLE;r->button=0u;r->held_ticks=0u;
    r->highlight=LOADOUT_SLOT_NONE;r->released=0u;r->animation=0u;
}
unsigned char loadout_ring_visible(const LoadoutRing *r)
{
    return (unsigned char)(r && r->phase>=LOADOUT_RING_OPENING);
}
static unsigned char ring_owned(unsigned short mask,unsigned char slot)
{
    return (unsigned char)(slot<16u && (mask & (1u<<slot))!=0u);
}
static unsigned char ring_next(unsigned short mask,unsigned char start,unsigned char step)
{
    unsigned char i;
    for(i=0u;i<16u;++i) {
        start=(unsigned char)((start+step)&15u);
        if(ring_owned(mask,start))return start;
    }
    return LOADOUT_SLOT_NONE;
}
unsigned char loadout_ring_step(LoadoutRing *r,unsigned char held,unsigned char pressed,
    unsigned short owned,unsigned char *b,unsigned char *c,unsigned char enabled)
{
    unsigned char selected,other,slot;
    if(!r || !b || !c)return 0u;
    if(!enabled){loadout_ring_reset(r);return 0u;}
    owned &= 0x81F7u; /* Owned item cells0..8 except3; letter alias15. */
    if(r->phase==LOADOUT_RING_IDLE) {
        selected=(pressed & LOADOUT_RING_B)?LOADOUT_RING_B:
                 (pressed & LOADOUT_RING_C)?LOADOUT_RING_C:0u;
        if(selected){r->phase=LOADOUT_RING_ARMED;r->button=selected;r->held_ticks=1u;}
        return 0u;
    }
    if(r->phase==LOADOUT_RING_ARMED) {
        if(!(held & r->button)) {
            selected=r->button==LOADOUT_RING_B?LOADOUT_RING_USE_B:LOADOUT_RING_USE_C;
            loadout_ring_reset(r);return selected;
        }
        if(++r->held_ticks>=LOADOUT_RING_HOLD_TICKS) {
            r->phase=LOADOUT_RING_OPENING;r->animation=0u;r->released=0u;
            slot=r->button==LOADOUT_RING_B?*b:*c;
            r->highlight=ring_owned(owned,slot)?slot:ring_next(owned,15u,1u);
            return LOADOUT_RING_OPENED;
        }
        return 0u;
    }
    if(r->phase==LOADOUT_RING_CLOSING) {
        if(r->animation) --r->animation;
        if(!r->animation)loadout_ring_reset(r);
        return 0u;
    }
    if(pressed & LOADOUT_RING_CLOSE) {
        r->phase=LOADOUT_RING_CLOSING;
        if(!r->animation)loadout_ring_reset(r);
        return LOADOUT_RING_CLOSED;
    }
    if(r->phase==LOADOUT_RING_OPENING && r->animation<LOADOUT_RING_ANIM_TICKS) {
        if(++r->animation==LOADOUT_RING_ANIM_TICKS)r->phase=LOADOUT_RING_OPEN;
    }
    if(!(held & r->button))r->released=1u;
    if(!ring_owned(owned,r->highlight))r->highlight=ring_next(owned,15u,1u);
    if(pressed & (LOADOUT_RING_PREV|LOADOUT_RING_NEXT))
        r->highlight=ring_next(owned,r->highlight,
            (pressed & LOADOUT_RING_NEXT)?1u:15u);
    if(!r->released)return 0u;
    selected=(pressed & LOADOUT_RING_B)?LOADOUT_RING_B:
             (pressed & LOADOUT_RING_C)?LOADOUT_RING_C:0u;
    if(!selected)return 0u;
    other=selected==LOADOUT_RING_B?*c:*b;
    if(!ring_owned(owned,r->highlight) || loadout_same_cell(other,r->highlight))
        return LOADOUT_RING_REJECTED;
    if(selected==LOADOUT_RING_B){*b=r->highlight;return LOADOUT_RING_ASSIGNED_B;}
    *c=r->highlight;return LOADOUT_RING_ASSIGNED_C;
}

/* NES source: approved tap/hold input extension; drained C: ring_step above.
 * Coverage: Genesis normalized button consumption and one-shot tap dispatch.
 * Stance: EXTEND; physical edges remain owned by input_state. */
LoadoutRingInput loadout_ring_filter(LoadoutRing *r,unsigned short held,unsigned short pressed,
    unsigned short owned,unsigned char *b,unsigned char *c,unsigned char enabled)
{
    LoadoutRingInput result={held,pressed,0u};
    unsigned char ui_held=0u,ui_pressed=0u,was_visible=loadout_ring_visible(r);
    if(!enabled){loadout_ring_reset(r);return result;}
    if(!was_visible && (pressed & 0x80u)) {
        loadout_ring_reset(r);
        result.held &= (unsigned short)~0x30u;
        result.pressed &= (unsigned short)~0x30u;
        return result;
    }
    if(held & 0x10u)ui_held|=LOADOUT_RING_B;
    if(held & 0x20u)ui_held|=LOADOUT_RING_C;
    if(pressed & 0x10u)ui_pressed|=LOADOUT_RING_B;
    if(pressed & 0x20u)ui_pressed|=LOADOUT_RING_C;
    if(pressed & 0x05u)ui_pressed|=LOADOUT_RING_PREV;
    if(pressed & 0x0Au)ui_pressed|=LOADOUT_RING_NEXT;
    if(pressed & 0xC0u)ui_pressed|=LOADOUT_RING_CLOSE;
    result.events=loadout_ring_step(r,ui_held,ui_pressed,owned,b,c,enabled);
    if(was_visible || loadout_ring_visible(r)) {
        result.held=0u;result.pressed=0u;
    } else {
        result.held &= (unsigned short)~0x30u;
        result.pressed &= (unsigned short)~0x30u;
        if(result.events & LOADOUT_RING_USE_B){result.held|=0x10u;result.pressed|=0x10u;}
        if(result.events & LOADOUT_RING_USE_C){result.held|=0x20u;result.pressed|=0x20u;}
    }
    return result;
}
