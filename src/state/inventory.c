#include "inventory.h"
#include "game_profile.h"
#include "../abi/platform_abi.h"
#include "../game/hud/hud_dispatch.h"
#include "../game/hud/heart_container_anim.h"
extern void roomrom_hud_status_bar_formatted(void);   /* hud_runtime.c (T-172) */
extern unsigned char transfer_buf_dyn_busy(void);       /* transfer_buf_drain.c */
extern void roomrom_hud_prepare_items(void);            /* native HUD icon owner */

/* Phase 6 Task 6.10.4 — singleton inventory storage.
 *
 * Boot defaults match the NES Z1 fresh-save profile:
 *   - 3 heart containers, 3 current full hearts (heart_values = 0x33).
 *   - HeartPartial full enough for NES MakeSwordShot ($80+).
 *   - 0 rupees, 0 keys, 0 bombs (NES MaxBombs default = 8).
 *   - No items, no bow/wand/boomerang/etc.
 *
 * The singleton is zero-initialized first; the populated fields below
 * land via the static initializer so save-load work in Phase 0 can
 * replace the literal once authored. */

inventory_t g_inventory = {
    .items            = 0u,
    .bombs            = 0u,
    .arrow            = INV_ARROW_NONE,
    .bow              = 0u,
    .candle           = INV_CANDLE_NONE,
    .food             = 0u,
    .potion           = 0u,
    .raft             = 0u,
    .book             = 0u,
    .ring             = INV_RING_NONE,
    .ladder           = 0u,
    .magic_key        = 0u,
    .bracelet         = 0u,
    .letter           = INV_LETTER_NONE,
    .compass_q1       = 0u,
    .map_q1           = 0u,
    .compass_l9       = 0u,
    .map_l9           = 0u,
    .clock            = 0u,
    .rupees           = 0u,
    .keys             = 0u,
    .heart_values     = 0x33u,         /* 3 max / 3 current */
    .heart_partial    = 0xFFu,
    .triforce         = 0u,
    .boomerang_wood   = 0u,
    .boomerang_magic  = 0u,
    .magic_shield     = INV_SHIELD_WOOD,
    .max_bombs        = MAX_BOMBS_DEFAULT,
    .rupees_to_add    = 0u,
    .rupees_to_sub    = 0u,
    .world_flags      = 0u,
    .selected_b_item  = 0u,
};

static unsigned char s_inventory_hud_dirty = 1u;

void inventory_hud_mark_dirty(void)
{
    s_inventory_hud_dirty = 1u;
}

unsigned char inventory_hud_consume_dirty(void)
{
    unsigned char dirty = s_inventory_hud_dirty;
    s_inventory_hud_dirty = 0u;
    return dirty;
}

/* NES source: Variables.inc:Items / Z_01.asm:ItemIdToSlot.
 * Drained C: item_dispatch.c:item_take_item; nes_ram_sync_inventory_hearts.
 * Coverage: PARTIAL inventory readback; Stance: EXTEND.
 * Native cells own gameplay inventory. This legacy struct is a readback
 * for existing UI/weapon consumers, never an independent award ledger.
 * Selection tokens and world_flags have different layouts and are not copied. */
/* T-172: every field below lives in $0654..$067F; when those 44 bytes are
 * unchanged since the last sync there is nothing to pull (this ran every
 * frame, ~180 instructions). */
#define INV_SHADOW_LONGS 11u /* native_ram bulk:inventory_shadow */
static unsigned long s_inv_shadow[INV_SHADOW_LONGS];
static unsigned char s_inv_shadow_valid;

/* NES source: Redux rupee.asm:GenerateHexValue / Z_01:World_ChangeRupees.
 * Drained C: inventory_sync_from_native / inventory_rupee_tick.
 * Coverage: PARTIAL (readback; migration and full save lifecycle pending).
 * Stance: EXTEND read-only legacy UI mirror of the native ledger. */
