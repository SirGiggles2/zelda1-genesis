/* core_dispatch.c — native core subsystem dispatch (Phase 4 cross-cut).
 *
 * Phase 4 first core batch: trivial helpers that unblock Phase 3 cave
 * deferred stubs. Drain MATCH per finding 4_6n. Pure C, no shims.
 */

#include "core_dispatch.h"
#include "../audio/audio_requests.h"
#include "platform_abi.h"      /* RAM, OBJ */
#include "../../state/weapon_state.h"
#include "object_state.h"      /* OBJ_STATE */
#include "room_state.h"        /* ROOM_TRANSFER_BUF_SELECT, ROOM_TILE_XFER_BUF */
#include "link_state.h"        /* DEATH_FRAME_COUNTER */
#include "object_state.h"      /* OBJ_TILE_X/_Y, OBJ_STATE, OBJ_TYPE, OBJ_SHOVE_DIR/DIST, OBJ_INV_TIMER, OBJ_METASTATE */
#include "sprite_state.h"      /* OAM_BYTE */
#include "progress_state.h"    /* SUBMODE_VALUE, PROG_ITEMS_BY_LEVEL */
#include "combat_state.h"      /* LINK_HEARTS */
#include "../items/candle_fire.h"  /* candle_fire_wield_from_shot (T-116) */
#include "../enemies/enemy_dispatch.h" /* transient shield slot lifecycle */

/* T5.2 plan v5b — DMC SFX dispatch from core tune writers. NES bitmap
 * → sample index per src/nes_io.asm:2547 DMC_SAMPLE_LOOKUP. */
extern void audio_sfx_play(unsigned char sfx);

void core_unhalt_link(void)
{
    /* drain at core_runtime.c:56-58. NES UnhaltLink (Z_01.asm:100):
     *   LDA #$00 / STA ObjState  -- ObjState = $00AC = OBJ_STATE(0). */
    OBJ_STATE(0) = 0u;
}

void core_inc_cave_state(void)
{
    /* drain at core_runtime.c:60-62. NES IncCaveState:
     *   INC ObjState+1  -- ObjState+1 = $00AD = OBJ_STATE(1) = CAVE_PERSON_STATE. */
    OBJ_STATE(1) = (uint8_t)(OBJ_STATE(1) + 1u);
}

void core_cue_transfer_buf_and_advance_state(unsigned int val)
{
    /* drain at core_runtime.c:160-163. NES CueTransferBufAndAdvanceState:
     *   STA TileBufSelector ($14 = ROOM_TRANSFER_BUF_SELECT)
     *   INC ObjState+1 (= core_inc_cave_state).
     */
    ROOM_TRANSFER_BUF_SELECT = (unsigned char)val;
    core_inc_cave_state();
}

unsigned char core_abs(unsigned int val)
{
    /* drain at core_runtime.c:219+. NES Abs: 6502 absolute value of a
     * signed byte. */
    const signed char s = (signed char)(unsigned char)val;
    return (unsigned char)((s < 0) ? -s : s);
}

void core_copy_price_list_template(void)
{
    /* drain at core_runtime.c:249-258. NES CopyPriceListTemplate.
     * Static 17-byte template into ROOM_TILE_XFER_BUF[0..16]. */
    static const unsigned char tmpl[17] = {
        0x22u, 0xC8u, 0x0Du, 0x21u, 0x24u, 0x24u, 0x24u, 0x24u,
        0x24u, 0x24u, 0x24u, 0x24u, 0x24u, 0x24u, 0x24u, 0x24u, 0xFFu
    };
    for (signed char i = 16; i >= 0; --i) {
        ROOM_TILE_XFER_BUF((unsigned char)i) = tmpl[(unsigned char)i];
    }
}

void core_post_debit(unsigned int amount)
{
    /* drain at core_runtime.c:32-34. NES PostDebit: lazy deferred
     * rupee debit accumulator at $067E. HUD tick decrements rupees
     * by 1/frame against this accumulator. */
    F_ram_067E_R = (unsigned char)(F_ram_067E_R + (unsigned char)amount);
}