unsigned short inventory_rupees_current(void)
{
    const unsigned char decimal = F_ram_067A;
    return game_profile_is_remix() ?
        (unsigned short)(F_ram_067B * 100u +
                        (decimal >> 4) * 10u + (decimal & 15u)) :
        (unsigned short)F_ram_066D;
}

void inventory_sync_from_native(void)
{
    unsigned char changed = 0u;
    unsigned char items = 0u;
    {
        const unsigned long *cells =
            (const unsigned long *)(unsigned long)&nes_ram[0x0654u]; /* native_ram bulk:src/state/inventory.c:103 */
        unsigned char i;
        unsigned char same = s_inv_shadow_valid;
        for (i = 0u; i < INV_SHADOW_LONGS; ++i) {
            const unsigned long v = cells[i];
            if (v != s_inv_shadow[i]) { same = 0u; s_inv_shadow[i] = v; }
        }
        if (same) return;
        s_inv_shadow_valid = 1u;
    }
#define PULL_FIELD(field, cell) do { \
    if (g_inventory.field != cell) changed = 1u; \
    g_inventory.field = cell; \
} while (0)
    PULL_FIELD(bombs, F_ram_0658);
    PULL_FIELD(arrow, F_ram_0659);
    PULL_FIELD(bow, F_ram_065A);
    PULL_FIELD(candle, F_ram_065B);
    PULL_FIELD(food, F_NES_INV_FOOD);
    PULL_FIELD(potion, F_NES_INV_POTION);
    PULL_FIELD(raft, F_ram_0660);
    PULL_FIELD(book, F_NES_INV_BOOK);
    PULL_FIELD(ring, F_ram_0662);
    PULL_FIELD(ladder, F_ram_0663);
    PULL_FIELD(magic_key, F_ram_0664);
    PULL_FIELD(bracelet, F_ram_0665);
    PULL_FIELD(letter, F_ram_0666);
    PULL_FIELD(compass_q1, F_ram_0667);
    PULL_FIELD(map_q1, F_ram_0668);
    PULL_FIELD(compass_l9, F_ram_0669);
    PULL_FIELD(map_l9, F_ram_066A);
    PULL_FIELD(clock, F_ram_066C);
    {
        const unsigned short value = inventory_rupees_current();
        if (g_inventory.rupees != value) changed = 1u;
        g_inventory.rupees = value;
    }
    PULL_FIELD(keys, F_ram_066E);
    PULL_FIELD(heart_values, F_ram_066F);
    PULL_FIELD(heart_partial, F_ram_0670);
    PULL_FIELD(triforce, F_ram_0671);
    PULL_FIELD(boomerang_wood, F_NES_INV_BOOMERANG);
    PULL_FIELD(boomerang_magic, F_ram_0675);
    PULL_FIELD(magic_shield, F_ram_0676);
    PULL_FIELD(max_bombs, F_ram_067C);
    PULL_FIELD(rupees_to_add, F_NES_RUPEES_TO_ADD);
    PULL_FIELD(rupees_to_sub, F_ram_067E);
#undef PULL_FIELD
    if (F_ram_065A) items |= ITEMS_BIT_BOW;
    if (F_NES_INV_WAND) items |= ITEMS_BIT_WAND;
    if (F_NES_INV_BOOMERANG || F_ram_0675) items |= ITEMS_BIT_BOOMERANG;
    if (F_NES_INV_RECORDER) items |= ITEMS_BIT_FLUTE;
    if (F_NES_INV_FOOD) items |= ITEMS_BIT_BAIT;
    if (F_ram_0666) items |= ITEMS_BIT_LETTER;
    if (F_NES_INV_POTION) items |= ITEMS_BIT_POTION_T1;
    if (F_NES_INV_POTION >= 2u) items |= ITEMS_BIT_POTION_T2;
    if (items != g_inventory.items) changed = 1u;
    g_inventory.items = items;
    if (changed) inventory_hud_mark_dirty();
}