void core_post_credit(unsigned int val)
{
    /* drain at core_runtime.c:107-109. NES PostCredit: lazy deferred
     * rupee credit accumulator at $067D. HUD tick increments rupees
     * by 1/frame against this accumulator. */
    F_NES_RUPEES_TO_ADD_R2 = (unsigned char)(F_NES_RUPEES_TO_ADD_R2 + (unsigned char)val);
}

void core_take_one_rupee(void)
{
    /* drain at core_runtime.c:165-168. NES TakeOneRupee:
     *   LDA #$01 / STA DEATH_FRAME_COUNTER  -- HUD anim trigger
     *   INC RAM($067D)                       -- credit accumulator++ */
    DEATH_FRAME_COUNTER = 1u;
    F_NES_RUPEES_TO_ADD_R2 = (uint8_t)(F_NES_RUPEES_TO_ADD_R2 + 1u);
}

void core_take_5_rupees(void)
{
    /* drain at core_runtime.c:195-200. NES Take5Rupees: loop
     * TakeOneRupee 5 times. */
    for (signed char i = 4; i >= 0; i--) {
        core_take_one_rupee();
    }
}

void core_cue_transfer_blank_person_wares(void)
{
    /* drain at core_runtime.c:191-193. NES UpdatePersonState_CueTransferBlankPersonWares:
     *   LDA #$2A / JMP CueTransferBufAndAdvanceState  -- selector 42 + state++ */
    core_cue_transfer_buf_and_advance_state(42u);
}

void core_destroy_object_wram(unsigned int val, unsigned int slot)
{
    /* drain at core_runtime.c:42-50. NES DestroyObjectWram. */
    enemy_like_like_shield_forget(slot);
    OBJ_SHOVE_DIR(slot)  = (uint8_t)val;
    OBJ_SHOVE_DIST(slot) = (uint8_t)val;
    if (slot == 16u || slot == 17u) {
        shared_effect_set_timer((unsigned char)slot, (unsigned char)val);
        shared_effect_set_state((unsigned char)slot, (unsigned char)val);
    } else {
        F_ram_0028_R3(slot) = (uint8_t)val;  /* ObjTimer+slot */
        OBJ_STATE(slot) = (uint8_t)val;
    }
    OBJ_INV_TIMER(slot)  = (uint8_t)val;
    F_ram_0492_R(slot)   = 0xFFu;
    OBJ_METASTATE(slot)  = 1u;
}

void core_destroy_whirlwind(unsigned int slot)
{
    /* drain at core_runtime.c:52-54. NES DestroyWhirlwind (Z_01.asm:1877)
     * starts with STA ObjType,X = 0 (T-147: the type survived, t129 t5905
     * whirlwind at the right edge kept updating). */
    OBJ_TYPE(slot) = 0u;
    core_destroy_object_wram(0u, slot);
}

void core_destroy_monster(unsigned int slot)
{
    /* drain at core_runtime.c:374. NES DestroyMonster. */
    OBJ_TYPE(slot) = 0u;
    core_destroy_object_wram(0u, slot);
}

void core_init_one_simple_object(unsigned int slot)
{
    /* drain at core_runtime.c:36-40. NES InitOneSimpleObject:
     *   $00 holds object type, $01 holds status flag byte. */
    OBJ_TYPE(slot)     = (uint8_t)F_ram_0000_R;
    F_ram_0492_R(slot) = 0u;
    F_ram_04BF_R2(slot) = (uint8_t)F_ram_0001_R;
}

void core_set_up_whirlwind(unsigned int slot)
{
    /* drain at core_runtime.c:64-68. NES SetUpWhirlwind:
     *   OBJ_TILE_Y(slot) = OBJ_TILE_Y(0)  -- copy Link's tile Y
     *   OBJ_TILE_X(slot) = 0
     *   OBJ_TYPE(slot)   = 46             -- whirlwind type. */
    OBJ_TILE_Y(slot) = (uint8_t)OBJ_TILE_Y(0);
    OBJ_TILE_X(slot) = 0u;
    OBJ_TYPE(slot)   = 46u;
}

void core_init_whirlwind(unsigned int val, unsigned int slot)
{
    /* drain at core_runtime.c:174-177. */
    OBJ_TILE_Y(0) = (uint8_t)val;
    core_set_up_whirlwind(slot);
}

unsigned char core_anim_set_sprite_desc_attrs(unsigned int val)
{
    /* drain at core_runtime.c:101-105. */
    F_ram_0004_R = (uint8_t)val;
    F_ram_0005_R = (uint8_t)val;
    return (uint8_t)val;
}

void core_set_item_value(unsigned int val, unsigned int slot3)
{
    /* drain at core_runtime.c:170-172. */
    PROG_ITEMS_BY_LEVEL(slot3) = (uint8_t)val;
}

unsigned int core_get_opposite_dir(unsigned int dir)
{
    /* drain at core_runtime.c:202-217. NES GetOppositeDir. */
    static const unsigned char opposite_dirs[] = { 0x04u, 0x08u, 0x01u, 0x02u };
    unsigned char d = (unsigned char)dir;
    signed char idx = 3;
    while (idx >= 0) {
        if (d & 1u) {
            break;
        }
        d = (unsigned char)(d >> 1);
        idx--;
    }
    if (idx < 0) {
        /* Direction 0: Y ends at $FF and LDA OppositeDirs, Y reads
         * OppositeDirs + $FF = $710E, a byte of the common code copied to
         * WRAM: $A9 (constant ROM code, read from the NES WRAM dump; also
         * in the linked PRG). T-171: BeginShove reverses a monster's ObjDir
         * this way when it harms Link; a Gleeok head with ObjDir 0 got
         * $A9 on the NES, $04 here (t171_gleeok_sword t473). */
        return (0xFFu << 8) | 0xA9u;
    }
    return ((unsigned int)(unsigned char)idx << 8) |
           (unsigned int)opposite_dirs[(unsigned char)idx];
}

unsigned char core_negate(unsigned int val)
{
    /* drain at core_runtime.c:224-227. NES Negate (2's complement). */
    const unsigned char v = (unsigned char)val;
    return (unsigned char)((~v + 1u) & 0xFFu);
}

void core_begin_update_mode(void)
{
    /* drain at core_runtime.c:155-158. */
    SUBMODE_VALUE = 0u;
    F_ram_0011_R2 = (uint8_t)(F_ram_0011_R2 + 1u);  /* ROOM_MODE_TIMER */
}

void core_play_effect(unsigned int val)
{
    /* drain at core_runtime.c:229-231. */
    F_ram_0603_R2 = (uint8_t)(F_ram_0603_R2 | (uint8_t)val);  /* ROOM_SFX_AUX */
}

void core_play_sample(unsigned int val)
{
    /* drain at core_runtime.c:233+. */
    F_ram_0601_R2 = (uint8_t)(F_ram_0601_R2 | (uint8_t)val);
}

unsigned char core_compare_hearts_to_containers(void)
{
    /* NES CompareHeartsToContainers stores the container count in $00,
     * then returns the whole-heart count for the caller's CMP $00. */
    F_ram_0000_R = (uint8_t)(LINK_HEARTS & 0x0Fu);
    return (uint8_t)(LINK_HEARTS >> 4);
}

void core_format_char_doublet(unsigned int val)
{
    /* drain at core_runtime.c:272-275. NES FormatCharDoublet. */
    F_ram_0002_R = (uint8_t)val;
    F_ram_0003_R = 36u;
}

unsigned char core_reset_cur_sprite_index(void)
{
    /* drain at core_runtime.c:277-280. NES ResetCurSpriteIndex. */
    F_ram_0341_R = 0u;
    return 0u;
}

void core_uw_person_complex_state_begin(void)
{
    /* drain at core_runtime.c:264-270. NES UWPersonComplexStateBegin.
     * RAM($0029) = ObjTimer+1 (drain comment says CAVE_DELAY_TIMER alias). */
    if (F_CAVE_ROOM_TYPE_R3 == 0x4Fu) {  /* ROOM_OBJ_TYPE(0) */
        ROOM_TRANSFER_BUF_SELECT = 108u;
    }
    F_ram_0029_R = 10u;
    OBJ_STATE(1) = (uint8_t)(OBJ_STATE(1) + 1u);
}

void core_play_boomerang_sfx(unsigned int sfx_id)
{
    /* drain at core_runtime.c:282-288. NES PlayBoomerangSfx. */
    if (F_NES_ITEM_OBJ_TIMER_R != 0u) {
        return;
    }
    core_play_effect(sfx_id);
    F_NES_ITEM_OBJ_TIMER_R = 10u;
}