/* NES source: Z_01.asm:World_ChangeRupees.
 * Drained C: hud_dispatch.c:hud_tick_native_rupees.
 * Coverage: FULL currency state; Stance: EXTEND.
 * NES cells own currency and pending transactions. Mirror after each tick
 * for legacy display/serialization consumers; never spend the mirror. */
void inventory_rupee_tick(unsigned char frame_counter)
{
    unsigned short previous = g_inventory.rupees;
    /* NES source: Z_07:UpdateHeartsAndRupees.
     * Drained C: HUD inventory descriptor + currency/heart owners.
     * Coverage: PARTIAL (Redux play ordering); Stance: EXTEND.
     * Icons precede heart/status formatting; cached SAT writes do not
     * replace the native descriptor work performed every update. */
    if (game_profile_is_remix()) roomrom_hud_prepare_items();
    /* UpdateHeartsAndRupees: World_FillHearts then World_ChangeRupees. */
    hud_world_fill_hearts();
    /* World_ChangeRupees returns while a static transfer buffer is chosen
     * or the dynamic one holds a record; otherwise it moves a rupee and,
     * on even frames, formats the status bar (hearts, counts) for the
     * next NMI. The Genesis HUD draws it then (T-172: t171_manhandla_sword
     * t1061, a pending record delayed the hearts to t1063). */
    if (F_ram_0014 == 0u && !transfer_buf_dyn_busy()) {
        hud_tick_native_rupees(frame_counter);
        if (!(frame_counter & 1u)) {
            if (game_profile_is_remix()) hud_format_status_bar_text();
            roomrom_hud_status_bar_formatted();
        }
    }
    g_inventory.rupees = inventory_rupees_current();
    g_inventory.rupees_to_add = F_NES_RUPEES_TO_ADD;
    g_inventory.rupees_to_sub = F_ram_067E;
    if (previous != g_inventory.rupees) inventory_hud_mark_dirty();
}

void inventory_rupee_credit(unsigned char count)
{
    unsigned short total = (unsigned short)(F_NES_RUPEES_TO_ADD + count);
    F_NES_RUPEES_TO_ADD = (total > 0xFFu) ? 0xFFu : (unsigned char)total;
    g_inventory.rupees_to_add = F_NES_RUPEES_TO_ADD;
}

void inventory_rupee_debit(unsigned char count)
{
    unsigned short total = (unsigned short)(F_ram_067E + count);
    F_ram_067E = (total > 0xFFu) ? 0xFFu : (unsigned char)total;
    g_inventory.rupees_to_sub = F_ram_067E;
}

/* Plan v5b T2.7 — NES @TakeHeartContainer parity + native scale-up anim.
 *
 * NES Z_01.asm:4538:
 *   LDA Items, Y        ; Y = $18 heart-container slot, value packs max/cur
 *   CMP #$F0            ; if already at max-hearts (max nibble = $F),
 *   BCS Exit            ;   return.
 *   ADC #$11            ; else add 1-max + 1-current (carry was clear from CMP).
 *   JMP SetItemValue    ; store back to Items[Y].
 *
 * engine equivalent: heart_values packs max in hi nibble, cur in lo
 * nibble; cap max at $0F (HEART_CONTAINERS_MAX). The CMP-with-$F0
 * gate means once max == $0F no further heart containers are added. */
unsigned char inventory_add_heart_container(void)
{
    unsigned char hv = g_inventory.heart_values;
    unsigned char max_h = heart_values_max(hv);
    unsigned char cur_h = heart_values_cur(hv);

    if (max_h >= HEART_CONTAINERS_MAX) {
        return 0u;
    }
    max_h++;
    if (cur_h < HEART_CONTAINERS_MAX) {
        cur_h++;
    }
    g_inventory.heart_values = heart_values_pack(max_h, cur_h);
    inventory_hud_mark_dirty();

    /* New container occupies slot (max_h - 1). draw_hearts_row uses
     * 0..7 = bottom row, 8..15 = top row. */
    hud_heart_container_anim_start((unsigned char)(max_h - 1u));
    return 1u;
}