void core_play_character_sfx(void)
{
    /* drain at core_runtime.c:9-11. */
    DEATH_FRAME_COUNTER = 8u;
}

void core_play_key_taken_tune(void)
{
    /* drain at core_runtime.c:13-16. */
    DEATH_FRAME_COUNTER = 0u;
    F_ram_0604_R = 8u;  /* ROOM_SFX_MAIN */
}

void core_play_parry_tune(void)
{
    /* drain at core_runtime.c:237-239. */
    F_ram_0604_R = 1u;  /* ROOM_SFX_MAIN */
}

unsigned char core_silence_all_sound(void)
{
    /* T-192: adopt core_runtime.c:24-30's RAM reset and synchronize
     * the native tune/PCM mirrors at this same SilenceAllSound boundary. */
    audio_requests_silence_tunes();
    F_ram_0604_R = 0x80u;  /* ROOM_SFX_MAIN */
    F_ram_0603_R = 0x80u;  /* ROOM_SFX_AUX */
    F_ram_0605_R = 0u;
    F_ram_0607_R = 0u;
    return 0u;
}

void core_take_power_triforce(void)
{
    /* drain at core_runtime.c:18-22. */
    F_POWER_TRIFORCE_FANFARE_FLAG_R2 = (uint8_t)(F_POWER_TRIFORCE_FANFARE_FLAG_R2 + 1u);  /* POWER_TRIFORCE_FANFARE_FLAG */
    F_ram_0028_R = 0xC0u;
    OBJ_STATE(0) = 64u;
}

void core_write_blank_priority_sprites(void)
{
    /* drain at core_runtime.c:241-247. NES WriteBlankPrioritySprites.
     * 8-byte template repeated 8 times = 64 OAM bytes (16 sprites). */
    static const unsigned char tmpl[8] = {
        0x3Du, 0x1Cu, 0x20u, 0x00u, 0xDDu, 0x1Cu, 0x20u, 0x00u
    };
    for (unsigned char i = 0u; i < 0x40u; i++) {
        OAM_BYTE(i) = tmpl[i & 7u];
    }
}

/* 16-bit add helpers shared body: add val to lo, carry out to hi. */
static inline unsigned char core_add_to_int16_at_inline(unsigned int val,
                                                        unsigned short lo_addr)
{
    const unsigned int sum =
        (unsigned int)(unsigned char)val +
        (unsigned int)nes_ram[lo_addr]; /* native_ram bulk:dyn_addr */
    nes_ram[lo_addr] = (unsigned char)sum; /* native_ram bulk:dyn_addr */
    if (sum > 0xFFu) {
        nes_ram[lo_addr + 1u] = (uint8_t)(nes_ram[lo_addr + 1u] + 1u); /* native_ram bulk:dyn_addr */
    }
    return (unsigned char)sum;
}

unsigned char core_add_to_int16_at_0(unsigned int val)
{
    return core_add_to_int16_at_inline(val, 0x0000u);
}

unsigned char core_add_to_int16_at_2(unsigned int val)
{
    return core_add_to_int16_at_inline(val, 0x0002u);
}

unsigned char core_add_to_int16_at_4(unsigned int val)
{
    return core_add_to_int16_at_inline(val, 0x0004u);
}

unsigned char core_add1_to_int16_at_0(void)
{
    return core_add_to_int16_at_0(1u);
}

unsigned char core_add1_to_int16_at_2(void)
{
    return core_add_to_int16_at_2(1u);
}

unsigned char core_add1_to_int16_at_4(void)
{
    return core_add_to_int16_at_4(1u);
}

unsigned int core_sub1_from_int16_at4(void)
{
    /* drain at core_runtime.c:315-323. NES Sub1FromInt16At4. Returns
     * CARRY_SET when no borrow occurred, else 0. */
    const unsigned char lo = (unsigned char)F_ram_0004_R;
    const unsigned int borrow = (lo == 0u) ? 1u : 0u;
    F_ram_0004_R = (unsigned char)(lo - 1u);
    if (borrow) {
        F_ram_0005_R = (unsigned char)(F_ram_0005_R - 1u);
    }
    return borrow ? 0u : CARRY_SET;
}

void core_uw_person_complex_state_delay_and_quit(void)
{
    /* drain at core_runtime.c:70-74. NES UWPersonComplexStateDelayAndQuit. */
    if (F_ram_0029_R == 0u) {
        F_CAVE_ROOM_TYPE_R3 = 0u;  /* ROOM_OBJ_TYPE(0) */
    }
}

void core_set_boomerang_speed(unsigned int val, unsigned int slot)
{
    /* drain at core_runtime.c:76-85. NES SetBoomerangSpeed. */
    F_ram_03BC_R3(slot) = (uint8_t)val;
    if ((OBJ_STATE(slot) & 0xF0u) == 0x40u) {
        F_ram_03BC_R3(slot) = (uint8_t)(F_ram_03BC_R3(slot) >> 1);
        F_ram_0380_R(slot) = (uint8_t)(F_ram_0380_R(slot) - 1u);
        if (F_ram_0380_R(slot) == 0u) {
            OBJ_STATE(slot) = 80u;
        }
    }
}

void core_map_screen_pos_to_ppu_addr(void)
{
    /* drain at core_runtime.c:138-143. NES MapScreenPosToPpuAddr. */
    const unsigned char y = (unsigned char)F_ram_0002_R;
    const unsigned char x = (unsigned char)F_ram_0003_R;
    F_ram_0000_R = (uint8_t)(0x20u | (y >> 6));
    F_ram_0001_R = (uint8_t)(((y << 2) & 0xE0u) | (x >> 3));
}

void core_update_person_state_reset_char_offset(void)
{
    /* drain at core_runtime.c:150-153. */
    F_ram_0416_R = 0u;  /* CAVE_TEXT_CHAR_INDEX */
    OBJ_STATE(1) = (uint8_t)(OBJ_STATE(1) + 1u);
}

void core_take_hearts_no_sound(void)
{
    /* drain at core_runtime.c:290-308. NES TakeHeartsNoSound.
     * Loop credit hearts, capped by container count. */
    F_ram_0001_R = (uint8_t)F_ZP_TMPA_R;
    for (;;) {
        /* NES JSR finishes its $00 write before CMP reads that scratch byte. */
        unsigned char whole_hearts = core_compare_hearts_to_containers();
        if (whole_hearts == (unsigned char)F_ram_0000_R) {
            unsigned char partial = LINK_PARTIAL_HEART;
            partial = (unsigned char)(partial + 1u);
            if (partial == 0u) {
                return;
            }
            LINK_PARTIAL_HEART = 0xFFu;
            return;
        }
        LINK_HEARTS = (uint8_t)(LINK_HEARTS + 1u);
        F_ram_0001_R = (uint8_t)(F_ram_0001_R - 1u);
        if ((signed char)F_ram_0001_R < 0) {
            return;
        }
    }
}

void core_take_hearts(void)
{
    /* drain at core_runtime.c:310-313. NES TakeHearts. */
    core_play_key_taken_tune();
    core_take_hearts_no_sound();
}

void core_set_shove_info_with0(unsigned int val, unsigned int slot)
{
    /* drain at core_runtime.c:332-335. */
    OBJ_SHOVE_DIR(slot)  = (uint8_t)val;
    OBJ_SHOVE_DIST(slot) = (uint8_t)val;
}

void core_reset_shove_info(unsigned int slot)
{
    /* drain at core_runtime.c:337-339. */
    core_set_shove_info_with0(0u, slot);
}

void core_reset_shove_info_and_inv_timer(unsigned int slot)
{
    /* drain at core_runtime.c:145-148. */
    core_set_shove_info_with0(0u, slot);
    OBJ_INV_TIMER(slot) = 0u;
}

void core_reset_obj_metastate(unsigned int slot)
{
    /* drain at core_runtime.c:341-343. */
    OBJ_METASTATE(slot) = 0u;
}

void core_reset_obj_metastate_and_timer(unsigned int slot)
{
    /* drain at core_runtime.c:345-348. */
    if (slot == 16u || slot == 17u)
        shared_effect_set_timer((unsigned char)slot, 0u);
    else
        F_ram_0028_R3(slot) = 0u;
    core_reset_obj_metastate(slot);
}

void core_decrement_invincibility_timer(unsigned int slot)
{
    /* drain at core_runtime.c:350+. */
    if (OBJ_INV_TIMER(slot) == 0u) {
        return;
    }
    OBJ_INV_TIMER(slot) = (uint8_t)(OBJ_INV_TIMER(slot) - 1u);
}

unsigned char core_reset_obj_state(unsigned int slot)
{
    /* drain at core_runtime.c:327-330. */
    if (slot == 0x0Fu) g_player_boomerang.state = 0u;
    if (slot == 16u || slot == 17u)
        shared_effect_set_state((unsigned char)slot, 0u);
    else
        OBJ_STATE(slot) = 0u;
    return 0u;
}

void core_deactivate_shot(unsigned int slot)
{
    /* drain at core_runtime.c:366-368. */
    (void)core_reset_obj_state(slot);
}

void core_deactivate_link_shot(void)
{
    /* drain at core_runtime.c:370-372. */
    (void)core_reset_obj_state(14u);
}

void core_set_type_and_clear_object(unsigned int type, unsigned int slot)
{
    /* drain at core_runtime.c:379-382. */
    OBJ_TYPE(slot) = (uint8_t)type;
    core_destroy_object_wram(0u, slot);
}

void core_init_tile_obj_or_item(unsigned int slot)
{
    /* drain at core_runtime.c:384-387. */
    F_ram_04BF_R2(slot) = 0x81u;
    core_reset_obj_metastate_and_timer(slot);
}

void core_init_flute_secret(unsigned int slot)
{
    /* drain at core_runtime.c:389-393. */
    F_ram_051A_R = 1u;             /* NES_ROOM_LAYOUT_SCRATCH */
    if (slot == 16u || slot == 17u)
        shared_effect_set_timer((unsigned char)slot, 0u);
    else
        F_ram_0028_R3(slot) = 0u;
    core_reset_obj_metastate(slot);
}

void core_ensure_object_aligned(unsigned int slot)
{
    /* drain at core_runtime.c:395-399. */
    if (F_ram_0394_R2(slot) != 0u) {  /* OBJ_ALIGN_FLAG(slot) */
        return;
    }
    OBJ_TILE_X(slot) = (uint8_t)(OBJ_TILE_X(slot) & 0xF8u);
    OBJ_TILE_Y(slot) =
        (uint8_t)((OBJ_TILE_Y(slot) & 0xF8u) | 0x05u);
}

void core_reverse_obj_dir(unsigned int slot)
{
    /* drain at core_runtime.c:401-406. NES ReverseObjDir.
     * OBJ_FLAG(slot) reads from NES_OBJ_FLAG_BASE; see object_state.h. */
    const unsigned char dir =
        (unsigned char)F_ram_0098_R4(slot);
    const unsigned char new_dir = (unsigned char)core_get_opposite_dir(dir);
    F_ram_0098_R4(slot) = new_dir;
    F_ram_000F_R3 = new_dir;
}

unsigned char core_reset_moving_dir(void)
{
    /* drain at core_runtime.c:408-411. */
    F_ram_000F_R3 = 0u;
    return 0u;
}

void core_do_nothing(void)
{
    /* drain at core_runtime.c:413. */
}

void core_update_dead_dummy(unsigned int slot)
{
    /* drain at core_runtime.c:356-359. */
    DEATH_FRAME_COUNTER = 32u;
    OBJ_METASTATE(slot) = 16u;
}

void core_set_shot_spreading_state(unsigned int slot)
{
    /* drain at core_runtime.c:361-364. */
    OBJ_STATE(slot) = (uint8_t)(OBJ_STATE(slot) + 1u);
    F_ram_0098_R4(slot) = 0xFEu;
}

void core_clear_ram0300_up_to(unsigned int end_hi, unsigned int start_off)
{
    /* drain at core_runtime.c:415-432. NES ClearRam0300UpTo. Backwards
     * clear of nes_ram[$0300+] until hi < $03. */
    unsigned char hi  = (unsigned char)end_hi;
    unsigned char off = (unsigned char)start_off;
    if (hi >= 0x03u) {
        /* Native counterpart of ObjPosFrac[$0E]. This is a movement clear,
         * unlike ResetInvObjState's state-only cleanup. Keep all owners in
         * the same room lifetime; do not import a stale legacy fraction. */
        if (hi > 0x03u || off >= 0xB6u) player_beam_clear_fractions();
        /* NES source: same ClearRam0300UpTo range. Drained C: this
         * boundary. Coverage: shared boomerang fraction at$03B7.
         * Stance: EXTEND native ownership; state-only resets retain it. */
        if (hi > 0x03u || off >= 0xB7u) g_player_boomerang.pos_frac = 0u;
        /* Same result as the byte loop below ($0300..end inclusive, then
         * the $0301 terminator), with long-word stores: the room-entry
         * clear of $0300-$051F is 544 bytes and the byte loop through the
         * volatile nes_ram pointer cost a lag frame on busy entries. */
        unsigned char *p = (unsigned char *)nes_ram; /* native_ram bulk:image_ptr */
        unsigned short i = 0x0300u;
        unsigned short n = (unsigned short)((((unsigned short)hi << 8) | off) + 1u);
        while ((i & 3u) != 0u && i < n) p[i++] = 0u;
        for (; (unsigned short)(i + 4u) <= n; i = (unsigned short)(i + 4u))
            *(unsigned long *)(p + i) = 0u;
        while (i < n) p[i++] = 0u;
        F_ram_0301_R3 = 0xFFu;
        return;
    }
    for (;;) {
        nes_ram[((unsigned short)hi << 8) | off] = 0u; /* native_ram bulk:dyn_addr */
        off = (unsigned char)(off - 1u);
        if (off != 0xFFu) {
            continue;
        }
        hi = (unsigned char)(hi - 1u);
        if (hi >= 0x03u) {
            off = 0xFFu;
            continue;
        }
        /* ROOM_TILE_XFER_BUF(0) = $FF — terminator. */
        F_ram_0301_R3 = 0xFFu;
        return;
    }
}

void core_handle_shot_blocked(unsigned int slot)
{
    /* drain at core_runtime.c:434-465. NES HandleShotBlocked.
     * STAGE-1: c_wield_candle path is a transpile shim; emit a TODO
     * for that arm. Other branches port natively. */
    if ((OBJ_STATE(slot) & 0x80u) == 0u) {
        core_set_shot_spreading_state(slot);
        return;
    }
    if (F_NES_INV_BOOK_R == 0u) {
        core_deactivate_shot(slot);
        return;
    }
    /* T-116 book fire: WieldCandle with UsedCandle cleared and Link's
     * state restored (both round-trip in NES), leaving X = the fire slot.
     * A moving fire ($21) becomes a standing one ($22) at the shot's
     * position and direction for $4F frames; the shot is deactivated
     * either way (DeactivateLinkShot). */
    {
        unsigned int x = candle_fire_wield_from_shot();
        if (OBJ_STATE(x) == 0x21u) {
            shared_effect_set_state((unsigned char)x, 0x22u);
            OBJ_TILE_X(x) = (uint8_t)OBJ_TILE_X(14);
            OBJ_TILE_Y(x) = (uint8_t)OBJ_TILE_Y(14);
            F_ram_0098_R4(x) =
                (uint8_t)F_ram_00A6_R;
            shared_effect_set_timer((unsigned char)x, 0x4Fu);
        }
    }
    core_deactivate_link_shot();
}

void core_set_up_common_cave_objects(unsigned int x, unsigned int slot,
                                     unsigned int y)
{
    /* drain at core_runtime.c:87-99. NES SetUpCommonCaveObjects (Z_01.asm).
     * Seeds the cave's 3 object slots (slot, slot+1, slot+2) with
     * tile-grid coords + walk frame state. */
    OBJ_TILE_X(slot) = (uint8_t)x;
    OBJ_TILE_Y(slot) = (uint8_t)y;
    F_ram_0485_R2(slot) = 0u;
    F_ram_04BF_R2(slot) = 0x81u;
    OBJ_STATE(0) = 64u;
    /* ROOM_OBJ_TYPE(slot) = RAM($0350 + slot). For NES InitCave path,
     * slots 1 and 2 are filled with type 64. */
    F_ram_0351_R3 = 64u;
    F_ram_0352_R2 = 64u;
    F_ENEMY_GLEEOK_BASE_X_R4(slot) = 72u;
    F_ram_0072_R3(slot) = 0xA8u;
    F_ENEMY_GLEEOK_BASE_Y_R4(slot) = (uint8_t)y;
    F_ram_0086_R3(slot) = (uint8_t)y;
}
