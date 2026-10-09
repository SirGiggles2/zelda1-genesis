/* Phase 7 Task 7.2 step 3 — wire first init dispatch row (slow octorok / ghini).
 *
 * Drain Rule D1 stance: EXTEND. Function-pointer tables are the dispatch
 * shape (verbatim NES InitObject_JumpTable at Z_07.asm:5601). Tasks 7.3-7.7
 * fill rows one family at a time, paired with the shim plumbing each
 * family needs.
 *
 * Step 3 wires INIT only for ENEMY_TYPE 0x07 (RedSlowOctorock / Ghini).
 * Update-side dispatch is still NULL — enrt_update_rope and friends call
 * c_walker_move / z07_anim_advance_and_fetch / c_check_monster_collisions
 * which are not yet linked into Debug.md. Update wiring lands in Task 7.3
 * paired with native drain or shim plumbing for those primitives. Per
 * Drain Rule D1 we route z07_reset_obj_state to the already-linked
 * core_reset_obj_state body in src/game/core/core_dispatch.c (drain at
 * src/core/core_runtime.c:327). Forwarder is one line, not a stub.
 *
 * Hard rule WT-5 (RoomRom freeze, 2026-05-09): this file lives at
 * src/game/enemies/ instead of RoomRom/src/. ENEMY_* macros are still
 * supplied by RoomRom/src/roomrom_enemy_state.h until Task 7.7 lands a
 * promoted version under src/state/.
 */

#include "enemy_loop.h"
#include "../room/room_dispatch.h"
#include "../cave/cave_dispatch.h"          /* cave_update_cave_person (T-133) */
#include "../core/core_dispatch.h"          /* ClearRam0300UpTo, SetTypeAndClearObject */
#include "../combat/collision_dispatch.h"  /* GetCollidableTileStill */
#include "enemy_dispatch.h"                /* FindEmptyMonsterSlot */
#include "roomrom_enemy_state.h"          /* still in RoomRom/src/ pre-WT-5 */
#include "platform_abi.h"
#include "probes/enemy_loop_probe.h"      /* step 4 live-tick publish */
#include "obj_lists.h"                    /* 7.7 step 1 room matrix loader */
#include "dungeon_state.h"                /* 7.7 step 2 DUNGEON_ROOM_* */
#include "bosses/boss_framework.h"        /* 8.1 step 3 room-item slot 19 */
#include "combat_state.h"                 /* MON_HP for HP init */
#include "../items/item_object.h"         /* Plan v5c $60 UpdateItem */
#include "../world/render/ow_render.h"      /* T-050 roomrom_ow_room_tile_object */
#include "../world/pushblock.h"             /* T-054 moving block sprite */
#include "../world/dock.h"                  /* T-056 UpdateDock */

/* NES Z_07.asm:5227 ObjectTypeToHpPairs — packed HP, 2 per byte.
 * Indexed by ObjType/2. Even-type uses high nibble (AND #$F0); odd-type
 * uses low nibble shifted to high (ASL #4). NES ExtractHitPointValue
 * at Z_04.asm:11035 produces an HP byte in the upper nibble form
 * (hearts * 16). combat_deal_damage compares raw byte against damage
 * (sword L1 = $10). Without this seed every spawned monster starts at
 * MON_HP=0 and dies/won't-die unpredictably. */
/* T-050: InitObject indexes past the 38-byte table for types >= $4C
 * (tile objects $62-$67 included) and reads the next ROM bytes, the start
 * of UpdateObject (PHA / LDA #$04 / JSR SwitchBank / PLA / LDY
 * ObjUninitialized,X / STY $0F / BEQ). Bytes copied from the ROM at file
 * offset $1FB84; live NES: tree $64 gets HP $F0. */
static const unsigned char k_object_hp_pairs[53] = {
    0x06, 0x43, 0x25, 0x31, 0x12, 0x24, 0x81, 0x14,
    0x22, 0x42, 0x00, 0xA9, 0x8F, 0x20, 0x00, 0x3F,
    0xF9, 0xFA, 0x46, 0x62, 0x11, 0x2F, 0xFF, 0xFF,
    0x7F, 0xF6, 0x2F, 0xFF, 0xFF, 0x22, 0x46, 0xF1,
    0xF2, 0xAA, 0xAA, 0xFB, 0xBF, 0xF0,
    0x48, 0xA9, 0x04, 0x20, 0xAC, 0xFF, 0x68, 0xBC,
    0x92, 0x04, 0x84, 0x0F, 0xF0, 0x1B,
    0xBD   /* T-171: types $68/$69 (USA ROM $1FB92; live NES block HP $B0) */
};

static void native_init_obj_hp(unsigned int slot, unsigned char type)
{
    const unsigned char pair_index = (unsigned char)(type >> 1);
    if (pair_index >= (unsigned char)sizeof(k_object_hp_pairs)) {
        MON_HP(slot) = 0u;
        return;
    }
    const unsigned char packed = k_object_hp_pairs[pair_index];
    if ((type & 1u) != 0u) {
        MON_HP(slot) = (unsigned char)((packed & 0x0Fu) << 4);
    } else {
        MON_HP(slot) = (unsigned char)(packed & 0xF0u);
    }
}

/* NES Z_07.asm:5213 ObjectTypeToAttributes — 95 bytes, indexed by
 * ObjType. NES InitObject @FetchAttrs (Z_07.asm:5566-5567):
 *   LDA ObjectTypeToAttributes,Y / STA ObjAttr,X
 * Sets ObjAttr at init so @LoopObject (Z_07.asm:1932-1940) can gate the
 * post-update draw + collision wrapper:
 *   - bit 0 = self-collide-and-draw (skip wrapper entirely)
 *   - bit 2 = self-draw (skip wrapper draw, still call collision)
 *
 * Pre-2026-05-18 the Genesis port never seeded this — ObjAttr stayed at
 * $00 for every walker, so today's @LoopObject draw-wrapper port fires
 * for octorok/stalfos/darknut (which already self-draw) → double-draw +
 * double-collide. Fix: port @FetchAttrs to all init paths. */
static const unsigned char k_object_type_attrs[95] = {
    0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05,
    0x05, 0x05, 0x05, 0x81, 0x81, 0x81, 0x81, 0x01,
    0x01, 0x81, 0x01, 0x01, 0x43, 0x43, 0x81, 0x81,
    0x81, 0x81, 0x01, 0x81, 0x81, 0x81, 0x01, 0x81,
    0x81, 0x81, 0x81, 0x81, 0x81, 0xC3, 0xC3, 0x89,
    0x89, 0x81, 0x81, 0x89, 0x89, 0x89, 0x89, 0x83,
    0x81, 0x89, 0x89, 0xC9, 0xC9, 0x81, 0x81, 0x81,
    0xA9, 0xA9, 0x41, 0x41, 0x89, 0x89, 0x81, 0x81,
    0x81, 0xC1, 0xC1, 0xC1, 0xC1, 0xC1, 0x81, 0x81,
    0x81, 0xA1, 0xA1, 0x81, 0x81, 0x81, 0x81, 0x81,
    0x81, 0x81, 0x81, 0xE3, 0xE3, 0xE3, 0xE3, 0xE3,
    0xE1, 0xE1, 0xE1, 0xE1, 0xE1, 0x81, 0x81
};

static void native_init_obj_attr(unsigned int slot, unsigned char type)
{
    if (type >= (unsigned char)sizeof(k_object_type_attrs)) return;
    RAM(0x04BFu + slot) = k_object_type_attrs[type];
}

/* Forward decls — defined in src/oracle/enemies/enemy_walker_runtime.c
 * (init), src/oracle/enemies/enemy_wanderer_runtime.c (goriya update),
 * src/game/enemies/enemy_walker_bridge.c (step-6 native octorock),
 * and src/game/core/core_dispatch.c (reset). All linked into Debug.md
 * per build_debug.py ROOMROM_C_SOURCES + step-6 unblock stubs. */
extern void enrt_init_slow_octorock_or_ghini(unsigned int slot);
extern void enrt_init_pond_fairy(unsigned int slot);             /* T-050 */
extern void enrt_update_pond_fairy(unsigned int slot);           /* T-050 */
extern void enrt_init_walker(unsigned int slot);                 /* step 7 */
extern void enrt_init_darknut(unsigned int slot);                /* step 7 */
extern void enrt_init_fast_octorock(unsigned int slot);          /* step 7 */
extern void enrt_update_octorock(unsigned int slot);  /* step 6 native */
extern void enrt_update_moblin(unsigned int slot);               /* step 7 */
extern void enrt_update_lynel(unsigned int slot);                /* enemy_fix Wave 2 */
extern void enrt_update_blue_wizzrobe(unsigned int slot);        /* enemy_fix Wave 5 */
extern void enrt_update_red_wizzrobe(unsigned int slot);         /* enemy_fix Wave 5 */
extern void uw_person_update_person_full(unsigned int slot);
extern void uw_person_update_grumble_full(unsigned int slot);
extern void roomrom_food_apply_chase_target(void);   /* items/boomerang.c */
extern void uw_person_update_life_or_money_full(unsigned int slot);
extern void uw_person_init_underworld_person_a(unsigned int slot);
extern void uw_person_init_underworld_person_b(unsigned int slot);
extern void uw_person_init_underworld_person_c(unsigned int slot);
extern void uw_person_init_life_or_money_full(unsigned int slot);
extern void uw_person_init_grumble_full(unsigned int slot);
extern void uw_person_init_rupee_stash_full(unsigned int slot);
extern void trap_update_rupee_stash_full(unsigned int slot);
extern void z07_update_dead_dummy(unsigned int slot);
extern void core_init_flute_secret(unsigned int slot);
extern void core_reset_obj_metastate(unsigned int slot);

/* NES Z_01.asm:1000 InitUnderworldPerson_Full dispatches by CurLevel.
 * Table from Z_01.asm:1007 InitUnderworldPerson_Full_JumpTable:
 *   L0=DoNothing, L1/L2/L5/L7=A, L3/L4/L6/L8=B, L9=C. */
static void uw_person_init_dispatch_by_level(unsigned int slot)
{
    unsigned char level = (unsigned char)RAM(0x0010u);  /* CUR_LEVEL */
    /* Z_01.asm:1003 preamble: PersonTextPtr ($045F) = TextboxLineAddrsLo+2
     * ($A4, front of the first line). T-166: missing, so the first line
     * streamed from whatever the pointer held (L1 old man: NT $2103+, the
     * room's top wall). */
    RAM(0x045Fu) = 0xA4u;
    switch (level) {
    case 1: case 2: case 5: case 7:
        uw_person_init_underworld_person_a(slot); break;
    case 3: case 4: case 6: case 8:
        uw_person_init_underworld_person_b(slot); break;
    case 9:
        uw_person_init_underworld_person_c(slot); break;
    default:
        break;
    }
}
extern void enrt_update_goriya(unsigned int slot);               /* step 7 */
extern void enrt_update_stalfos(unsigned int slot);              /* step 7 */
extern void enrt_update_darknut(unsigned int slot);              /* step 11 */
extern void enrt_update_monster_shot(unsigned int slot);         /* step 12 */
extern void enrt_update_fireball(unsigned int slot);             /* step 12 */
extern void enrt_update_monster_arrow(unsigned int slot);        /* B5 audit 2026-05-18 */
extern void enrt_update_arrow_or_boomerang(unsigned int slot);   /* B5 audit 2026-05-18 */
extern void update_meta_object(unsigned int slot);               /* step 20 */
extern void enrt_init_blue_keese(unsigned int slot);             /* step 3 */
extern void enrt_init_red_or_black_keese(unsigned int slot);     /* step 3 */
extern void enrt_update_keese(unsigned int slot);                /* step 3 */
extern void enrt_init_gel(unsigned int slot);                    /* step 4 */
extern void enrt_update_zol(unsigned int slot);                  /* step 4 */
extern void enrt_update_gel(unsigned int slot);                  /* step 4 */
extern void enrt_init_rope(unsigned int slot);                   /* step 5 */
extern void enrt_update_rope(unsigned int slot);                 /* step 5 */
extern void enrt_init_peahat(unsigned int slot);                 /* step 6 */
extern void enrt_update_peahat(unsigned int slot);               /* step 6 */
extern void enrt_update_vire(unsigned int slot);                 /* step 7 */
extern void enrt_init_boulder(unsigned int slot);                /* 7.4 step 2a */
extern void enrt_init_boulder_set(unsigned int slot);            /* 7.4 step 2a */
extern void enrt_init_monster_shot(unsigned int slot);           /* 2026-05-17 */
extern void enrt_init_monster_shot_unknown54(unsigned int slot); /* 2026-05-17 */
extern void enrt_update_boulder_set(unsigned int slot);          /* 7.4 step 2a */
extern void enrt_update_tektite_or_boulder(unsigned int slot);   /* 7.4 step 2a */
extern void enrt_update_zora(unsigned int slot);                 /* 7.4 step 2b */
extern void enrt_update_ghini(unsigned int slot);                /* 7.4 step 3 */
extern void enrt_update_flying_ghini(unsigned int slot);         /* 7.4 step 6a */
extern void enrt_update_armos(unsigned int slot);                /* 7.4 step 6b */
extern void enrt_update_gibdo(unsigned int slot);                /* 7.4 step 6d */
extern void enrt_update_bubble(unsigned int slot);               /* 7.4 step 6d */
extern void enrt_init_tektite(unsigned int slot);                /* 7.4 step 6e */
extern void enrt_init_bubble(unsigned int slot);                 /* 7.4 step 6e */
extern void enrt_init_armos_or_flying_ghini(unsigned int slot);  /* 7.4 step 6c */
/* 7.4 step 7 — whirlwind + trap UPDATE + INIT (trap_dispatch.c).
 * Native drains already linked into Debug.md; just need externs +
 * dispatch rows. NES SwitchBank #$01 collapses to no-op on Genesis. */
extern void trap_update_whirlwind_full(unsigned int slot);
extern void trap_update_trap_full(unsigned int slot);
extern void trap_init_trap_full(unsigned int slot);
/* 7.4 step 8 — fire-shooter UPDATE rows.
 *   $3F GuardFire    -> enrt_update_guard_fire (native NES Z_04.asm:9684
 *                       drain in enemy_walker_bridge.c).
 *   $40 StandingFire -> enrt_update_standing_fire (already drained at
 *                       enemy_walker_runtime.c:146). */
extern void enrt_update_guard_fire(unsigned int slot);
extern void enrt_init_zelda(unsigned int slot);
extern void enrt_update_zelda(unsigned int slot);
extern void enrt_update_standing_fire(unsigned int slot);
/* 7.4 step 10 — Aquamentus boss INIT + UPDATE.
 *   $3D Aquamentus -> enrt_init_aquamentus + enrt_update_aquamentus
 *                     (drained at enemy_boss_runtime.c:102/108).
 *   c_aquamentus_{move,shoot,draw} native bodies in enemy_boss_bridge.c.
 *   draw_write_boss_sprite primitive drained into draw_dispatch.c. */
extern void enrt_init_aquamentus(unsigned int slot);
extern void enrt_update_aquamentus(unsigned int slot);
/* Phase 8 Task 8.3 — Dodongo INIT + UPDATE.
 *   $31/$32 Dodongo -> enrt_init_dodongo + boss_dodongo_update
 *                     (native bridge in src/game/enemies/bosses/boss_dodongo.c).
 *   Bridge composes drained primitives (collisions / bomb-hit / draw /
 *   stunned / bloated_sub_die / bloated_sub_end) with native State0_Move +
 *   Bloated_Sub_Wait + state/bloated dispatchers. */
#include "bosses/boss_dodongo.h"
extern void enrt_init_dodongo(unsigned int slot);
/* Phase 8 Task 8.4 — Manhandla INIT + UPDATE.
 *   $3C Manhandla -> enrt_init_manhandla + enrt_update_manhandla
 *                    (drained at enemy_manhandla_runtime.c:20/44).
 *   Callee shims (c_turn_randomly_dir8 / c_play_boss_*_cry /
 *   c_draw_object_mirrored) live in src/game/enemies/bosses/boss_manhandla.c. */
#include "bosses/boss_manhandla.h"
extern void enrt_init_manhandla(unsigned int slot);
extern void enrt_update_manhandla(unsigned int slot);
/* Phase 8 Task 8.5 — Gleeok INIT + UPDATE ($42-$46).
 *   $42-$45 Gleeok 1-4-neck -> boss_gleeok_init (native InitGleeok body
 *                              at Z_04.asm:7649) + enrt_update_gleeok
 *                              (drained at enemy_gleeok_runtime.c).
 *   $46 GleeokHead         -> enrt_init_gleeok_head (drained) +
 *                              boss_gleeok_update_head (native body
 *                              at Z_04.asm:8527).
 *   8 c_gleeok_* primitives + native InitGleeok + UpdateGleeokHead live
 *   in src/game/enemies/bosses/boss_gleeok.c (EXTEND stance — drained
 *   per-segment helpers consumed verbatim). */
#include "bosses/boss_gleeok.h"
extern void enrt_init_gleeok_head(unsigned int slot);
extern void enrt_update_gleeok(unsigned int slot);
/* Phase 8 Task 8.6 — Digdogger INIT + UPDATE.
 *   $38 Digdogger1 / $39 Digdogger2 -> enrt_init_digdogger1/2 (drained
 *                                       at enemy_boss_runtime.c).
 *   $18 LittleDigdogger / $38 / $39 -> enrt_update_digdogger (drained at
 *                                       enemy_boss_runtime.c). Header
 *                                       documenting Rule D1 stance lives
 *                                       in src/game/enemies/bosses/boss_digdogger.h.
 *   No bridge .c — all callees (turn_towards_player8 / turn_randomly_dir8 /
 *   bound_flyer / check_monster_collisions / play_boss_death_cry /
 *   draw_object_mirrored / draw_object_not_mirrored / anim_advance_fetch /
 *   anim_fetch_obj_pos / anim_set_sprite_desc_attrs /
 *   anim_set_sprite_desc_level_palette_row) already resolved by existing
 *   bridges (boss_manhandla, flyer / jumper / projectile / common). */
#include "bosses/boss_digdogger.h"
extern void enrt_init_digdogger1(unsigned int slot);
extern void enrt_init_digdogger2(unsigned int slot);
extern void enrt_update_digdogger(unsigned int slot);
/* Phase 8 Task 8.7 — Gohma INIT + UPDATE.
 *   $33 BlueGohma / $34 RedGohma -> enrt_init_gohma (Z_04.asm:7814) +
 *                                    enrt_update_gohma (Z_04.asm:8207).
 *   Both drained in enemy_boss_runtime.c. Shims c_reverse_obj_dir8 +
 *   c_shoot_fireball + c_gohma_animate_and_draw + c_gohma_check_collisions
 *   already linked via c_shims.asm. Arrow-only damage gate lives in
 *   Gohma_HandleWeaponCollision (Z_01.asm) reachable through the asm
 *   collision shim. No bridge .c needed. */
#include "bosses/boss_gohma.h"
extern void enrt_init_gohma(unsigned int slot);
extern void enrt_update_gohma(unsigned int slot);
/* Phase 8 Task 8.8 — Patra INIT + UPDATE.
 *   $47 Patra1 / $48 Patra2 -> enrt_init_patra (Z_04.asm:9552) +
 *                              boss_patra_update orchestrator
 *                              (Z_04.asm:10070, bridge composes drained
 *                              flyer primitives + control_patra_flight
 *                              over states 0..3 with 2/3 routed through
 *                              c_control_keese_flight).
 *   $25 PatraChild1 / $26 PatraChild2 -> enrt_update_patra_child
 *                                        (Z_04.asm:10164). INIT for $25/$26
 *                                        is a no-op — patra children are
 *                                        seeded inside enrt_init_patra's
 *                                        slot-2..9 loop, not via the JT. */
#include "bosses/boss_patra.h"
extern void enrt_init_patra(unsigned int slot);
extern void enrt_update_patra_child(unsigned int slot);
/* Phase 8 Task 8.9 — Moldorm + Lamnola.
 *   $3A Lamnola1 / $3B Lamnola2 -> enrt_init_lamnola + enrt_update_lamnola
 *                                   (drained at enemy_lamnola_runtime.c).
 *   $41 Moldorm   -> enrt_init_moldorm + enrt_update_moldorm
 *                    (drained at enemy_moldorm_runtime.c — full
 *                    InitMoldorm + UpdateMoldorm + ControlMoldormFlight
 *                    + Moldorm_{Chase,Wander,ChangeFlyingState,
 *                    PropagateDirs}; head-only flight on slots 5/$A,
 *                    body segments tail-swap into $5D dead-dummy on
 *                    metastate). ADOPT stance — drain primitives reused
 *                    via c_flyer_chase / c_flyer_wander / c_move_flyer /
 *                    c_anim_write_sprite / c_check_monster_collisions /
 *                    enrt_check_boss_hit_reaction /
 *                    enrt_flyer_moldorm_decide_state. */
extern void enrt_init_lamnola(unsigned int slot);
extern void enrt_update_lamnola(unsigned int slot);
extern void enrt_init_moldorm(unsigned int slot);
extern void enrt_update_moldorm(unsigned int slot);
/* Phase 8 Task 8.10 — Ganon ($3E).
 *   $3E Ganon -> enrt_init_ganon + enrt_update_ganon
 *                (drained at enemy_ganon_runtime.c — full umbrella +
 *                ScenePhase0/1/2 + Ganon_Dying + DrawBody + DrawAshes +
 *                DrawCloud + DrawBurst + SetUpBurstRays + CheckCollisions
 *                + AppendPaletteRowTransferRecord_{Brown,Blue,Triforce}).
 *                PARTIAL stance — composes already-drained
 *                enrt_ganon_{randomize_location,activate_room_item,
 *                get_cur_cloud_*}, enrt_play_boss_hit_cry_if_needed,
 *                enrt_play_boss_death_cry, enrt_update_candle, plus
 *                core_reset_obj_metastate_and_timer / shove + collision
 *                primitives. STUBS for BlueWizzrobe family
 *                (TurnSometimesAndMoveAndCheckTile, Move) and PlaySample;
 *                slot ticks but Ganon stays stationary until Z_07
 *                wizzrobe drain lands. */
extern void enrt_init_ganon(unsigned int slot);
extern void enrt_update_ganon(unsigned int slot);
extern void core_reset_obj_metastate_and_timer(unsigned int slot); /* 7.4 step 2b ($11 INIT) */
extern unsigned char core_reset_obj_state(unsigned int slot);
/* 7.5 step 2 — special-enemy UPDATE bridge bodies (enemy_special_bridge.c).
 *   $17 LikeLike   -> enrt_update_like_like   (NES Z_04.asm:6818).
 *   $16 PolsVoice  -> enrt_update_pols_voice  (NES Z_04.asm:6533, step 3).
 *   $27 Wallmaster -> enrt_update_wallmaster  (NES Z_04.asm:4121, step 4). */
extern void enrt_update_like_like(unsigned int slot);
extern void enrt_update_pols_voice(unsigned int slot);
extern void enrt_update_wallmaster(unsigned int slot);
/* 7.6 step 1 — leever INIT ($0F BlueLeever / $10 RedLeever).
 * NES Z_07.asm:5617 InitObject_JumpTable rows $0F/$10 -> InitLeever.
 * Body drained at src/oracle/enemies/enemy_walker_runtime.c:37
 * (RedLeeverLongTimer=5 + z07_reset_obj_metastate_and_timer). */
extern void enrt_init_leever(unsigned int slot);
/* 7.6 step 2 — BlueLeever UPDATE bridge ($0F). NES UpdateBlueLeever
 * @ Z_04.asm:2599 — sets ObjTurnRate ($041F = ENEMY_AIR_SPEED) to
 * $A0, calls Wanderer_TargetPlayer, falls through to UpdateBurrower
 * (c_update_burrower already drained in enemy_jumper_bridge.c). */
extern void enrt_update_blue_leever(unsigned int slot);
/* 7.6 step 3 — RedLeever UPDATE bridge ($10). NES UpdateRedLeever
 * @ Z_04.asm:2737 — state-0 spawn-from-Link gate (RedLeeverLongTimer
 * + ActiveRedLeeverCount cap), state-3 shove/move/boundary cycle, fall
 * through to RedLeever_Animate via Burrower_AnimateDrawAndCheckCollisions
 * shared helper. Bridge body in enemy_jumper_bridge.c. */
extern void enrt_update_red_leever(unsigned int slot);

/* z07_reset_obj_state forwarder. enrt_octorock_common (same TU as the
 * init we wire below) calls this symbol. The drained body lives at
 * src/core/core_runtime.c:327 and was promoted into the native core
 * dispatch as core_reset_obj_state — call it directly. Avoids dragging
 * in src/gen/z_07.c (which would also pull room_runtime + core_runtime
 * + collision_runtime + their state header chains). Per --gc-sections
 * + -ffunction-sections, only this single forwarder is retained. */
unsigned char z07_reset_obj_state(unsigned int slot)
{
    return core_reset_obj_state(slot);
}

/* Function-pointer tables. Designated initializers leave unset rows at
 * NULL. Tasks 7.3-7.7 fan out by adding rows here without otherwise
 * modifying the file. */
const enemy_init_fn enemy_init_fns[ENEMY_LOOP_TYPE_MAX] = {
    /* NES InitObject_JumpTable @ Z_07.asm:5601.
     * $01-$06 = InitWalker (Lynel/Moblin/Goriya families).
     * $07/$09 = InitSlowOctorockOrGhini (sets WALK_SPEED=$20).
     * $08/$0A = InitFastOctorock (sets WALK_SPEED=$30).
     * $0B/$0C = InitDarknut (INVINCIBILITY $F6 + WALK_SPEED $20/$28).
     * $2A     = InitWalker (Stalfos).
     *
     * RedSlowOctorock body: enrt_octorock_common(slot, 32) →
     * enrt_init_walker(slot) — sets WALK_SPEED, MOVE_TIMER=(slot+1)<<4,
     * OBJ_STATE=0, DRAW_FRAME=0, ANIM_TIMER=6, then computes DIR from
     * CHASE_TARGET_X/CHASE_TARGET_Y vs OBJ_X/OBJ_Y (h_dir or v_dir, whichever has
     * larger diff). */
    [0x01] = enrt_init_walker,                 /* BlueLynel */
    [0x02] = enrt_init_walker,                 /* RedLynel */
    [0x03] = enrt_init_walker,                 /* BlueMoblin */
    [0x04] = enrt_init_walker,                 /* RedMoblin */
    [0x05] = enrt_init_walker,                 /* BlueGoriya */
    [0x06] = enrt_init_walker,                 /* RedGoriya */
    [0x07] = enrt_init_slow_octorock_or_ghini, /* RedSlowOctorock */
    [0x08] = enrt_init_fast_octorock,          /* RedFastOctorock */
    [0x09] = enrt_init_slow_octorock_or_ghini, /* BlueSlowOctorock */
    [0x0A] = enrt_init_fast_octorock,          /* BlueFastOctorock */
    [0x0B] = enrt_init_darknut,                /* BlueDarknut */
    [0x0C] = enrt_init_darknut,                /* RedDarknut */
    [0x2A] = enrt_init_walker,                 /* Stalfos */
    /* Task 7.3 step 3 — keese family INIT wired (NES Z_07.asm:5601). */
    [0x1B] = enrt_init_blue_keese,             /* BlueKeese */
    [0x1C] = enrt_init_red_or_black_keese,     /* RedKeese */
    [0x1D] = enrt_init_red_or_black_keese,     /* BlackKeese */
    /* Task 7.3 step 4 — zol/gel family INIT wired. NES InitObject_JumpTable
     * @ Z_07.asm:5601: $13/$14 = InitWalker (Zol/RedZol — bare walker init,
     * no special body); $15 = InitGel (sets STATE_TIMER=2 then InitWalker).
     * Bodies drained at enemy_walker_runtime.c:42 (InitWalker) and :92
     * (InitGel). */
    [0x13] = enrt_init_walker,                 /* Zol */
    [0x14] = enrt_init_walker,                 /* RedZol */
    [0x15] = enrt_init_gel,                    /* Gel */
    /* Task 7.3 step 5 — rope INIT (NES Z_07.asm:5601 InitObject_JumpTable
     * $28 = InitRope). Body drained at enemy_walker_runtime.c:71 — sets
     * CHARGE_SPEED=$10 (or $40 for second-quest) then InitWalker. */
    [0x28] = enrt_init_rope,                   /* Rope */
    /* Task 7.3 step 6 — peahat INIT (NES Z_07.asm:5601 $1A = InitPeahat).
     * Body drained at enemy_flyer_runtime.c:100 — z07_reset_obj_metastate
     * + DIR=$08 (down) + EndInitFlyer. */
    [0x1A] = enrt_init_peahat,                 /* Peahat */
    /* Task 7.3 step 7 — vire INIT (NES Z_07.asm:5601 InitObject_JumpTable
     * index $12 = InitWalker — vire shares the bare walker init body, no
     * vire-specific drain needed). enrt_init_walker drained at
     * enemy_walker_runtime.c:42. */
    [0x12] = enrt_init_walker,                 /* Vire */
    /* Task 7.4 step 2a — projectile-family carrier INIT rows (boulder
     * subset). NES Z_07.asm:5601 InitObject_JumpTable:
     *   $1F = InitBoulderSet (rock spawner) — drained at
     *         enemy_projectile_runtime.c:40.
     *   $20 = InitBoulder (rock projectile) — drained at
     *         enemy_projectile_runtime.c:35.
     * $11 Zora deferred to step 2b — needs UpdateBurrower drain chain. */
    [0x1F] = enrt_init_boulder_set,            /* BoulderSet (statue spawner) */
    [0x20] = enrt_init_boulder,                /* Boulder (rock projectile) */
    /* 2026-05-17 — monster-shot INIT wires. NES Z_07.asm:5685-5692
     * InitObject_JumpTable rows $53..$5A. c_shoot_if_wanted spawns a
     * shot slot but only sets type/dir/x/y/state; without per-type init
     * here, ObjQSpeed stays 0 and the shot never moves. Sets QSpeed=$C0
     * (3 px/frame) for the standard rock variants; $54 (boomerang-style
     * variant) uses $E0. Drains at enemy_projectile_runtime.c:30/37. */
    [0x53] = enrt_init_monster_shot,             /* FlyingRock (octorok) */
    [0x54] = enrt_init_monster_shot_unknown54,   /* Unknown54 variant */
    [0x55] = enrt_init_monster_shot,             /* MonsterShot 0x55 */
    [0x56] = enrt_init_monster_shot,             /* MonsterShot 0x56 */
    [0x57] = enrt_init_monster_shot,             /* SwordShot */
    [0x58] = enrt_init_monster_shot,             /* MagicShot */
    [0x59] = enrt_init_monster_shot,             /* MagicShot variant */
    [0x5A] = enrt_init_monster_shot,             /* MonsterShot 0x5A */
    /* T-012: NES rows $5B/$5C are ResetObjMetastate (the arrow and
     * boomerang set their own speed), $5D UpdateDeadDummy. */
    [0x5B] = core_reset_obj_metastate,           /* MonsterArrow */
    [0x5C] = core_reset_obj_metastate,           /* MonsterBoomerang */
    [0x5D] = z07_update_dead_dummy,              /* DeadDummy */
    /* Task 7.4 step 2b — $11 Zora INIT. NES InitObject_JumpTable @
     * Z_07.asm:5601 row $11 = ResetObjMetastateAndTimer. Body drained
     * at src/game/core/core_dispatch.c:455 (one-line: ENEMY_MOVE_TIMER=0
     * + core_reset_obj_metastate). */
    [0x11] = core_reset_obj_metastate_and_timer, /* Zora */
    /* Task 7.4 step 3 — $21 Ghini INIT shares InitSlowOctorockOrGhini
     * (NES Z_07.asm:5601 row $21). Body drained at
     * src/oracle/enemies/enemy_walker_runtime.c:34
     * (enrt_init_slow_octorock_or_ghini). Reused here. */
    [0x21] = enrt_init_slow_octorock_or_ghini,   /* Ghini */
    /* Task 7.4 step 6e — tektite + bubble INIT wires.
     *
     * NES Z_07.asm:5601 InitObject_JumpTable:
     *   $0D BlueTektite -> InitTektite.
     *   $0E RedTektite  -> InitTektite.
     *   $2B BlueBubble  -> InitBubble.
     *   $2C RedBubble   -> InitBubble.
     *   $2D BlueBubble2 -> InitBubble.
     *
     * enrt_init_tektite (boss_runtime.c:119): RNG_B & 3 -> dir lookup +
     *   MOVE_TIMER = dir << 2. TektiteStartingDirs lives in
     *   enemy_jumper_bridge.c (already linked).
     * enrt_init_bubble (walker_runtime.c:66): WALK_SPEED=64 + InitWalker
     *   (link-relative dir seeding). */
    [0x0D] = enrt_init_tektite,                  /* BlueTektite */
    [0x0E] = enrt_init_tektite,                  /* RedTektite */
    [0x2B] = enrt_init_bubble,                   /* BlueBubble */
    [0x2C] = enrt_init_bubble,                   /* RedBubble */
    [0x2D] = enrt_init_bubble,                   /* BlueBubble2 */
    /* Task 7.4 step 6c — armos + flying-ghini INIT wires (closes step 6).
     *
     * NES Z_07.asm:5601 InitObject_JumpTable:
     *   $1E Armos       -> InitArmosOrFlyingGhini (armos branch).
     *   $22 FlyingGhini -> InitArmosOrFlyingGhini (flying-ghini branch).
     *
     * Native body lives at src/game/enemies/enemy_walker_bridge.c
     * (enrt_init_armos_or_flying_ghini). Composes:
     *   - SecretArmosRoomIds + SecretArmosXs scan (7-entry table).
     *   - dyn_tile_change_tile_obj_tiles (src/game/world/dyn_tile_dispatch.c —
     *     native ChangeTileObjTiles drain unblocking step 6c).
     *   - progress_get_room_flag_uw_item_state (already drained).
     *   - enemy_play_secret_found_tune (already drained).
     *   - enrt_end_init_flyer (already drained, $22 path).
     *   - core_reset_obj_metastate_and_timer (already drained, $22 path).
     *   - armos_draw_and_check_collisions (enemy_walker_bridge.c, $1E path).
     *
     * Closes Task 7.4 step 6 family: $1E + $22 INIT now wired alongside
     * UPDATE rows ($1E from 6b, $22 from 6a). */
    [0x1E] = enrt_init_armos_or_flying_ghini,    /* Armos */
    [0x22] = enrt_init_armos_or_flying_ghini,    /* FlyingGhini */
    /* Additional INIT wires per NES Z_07.asm:5620 row $18 = InitWalker
     * (Digdogger child uses walker init). NES rows $23/$24 = ResetObj
     * MetastateAndTimer; same for $25/$26 PatraChild + $27 Wallmaster. */
    [0x18] = enrt_init_walker,                   /* DigdoggerChild */
    [0x5E] = core_init_flute_secret,             /* FluteSecret NES Z_07.asm:5817 */
    /* T-050: InitObject sends types >= $5F to InitTileObjOrItem. */
    [0x2F] = enrt_init_pond_fairy,               /* PondFairy (T-050) */
    [0x5F] = room_init_tile_obj_or_item,
    [0x60] = room_init_tile_obj_or_item,         /* DroppedItem (T-171: ObjTimer 0) */
    [0x61] = room_init_tile_obj_or_item,         /* Dock */
    [0x69] = room_init_tile_obj_or_item,
    [0x62] = room_init_tile_obj_or_item,         /* Rock */
    [0x63] = room_init_tile_obj_or_item,         /* RockWall */
    [0x64] = room_init_tile_obj_or_item,         /* Tree */
    [0x65] = room_init_tile_obj_or_item,         /* Gravestone */
    [0x66] = room_init_tile_obj_or_item,
    [0x67] = room_init_tile_obj_or_item,
    [0x68] = room_init_tile_obj_or_item,         /* UW push block (T-013) */
    [0x23] = core_reset_obj_metastate_and_timer, /* BlueWizzrobe init */
    [0x24] = core_reset_obj_metastate_and_timer, /* RedWizzrobe init */
    [0x25] = core_reset_obj_metastate_and_timer, /* PatraChild1 init */
    [0x26] = core_reset_obj_metastate_and_timer, /* PatraChild2 init */
    /* UW NPC INIT rows — NES Z_07.asm:5655-5684 InitObject_JumpTable.
     * Bodies at src/game/cave/uw_person_dispatch.c. */
    [0x35] = uw_person_init_rupee_stash_full,    /* RupeeStash */
    [0x36] = uw_person_init_grumble_full,        /* Grumble */
    [0x37] = enrt_init_zelda,                     /* Zelda / guard fires */
    [0x4B] = uw_person_init_dispatch_by_level,   /* UnderworldPerson */
    [0x4C] = uw_person_init_dispatch_by_level,
    [0x4D] = uw_person_init_dispatch_by_level,
    [0x4E] = uw_person_init_dispatch_by_level,
    [0x4F] = uw_person_init_dispatch_by_level,
    [0x50] = uw_person_init_dispatch_by_level,
    [0x51] = uw_person_init_life_or_money_full,  /* LifeOrMoneyDoor */
    [0x52] = uw_person_init_dispatch_by_level,
    /* Task 7.4 step 7 — trap INIT wires.
     *
     * NES Z_07.asm:5601 InitObject_JumpTable rows:
     *   $49 Trap -> InitTrap (Z_07.asm:5712 -> InitTrap_Full Z_01.asm).
     *   $4A Trap -> InitTrap.
     *   $2E Whirlwind -> DoNothing (no init body — NULL).
     *
     * Native body trap_init_trap_full @ src/game/world/trap_dispatch.c
     * (drained from src/oracle/world/trap_runtime.c:4). Spawns a
     * 4 / 6-trap cluster from TrapXs/TrapYs into TRAP_BASE_SLOT+i.
     * Composes core_init_one_simple_object (already drained). */
    [0x49] = trap_init_trap_full,                /* Trap */
    [0x4A] = trap_init_trap_full,                /* Trap (alt) */
    /* Task 7.4 step 9 — gibdo INIT pair-close.
     *
     * NES Z_07.asm:5601 InitObject_JumpTable row $30 Gibdo -> InitWalker
     * (bare walker init). $30 UPDATE was wired in step 6d
     * (enrt_update_gibdo); this row closes the pair so freshly-spawned
     * gibdos enter the walker init seed (DIR/MOVE_TIMER/anim defaults)
     * before the dispatch table picks up update side. */
    [0x30] = enrt_init_walker,                   /* Gibdo */
    /* Task 7.4 step 10 — Aquamentus INIT.
     *
     * NES Z_07.asm:5601 InitObject_JumpTable row $3D Aquamentus ->
     * SwitchBank #$01 + JMP InitAquamentus @ Z_04.asm region (sets
     * INVINCIBILITY=$E2, SFX_BOSS_CRY=16, X=$B0, Y=$80). Drained at
     * enemy_boss_runtime.c:102. Primitives self-contained — only writes
     * 4 RAM cells, no callouts. */
    [0x3D] = enrt_init_aquamentus,               /* Aquamentus */
    /* Phase 8 Task 8.3 — Dodongo INIT pair (NES Z_07.asm:5601 rows
     * $31/$32 -> SwitchBank #$01 + JMP InitDodongo @ Z_04.asm:4893).
     * Drained at enemy_dodongo_runtime.c:21 — sets ENEMY_SFX_BOSS_CRY=32
     * + ENEMY_DIR via RNG_A < $80. */
    [0x31] = enrt_init_dodongo,                  /* Dodongo */
    [0x32] = enrt_init_dodongo,                  /* Dodongo (alt) */
    /* Phase 8 Task 8.4 — Manhandla INIT (NES Z_07.asm:5601 row $3C ->
     * SwitchBank #$01 + JMP InitManhandla @ Z_04.asm:7747). Drained at
     * enemy_manhandla_runtime.c:20 — seeds 5-segment row at $0099+, sets
     * SFX_BOSS_CRY=64, picks random Directions8 dir, fans X/Y/Speed/
     * FrameAttr offsets per-segment. ADOPT — drain consumed verbatim. */
    [0x3C] = enrt_init_manhandla,                /* Manhandla */
    /* Phase 8 Task 8.5 — Gleeok INIT rows ($42-$46).
     * NES Z_07.asm:5601 InitObject_JumpTable rows $42/$43/$44/$45 ->
     * SwitchBank #$01 + JMP InitGleeok @ Z_04.asm:7649. Native body in
     * src/game/enemies/bosses/boss_gleeok.c (boss_gleeok_init) — seeds
     * 4 necks * 6 segments per Gleeok_NeckXs/Ys, head info, body anim
     * frame; per-line port of NES InitGleeok.
     * Row $46 GleeokHead -> enrt_init_gleeok_head (drained at
     * enemy_gleeok_runtime.c) — neck terminal flying head spawn. */
    [0x42] = boss_gleeok_init,                   /* Gleeok 1-neck */
    [0x43] = boss_gleeok_init,                   /* Gleeok 2-neck */
    [0x44] = boss_gleeok_init,                   /* Gleeok 3-neck */
    [0x45] = boss_gleeok_init,                   /* Gleeok 4-neck */
    [0x46] = enrt_init_gleeok_head,              /* GleeokHead (flying) */
    /* Phase 8 Task 8.6 — Digdogger INIT pair (NES Z_07.asm:5658/5659 rows
     * $38/$39 -> InitDigdogger1/InitDigdogger2 @ Z_04.asm:4860+). Drained
     * at enemy_boss_runtime.c:458/468 (already shipped pre-task). $18
     * LittleDigdogger has no init row — children spawn dynamically via
     * enrt_init_digdogger1 inside the drained make_children helper. */
    [0x38] = enrt_init_digdogger1,               /* Digdogger1 */
    [0x39] = enrt_init_digdogger2,               /* Digdogger2 (2nd quest) */
    /* Phase 8 Task 8.7 — Gohma INIT pair (NES Z_07.asm:5653/5654 rows
     * $33/$34 -> InitGohma @ Z_04.asm:7814). Drained at
     * enemy_boss_runtime.c:426 — sfx $20 + INVINCIBILITY=$FB +
     * BOSS_HP_PHASE++ (aliases SHOOT_TIMER=1) + X=$80 / Y=$70 +
     * ResetObjMetastateAndTimer. ADOPT stance. */
    [0x33] = enrt_init_gohma,                    /* Blue Gohma */
    [0x34] = enrt_init_gohma,                    /* Red Gohma */
    /* Phase 8 Task 8.8 — Patra INIT pair (NES Z_07.asm rows $47/$48 ->
     * InitPatra @ Z_04.asm:9552). Drained at enemy_patra_runtime.c —
     * INVINCIBILITY=$FE + X=$80/Y=$70/Dir=$08 + Flyer_ObjSpeed=$1F +
     * FlyingMaxSpeedFrac=$40 + roar SFX + ObjTimer+1=$FF + slot 2..9
     * child seeding (type $25 for Patra1, $26 for Patra2). $25/$26
     * children have no INIT row — they're spawned by enrt_init_patra
     * itself. ADOPT stance. */
    [0x47] = enrt_init_patra,                    /* Patra1 */
    [0x48] = enrt_init_patra,                    /* Patra2 */
    /* Phase 8 Task 8.9 — Lamnola + Moldorm INIT rows.
     * NES Z_07.asm:5601 InitObject_JumpTable rows:
     *   $3A Lamnola1 -> InitLamnola (Z_04.asm:9502).
     *   $3B Lamnola2 -> InitLamnola (Z_04.asm:9502).
     *   $41 Moldorm  -> InitMoldorm (Z_04.asm:4763).
     * Drained body for Lamnola at enemy_lamnola_runtime.c:14;
     * Moldorm at enemy_moldorm_runtime.c:128. ADOPT stance — drain
     * consumed verbatim, no bridge layer required. */
    [0x3A] = enrt_init_lamnola,                  /* Lamnola1 */
    [0x3B] = enrt_init_lamnola,                  /* Lamnola2 */
    [0x3E] = enrt_init_ganon,                    /* Ganon */
    [0x41] = enrt_init_moldorm,                  /* Moldorm */
    /* Task 7.4 step 11 — projectile-family INIT close.
     *
     * NES Z_07.asm:5601 InitObject_JumpTable rows:
     *   $11 Zora  -> ResetObjMetastateAndTimer (already drained as
     *                core_reset_obj_metastate_and_timer; extern'd
     *                since 7.4 step 2b for use by other rows).
     *   $21 Ghini -> InitSlowOctorockOrGhini (already extern'd +
     *                wired for $07/$09; reused here per NES table). */
    [0x11] = core_reset_obj_metastate_and_timer, /* Zora */
    [0x21] = enrt_init_slow_octorock_or_ghini,   /* Ghini */
    /* Task 7.5 step 1 — special-enemy INIT wins.
     *
     * NES Z_07.asm:5601 InitObject_JumpTable rows:
     *   $16 PolsVoice  -> InitWalker (bare walker init seed; full
     *                     pols-voice spawn state set by UpdatePolsVoice
     *                     state-1 path).
     *   $17 LikeLike   -> InitWalker (bare walker init seed; like-like
     *                     state machine driven entirely by UpdateLikeLike).
     *   $27 Wallmaster -> ResetObjMetastateAndTimer (state machine
     *                     entered from State 0; UPDATE drives the rest).
     *
     * All three INIT bodies are already drained + extern'd above
     * (enrt_init_walker for $16/$17, core_reset_obj_metastate_and_timer
     * for $27). Stance: ADOPT — single-row table-deltas, no bridges. */
    [0x16] = enrt_init_walker,                   /* PolsVoice */
    [0x17] = enrt_init_walker,                   /* LikeLike */
    [0x27] = core_reset_obj_metastate_and_timer, /* Wallmaster */
    /* Task 7.6 step 1 — leever INIT pair (NES Z_07.asm:5617 rows $0F/$10
     * both -> InitLeever). Body drained at enemy_walker_runtime.c:37 —
     * sets RedLeeverLongTimer=5 then z07_reset_obj_metastate_and_timer.
     * Body-shared between BlueLeever and RedLeever per NES asm. ADOPT
     * stance — drained twin reused, no bridge edits. */
    [0x0F] = enrt_init_leever,                   /* BlueLeever */
    [0x10] = enrt_init_leever,                   /* RedLeever */
};

const enemy_update_fn enemy_update_fns[ENEMY_LOOP_TYPE_MAX] = {
    /* NES UpdateObject_JumpTable @ Z_04.asm:5295.
     * $03/$04 = UpdateMoblin (turn rate $A0 + Wanderer + _TryShoot $5B).
     * $05/$06 = UpdateGoriya (Wanderer + boomerang $5C try).
     * $07-$0A = UpdateOctorock (this file, native, step 6).
     * $2A     = UpdateStalfos (Wanderer + animate-and-draw + sword $57).
     *
     * Step 6 fixed $07 = RedSlowOctorock semantically (was reusing
     * enrt_update_rope which is actually $29). Step 7 wires the rest of
     * the walker family using already-drained UPDATE bodies in
     * src/oracle/enemies/{enemy_walker,enemy_wanderer}_runtime.c.
     *
     * NOTE: enrt_update_moblin is intentionally bare in the drain
     * (no anim/draw/collision tail) — NES UpdateMoblin tail-jumps to
     * _TryShooting which returns directly to the dispatch caller. The
     * NES dispatcher (UpdateObject) handles post-call animate/draw
     * centrally. Our enemy_loop_tick does not yet have a central
     * post-dispatch animate/draw, so moblin walks but does not draw
     * sprites until that hook lands (step 8 / next task).
     *
     * Future rows ($01/$02 lynel, $0B/$0C darknut, $29 rope) need new
     * drain bodies (no _runtime.c entry yet) and will land in step 8+. */
    [0x01] = enrt_update_lynel,     /* RedLynel — Goriya AI + sword shot $57 */
    [0x02] = enrt_update_lynel,     /* BlueLynel */
    [0x23] = enrt_update_blue_wizzrobe,  /* BlueWizzrobe — Z_04.asm:7034 */
    [0x24] = enrt_update_red_wizzrobe,   /* RedWizzrobe  — Z_04.asm:7474 */
    /* UW NPC dispatch — NES Z_07.asm:5371-5378 UpdateObject_JumpTable
     * rows $4B..$50, $52 = UpdateUnderworldPerson; $51 =
     * UpdateUnderworldPersonLifeOrMoney; $36 = UpdateGrumble.
     * Bodies live at src/game/cave/uw_person_dispatch.c. Unblocked by
     * TextboxCharTransferRecTemplate + PersonTextAddrs[] data ports
     * (cave_dispatch.c + src/data/person_text_data.c). */
    [0x35] = trap_update_rupee_stash_full,   /* RupeeStash NES Z_01.asm:2584 */
    [0x5D] = z07_update_dead_dummy,          /* DeadDummy NES Z_07.asm:5389 */
    [0x36] = uw_person_update_grumble_full,
    [0x4B] = uw_person_update_person_full,
    [0x4C] = uw_person_update_person_full,
    [0x4D] = uw_person_update_person_full,
    [0x4E] = uw_person_update_person_full,
    [0x4F] = uw_person_update_person_full,
    [0x50] = uw_person_update_person_full,
    [0x51] = uw_person_update_life_or_money_full,
    [0x52] = uw_person_update_person_full,
    [0x03] = enrt_update_moblin,    /* BlueMoblin */
    [0x04] = enrt_update_moblin,    /* RedMoblin */
    [0x05] = enrt_update_goriya,    /* BlueGoriya (drained, full body) */
    [0x06] = enrt_update_goriya,    /* RedGoriya */
    [0x07] = enrt_update_octorock,  /* RedSlowOctorock (step 6 native) */
    [0x08] = enrt_update_octorock,  /* RedFastOctorock — color/qspeed branch in body */
    [0x09] = enrt_update_octorock,  /* BlueSlowOctorock */
    [0x0A] = enrt_update_octorock,  /* BlueFastOctorock */
    [0x0B] = enrt_update_darknut,   /* BlueDarknut (step 11 native drain) */
    [0x0C] = enrt_update_darknut,   /* RedDarknut */
    [0x2A] = enrt_update_stalfos,   /* Stalfos (drained, full body) */
    /* Task 7.3 step 3 — keese family UPDATE wired (NES Z_04.asm:5295
     * UpdateObject_JumpTable). enrt_update_keese consumes flyer
     * primitives now resolved by src/game/enemies/enemy_flyer_bridge.c
     * (Directions8, c_move_flyer, c_control_keese_flight,
     * c_reset_shove_info, c_draw_object_mirrored_with_frame). */
    [0x1B] = enrt_update_keese,     /* BlueKeese */
    [0x1C] = enrt_update_keese,     /* RedKeese */
    [0x1D] = enrt_update_keese,     /* BlackKeese */
    /* Task 7.3 step 4 — zol/gel family UPDATE wired (NES Z_04.asm:5295).
     * NES UpdateObject_JumpTable: $13 = UpdateZol; $14 RedZol uses
     * UpdateGel (alias); $15 = UpdateGel. Bodies drained at
     * enemy_walker_runtime.c:156/163. Primitives resolved by
     * src/game/enemies/enemy_common_bridge.c (c_update_zol_state,
     * c_zol_check_collisions, c_gel_move, c_gel_check_collisions, plus
     * native c_shoot_limited drain for Zol state-2 split path). */
    [0x13] = enrt_update_zol,       /* Zol */
    [0x14] = enrt_update_gel,       /* RedZol — NES alias to UpdateGel */
    [0x15] = enrt_update_gel,       /* Gel */
    /* Task 7.3 step 5 — rope UPDATE (NES Z_04.asm:5295 $28 = UpdateRope).
     * Body drained at enemy_walker_runtime.c:97 — full leever-style
     * speed-ramp + dir-switch behavior. All primitives (c_walker_move,
     * c_draw_object_not_mirrored_with_frame, c_check_monster_collisions,
     * z07_anim_advance_and_fetch, z01_anim_set_sprite_desc_attrs, z01_abs)
     * already resolved by enemy_walker_bridge.c (Task 7.2 step 4..18). */
    [0x28] = enrt_update_rope,      /* Rope */
    /* Task 7.3 step 6 — peahat UPDATE (NES Z_04.asm:4014 UpdatePeahat).
     * Native drain in enemy_flyer_bridge.c — composes c_obj_shove,
     * c_control_peahat_flight (state-1 = enrt_flyer_peahat_decide_state,
     * other states share keese rows), c_move_flyer, draw + collisions. */
    [0x1A] = enrt_update_peahat,    /* Peahat */
    /* Task 7.3 step 7 — vire UPDATE wired (NES Z_04.asm:5295 $12 UpdateVire).
     * Drained body at enemy_boss_runtime.c:339 — composes
     * enrt_update_vire_state (state machine + jump-offset table +
     * c_gel_move_splitting + z04_update_common_wanderer),
     * enrt_check_vire_collisions, enrt_draw_vire, plus shoot/destroy
     * primitives. Bridge primitives carried in enemy_boss_bridge.c. */
    [0x12] = enrt_update_vire,      /* Vire */

    /* Step 12: shot UPDATE rows (Z_07.asm:5379-5388 dispatch).
     * NES UpdateMonsterShot (Z_04.asm:820) covers $53/$54 flying-rocks +
     * $57/$58/$59/$5A sword/magic/boomerang shots.
     * NES UpdateFireball (Z_04.asm offsets) covers $55/$56 fireballs.
     * Step 12 also fixed drain bug — enrt_update_monster_shot now falls
     * through to enrt_draw_shot (NES L_DrawShot label).
     * UpdateMonsterArrow ($5B) + UpdateArrowOrBoomerang ($5C) have
     * separate NES bodies — NOT yet drained, rows left NULL. */
    [0x53] = enrt_update_monster_shot,  /* FlyingRock (octorok shot) */
    [0x54] = enrt_update_monster_shot,  /* (alt rock) */
    [0x55] = enrt_update_fireball,      /* Fireball */
    [0x56] = enrt_update_fireball,      /* Fireball2 */
    [0x57] = enrt_update_monster_shot,  /* SwordShot */
    [0x58] = enrt_update_monster_shot,  /* MagicShot */
    [0x59] = enrt_update_monster_shot,  /* (shot variant) */
    [0x5A] = enrt_update_monster_shot,  /* (shot variant) */
    /* B5 projectile drain: $5B uses the NES arrow-specific MoveShot,
     * spark and Link-hit path; $5C uses the boomerang return state machine.
     * Both share UpdateArrowOrBoomerang primitives, but differ on blocked
     * movement, range handling and collision outcomes. */
    [0x5B] = enrt_update_monster_arrow,       /* MonsterArrow */
    [0x5C] = enrt_update_arrow_or_boomerang,  /* ArrowOrBoomerang */
    /* Task 7.4 step 2a — projectile-family carrier UPDATE rows (boulder
     * subset). NES Z_07.asm:5295 UpdateObject_JumpTable:
     *   $1F = UpdateBoulderSet  - enemy_projectile_runtime.c:98
     *   $20 = UpdateTektiteOrBoulder (Boulder branch)
     *                            - enemy_boss_runtime.c:126
     * $11 Zora deferred to step 2b — needs UpdateBurrower drain chain. */
    [0x1F] = enrt_update_boulder_set,       /* BoulderSet */
    [0x20] = enrt_update_tektite_or_boulder,/* Boulder */
    /* Task 7.4 step 2b — $11 Zora UPDATE. NES UpdateObject_JumpTable @
     * Z_04.asm:5295 row $11 = UpdateZora (Z_04.asm:1920). Drain at
     * src/oracle/enemies/enemy_walker_runtime.c:174. Native
     * c_update_burrower body in enemy_jumper_bridge.c (step 2b). */
    [0x11] = enrt_update_zora,              /* Zora */
    /* Task 7.4 step 3 — $21 Ghini UPDATE (NES Z_07.asm:5295 row $21 =
     * UpdateGhini @ Z_04.asm:3067). Drain at
     * src/oracle/enemies/enemy_walker_runtime.c:313 — composes
     * enrt_update_common_wanderer($FF), enrt_draw_ghini_and_check_collisions,
     * c_check_monster_collisions, plus per-slot loop killing $22 flying
     * ghini when ghini dies. All primitives already linked. */
    [0x21] = enrt_update_ghini,             /* Ghini */
    /* Task 7.4 step 6a — $22 FlyingGhini UPDATE (NES Z_04.asm:3967
     * UpdateFlyingGhini). Native drain in enemy_flyer_bridge.c —
     * composes c_control_flying_ghini_flight (6-row dispatch with
     * enrt_flyer_ghini_decide_state at state-1, flyer_chase/wander
     * shared with keese/peahat), c_move_flyer, and the existing
     * enrt_draw_ghini_and_check_collisions tail (walker_runtime.c:295).
     *
     * INIT row deferred to step 6b — InitArmosOrFlyingGhini body is
     * shared with $1E armos and reaches into ChangeTileObjTiles +
     * SecretArmosRoomIds + GetRoomFlagUWItemState which require their
     * own native drains (c_change_tile_obj_tiles only resolves via
     * c_shims.asm bank, not linked into Debug.md). */
    [0x22] = enrt_update_flying_ghini,      /* FlyingGhini */
    /* Task 7.4 step 6b — $1E Armos UPDATE (NES Z_04.asm:3302 UpdateArmos).
     * Native drain in enemy_walker_bridge.c — composes enrt_update_goriya
     * (already drained, type-$1E specialization at wanderer_runtime.c:176)
     * + ObjShoveDir / ObjAnimCounter gates + DrawArmosAndCheckCollisions
     * (sprite_anim_advance_and_fetch + dir-keyed frame select +
     * link/monster collision tail with $5D dead-dummy convert on death).
     *
     * INIT row $1E + $22 still deferred — InitArmosOrFlyingGhini reaches
     * ChangeTileObjTiles + secret-armos table not yet drained. */
    [0x1E] = enrt_update_armos,             /* Armos */
    /* Task 7.4 step 6d — bubble + gibdo UPDATE wires.
     *
     * NES Z_07.asm:5295 UpdateObject_JumpTable rows:
     *   $2B BlueBubble  -> UpdateBubble (Z_04.asm:bubble entry).
     *   $2C RedBubble   -> UpdateBubble.
     *   $2D BlueBubble2 -> UpdateBubble.
     *   $30 Gibdo       -> UpdateGibdo (Z_04.asm:7012 region).
     *
     * Both drained: enemy_walker_runtime.c:14 (bubble) and
     * enemy_common_runtime.c:22 (gibdo). All composed primitives
     * already linked into Debug.md:
     *   - wanderer_update_common / enrt_update_common_wanderer
     *     (enemy_wanderer_runtime.c — already used by ghini/moblin).
     *   - z01_anim_set_sprite_desc_attrs / enrt_animate_and_draw_common_object
     *     (enemy_walker_bridge.c:182/195/206).
     *   - z01_check_link_collision (enemy_projectile_bridge.c:53).
     *   - c_check_monster_collisions (link_collision_dispatch.c).
     *   - c_draw_object_not_mirrored_with_frame (draw_dispatch.c).
     *   - z07_anim_set_obj_hflip (enemy_walker_bridge.c:187). */
    [0x2B] = enrt_update_bubble,            /* BlueBubble */
    [0x2C] = enrt_update_bubble,            /* RedBubble */
    [0x2D] = enrt_update_bubble,            /* BlueBubble2 */
    [0x30] = enrt_update_gibdo,             /* Gibdo */
    /* Task 7.4 step 6e — tektite UPDATE rows.
     *
     * NES Z_07.asm:5295 UpdateObject_JumpTable rows:
     *   $0D BlueTektite -> UpdateTektiteOrBoulder (Tektite branch).
     *   $0E RedTektite  -> UpdateTektiteOrBoulder.
     *
     * Body drained at enemy_boss_runtime.c:126 (already wired for $20
     * Boulder in step 2a). Type-keyed branches at enemy_boss_runtime.c:218
     * ($20 boulder skips reversal-timer randomization) and :228
     * ($0D blue-tektite skips the &$7F mask in the post-land timer
     * randomization). */
    [0x0D] = enrt_update_tektite_or_boulder, /* BlueTektite */
    [0x0E] = enrt_update_tektite_or_boulder, /* RedTektite */
    /* Task 7.4 step 7 — whirlwind + trap UPDATE wires.
     *
     * NES Z_07.asm:5295 UpdateObject_JumpTable rows:
     *   $2E Whirlwind -> UpdateWhirlwind (SwitchBank #$01 + JMP
     *                    UpdateWhirlwind_Full Z_01.asm:1765).
     *   $49 Trap      -> UpdateTrap (SwitchBank #$01 + JMP
     *                    UpdateTrap_Full Z_01.asm:2434).
     *   $4A Trap      -> UpdateTrap.
     *
     * Native bodies already drained + linked via
     * src/game/world/trap_dispatch.c (trap_update_whirlwind_full /
     * trap_update_trap_full). All composed primitives resolved:
     *   - core_set_up_whirlwind, core_destroy_whirlwind,
     *     core_init_one_simple_object, core_get_opposite_dir,
     *     core_anim_set_sprite_desc_attrs (core_dispatch.c).
     *   - link_collision_check_link_collision (link_collision_dispatch.c).
     *   - sprite_anim_advance_and_fetch / sprite_anim_set_obj_hflip
     *     / sprite_anim_fetch_obj_pos (sprite_dispatch.c).
     *   - draw_object_not_mirrored_with_frame (draw_dispatch.c).
     *   - room_go_to_next_mode_from_play (room_dispatch.c).
     *   - uw_person_person_draw_and_check_collisions (uw_person_dispatch.c).
     *
     * SwitchBank #$01 collapses to no-op (single linear address space). */
    [0x2E] = trap_update_whirlwind_full,    /* Whirlwind */
    [0x49] = trap_update_trap_full,         /* Trap */
    [0x4A] = trap_update_trap_full,         /* Trap (alt) */
    /* Task 7.4 step 8 — fire-shooter UPDATE wires.
     *
     * NES Z_07.asm:5295 UpdateObject_JumpTable rows:
     *   $3F GuardFire    -> UpdateGuardFire (Z_04.asm:9684).
     *   $40 StandingFire -> UpdateStandingFire (Z_04.asm:257).
     *
     * UpdateGuardFire body (6 instructions): rate-6 animate-and-draw
     * + monster collisions + DeadDummy convert on kill (metastate
     * non-zero). Native drain in enemy_walker_bridge.c
     * (enrt_update_guard_fire). No INIT row — both default to bare
     * scratch init (NES Z_07.asm:5601 rows $3F/$40 not in the
     * InitObject_JumpTable's specialized list).
     *
     * UpdateStandingFire body: c_check_link_collision + palette=2 +
     * DIR=8 + animate-walking + (FRAME_FLAGS=0 if type != $40) +
     * draw_not_mirrored. Drain at enemy_walker_runtime.c:146.
     * Primitives all linked via walker_bridge / projectile_bridge
     * (z07_animate_object_walking forwarder added in step 8). */
    [0x3F] = enrt_update_guard_fire,        /* GuardFire (native EXTEND drain) */
    [0x37] = enrt_update_zelda,             /* Zelda rescue trigger */
    [0x40] = enrt_update_standing_fire,     /* StandingFire (oracle drain) */
    /* Task 7.4 step 10 — Aquamentus UPDATE.
     *
     * NES Z_07.asm:5295 UpdateObject_JumpTable row $3D Aquamentus ->
     * UpdateAquamentus (Z_04.asm:5594-5607). Drained at
     * enemy_boss_runtime.c:108 — composes c_aquamentus_{move,shoot,draw}
     * + c_check_monster_collisions + enrt_play_boss_hit_cry_if_needed.
     *
     * Native bridge bodies for c_aquamentus_{move,shoot,draw} added in
     * enemy_boss_bridge.c (per-line translation of Z_04.asm:5612/5684/5764).
     * draw_write_boss_sprite primitive (WriteBossSprite + Anim_EndWriteSprite
     * fused) lives in draw_dispatch.c. ENEMY_PAUSE_FLAG gate handled inside
     * enrt_update_aquamentus drain body. */
    [0x3D] = enrt_update_aquamentus,        /* Aquamentus */
    /* Phase 8 Task 8.3 — Dodongo UPDATE pair (NES Z_04.asm:5856
     * UpdateDodongo). Native bridge body in
     * src/game/enemies/bosses/boss_dodongo.c composes drained
     * primitives (collisions / bomb-hit / draw / stunned /
     * bloated_sub_die / bloated_sub_end) with native State0_Move +
     * Bloated_Sub_Wait + dispatchers. Two NES rows ($31/$32) fan to
     * the same UpdateDodongo entry — preserved verbatim. */
    [0x31] = boss_dodongo_update,           /* Dodongo */
    [0x32] = boss_dodongo_update,           /* Dodongo (alt) */
    /* Phase 8 Task 8.4 — Manhandla UPDATE (NES Z_07.asm:5295 row $3C ->
     * SwitchBank #$01 + JMP UpdateManhandla @ Z_04.asm:7842). Drained at
     * enemy_manhandla_runtime.c:44 — full UpdateManhandla including the
     * 5-segment loop, Manhandla_BounceDir / Manhandla_SegmentJustDied,
     * TurnTowardsPlayer8 vs TurnRandomlyDir8 pick, $56 fireball spawn
     * gate, and mirrored vs not-mirrored draw fork. Callee shims for
     * c_turn_randomly_dir8 / c_play_boss_*_cry / c_draw_object_mirrored
     * live in src/game/enemies/bosses/boss_manhandla.c. ADOPT stance. */
    [0x3C] = enrt_update_manhandla,         /* Manhandla */
    /* Phase 8 Task 8.5 — Gleeok UPDATE rows ($42-$46).
     *   $42-$45 Gleeok 1-4 neck -> enrt_update_gleeok (drained at
     *                              enemy_gleeok_runtime.c — full 4-neck
     *                              dispatch over c_gleeok_fetch_neck_addrs /
     *                              move_neck / draw_segment_and_check_collisions /
     *                              calc_segment_limits / stretch_neck and
     *                              gleeok_check_collisions / dec_head_timer).
     *   $46 GleeokHead          -> boss_gleeok_update_head (native body
     *                              at Z_04.asm:8527 — flying-head 5-state
     *                              dispatch including z04_init_blue_keese,
     *                              c_control_keese_flight reuse). */
    [0x42] = enrt_update_gleeok,            /* Gleeok 1-neck */
    [0x43] = enrt_update_gleeok,            /* Gleeok 2-neck */
    [0x44] = enrt_update_gleeok,            /* Gleeok 3-neck */
    [0x45] = enrt_update_gleeok,            /* Gleeok 4-neck */
    [0x46] = boss_gleeok_update_head,       /* GleeokHead (flying) */
    /* Phase 8 Task 8.6 — Digdogger UPDATE rows (NES Z_07.asm:5320/5352/5353
     * rows $18/$38/$39 -> UpdateDigdogger @ Z_04.asm:5265). Drained at
     * enemy_boss_runtime.c:enrt_update_digdogger — full body including
     * magic-clock/stun gate, flute states 1+2 (turn vs split-up vs
     * make-children), 4-corner CheckBigDigdoggerCollisions loop, big +
     * little draw, Digdogger_ChangeSpeed/Move/Draw helpers.
     * $18 child rows trampoline through the same drain since IsChild
     * gating drives the big-vs-little branches. ADOPT stance. */
    [0x18] = enrt_update_digdogger,         /* LittleDigdogger (child) */
    [0x38] = enrt_update_digdogger,         /* Digdogger1 (big) */
    [0x39] = enrt_update_digdogger,         /* Digdogger2 (big, 2nd quest) */
    /* Phase 8 Task 8.7 — Gohma UPDATE pair (NES Z_07.asm:5347/5348 rows
     * $33/$34 -> UpdateGohma @ Z_04.asm:8207). Drained at
     * enemy_boss_runtime.c:774 — full body: random-direction pick +
     * 1/2-pixel movement accumulator + 0x20-pixel sprint reverse +
     * eye state machine (open / half-open / closed cycle, 0xC0|RNG
     * reload) + shoot timer rollover spawning fireball type 86, then
     * tail-calls c_gohma_animate_and_draw + c_gohma_check_collisions
     * (asm shims; Gohma_HandleWeaponCollision in Z_01.asm gates
     * arrow-only damage by eye state == 3). ADOPT stance. */
    [0x33] = enrt_update_gohma,             /* Blue Gohma */
    [0x34] = enrt_update_gohma,             /* Red Gohma */
    /* Phase 8 Task 8.8 — Patra UPDATE rows (NES Z_07.asm rows $25/$26/$47/$48).
     *   $47/$48 Patra1/Patra2 -> boss_patra_update (bridge orchestrator,
     *                            calls enrt_flyer_speed_up /
     *                            enrt_flyer_patra_decide_state /
     *                            c_control_keese_flight (states 2/3) +
     *                            c_move_flyer + enrt_animate_and_draw_common_object(2)
     *                            + child-loop + TryChangeManeuver flip).
     *   $25/$26 PatraChild1/PatraChild2 -> enrt_update_patra_child
     *                                      (drained — State 0 staged spawn
     *                                      off slot-2 child's angle, State 1
     *                                      orbit + draw + collision + dead-dummy
     *                                      transition). ADOPT stance. */
    [0x47] = boss_patra_update,             /* Patra1 */
    [0x48] = boss_patra_update,             /* Patra2 */
    [0x25] = enrt_update_patra_child,       /* PatraChild1 */
    [0x26] = enrt_update_patra_child,       /* PatraChild2 */
    /* Phase 8 Task 8.9 — Lamnola + Moldorm UPDATE rows.
     * NES Z_04.asm:5295 UpdateObject_JumpTable rows:
     *   $3A Lamnola1 -> UpdateLamnola (Z_04.asm:9699).
     *   $3B Lamnola2 -> UpdateLamnola.
     *   $41 Moldorm  -> UpdateMoldorm (Z_04.asm:4907).
     * Bodies drained at enemy_lamnola_runtime.c:44 and
     * enemy_moldorm_runtime.c:170. ADOPT stance. */
    [0x3A] = enrt_update_lamnola,           /* Lamnola1 */
    [0x3B] = enrt_update_lamnola,           /* Lamnola2 */
    [0x3E] = enrt_update_ganon,             /* Ganon */
    [0x41] = enrt_update_moldorm,           /* Moldorm */
    /* Task 7.5 step 2 — special-enemy UPDATE: $17 LikeLike.
     *
     * NES Z_07.asm:5295 UpdateObject_JumpTable row $17 LikeLike ->
     * UpdateLikeLike @ Z_04.asm:6818. Top-level state machine not
     * directly drained; native bridge body in enemy_special_bridge.c
     * carries per-line NES translation. Composes z04_update_common_wanderer
     * (already drained in enemy_boss_bridge.c) + z07_anim_fetch_obj_pos +
     * draw_object_mirrored_with_frame + draw_object_mirrored_over_link
     * (new public API in draw_dispatch.c step 2) + c_check_monster_collisions
     * + enemy_hide_sprites_over_link.
     *
     * Two paths driven by ObjCaptureTimer (= ENEMY_TURN_TIMER alias at
     * $042C):
     *   - 0: free-roam wander + 4-frame anim + capture-detect
     *        post-collision; if capture fired, seed monster X/Y =
     *        Link X/Y, clear Link's timer/metastate/shove, reset
     *        monster anim, INC LinkParalyzed.
     *   - != 0: animate up to frame 3, INC capture timer, drop magic
     *           shield at >= $60, draw mirrored OVER Link (sprites
     *           $10/$11), check death; on death (metastate != 0)
     *           clear LinkParalyzed + hide over-Link sprites. */
    [0x17] = enrt_update_like_like,         /* LikeLike */
    /* 7.5 step 3 — $16 PolsVoice UPDATE wired.
     * NES Z_04.asm:6533 UpdatePolsVoice. Native bridge body in
     * enemy_special_bridge.c. Two-state walker/jumper. Composes
     * enrt_pols_voice_move_x + enrt_pols_voice_is_square_walkable
     * (already drained in enemy_boss_runtime.c) + z07_anim_advance_and_fetch
     * + draw_object_mirrored_with_frame + c_check_monster_collisions.
     *
     *   State 0 (walking): decrement ObjRemDistance, ADC walk-speed-Y
     *                       per-direction, walkability probe; on tile
     *                       $B0 or $F4..$FF -> set state 1; on other
     *                       block -> flip dir (horizontal: EOR $03 +
     *                       2x MoveX; vertical: EOR $0C).
     *
     *   State 1 (jumping): vertical accel $38 frac + carry whole, ObjY
     *                       += whole; on speed-positive AND ObjY >=
     *                       TargetY -> state 0, randomize dir
     *                       ($01/$02/$04/$08) + distance ($31/$71),
     *                       grid-snap X/Y.
     *
     *   Pre-pass guards: InvClock or ObjStunTimer, OR odd FrameCounter
     *                    -> draw + collisions only.
     *   Tail: AdvanceAnim($08), DrawObjectMirrored, mask=$FE, collisions. */
    [0x16] = enrt_update_pols_voice,        /* PolsVoice */

    /* 7.5 step 4: $27 Wallmaster UPDATE. NES UpdateWallmaster
     * (Z_04.asm:4121). Native bridge body in enemy_special_bridge.c
     * carries per-line NES translation. State machine:
     *
     *   State 0 (idle): gated on Link's ObjState[0]==$40 + ObjTimer+1==0
     *                   + Link standing in a wall trigger zone (X in
     *                   {$20,$D0} for side walls, Y in {$5D,$BD} for
     *                   top/bottom). Calls drained
     *                   enrt_wallmaster_calc_start_position to compute
     *                   emergence offset + initial X/Y; seeds dir from
     *                   k_wallmaster_dirs_and_attrs[ObjStep], timer1=$60
     *                   / qspeed=$18 / animcount=$08, INC ObjState.
     *
     *   State 1 (walking): each frame, if shoved -> Obj_Shove; else if
     *                      magic clock or stun, draw only; else
     *                      MoveObject in current dir; on grid alignment
     *                      ($10/$F0), advance step + dir + tiles
     *                      crossed; on 7th tile end-of-trip: if
     *                      ObjCaptureTimer != 0 -> HideSpritesOverLink +
     *                      GameMode=3 + reset Link/IsUpdatingMode/
     *                      GameSubmode; either way ObjState[slot]=0
     *                      and exit (return to wall).
     *
     *   Draw + collisions tail:
     *     - If captured: reposition Link onto monster, force frame=1
     *       hand-closed, draw OverLink, patch sprites at hardcoded
     *       OAM offsets $40/$44.
     *     - Else: CheckMonsterCollisions (may set capture); save sprite
     *       cursor, PrepareToDraw + DrawObjectNotMirrored, restore
     *       cursor, look up SpriteOffsets[idx], patch $9C->$AC keese-
     *       tile fixup on closed-hand frame. */
    [0x27] = enrt_update_wallmaster,        /* Wallmaster */
    /* Task 7.6 step 2 — BlueLeever UPDATE ($0F). NES UpdateBlueLeever
     * @ Z_04.asm:2599-2647: ObjTurnRate=$A0 (ENEMY_AIR_SPEED=$A0) +
     * Wanderer_TargetPlayer + UpdateBurrower fall-through. Bridge body
     * at enemy_jumper_bridge.c:enrt_update_blue_leever — composes
     * enrt_wanderer_target_player (drained twin) + c_update_burrower
     * (already drained for $11 Zora UPDATE chain). */
    [0x0F] = enrt_update_blue_leever,       /* BlueLeever */
    /* Task 7.6 step 3 — RedLeever UPDATE ($10). NES UpdateRedLeever
     * @ Z_04.asm:2737-2961: state-0 spawn-from-Link with cap-2
     * ActiveRedLeeverCount + RedLeeverLongTimer gate; state-3 shove
     * then move/boundary cycle; fall-through to animate path that
     * passes its own RedLeeverStateAnimTimes through the shared
     * Burrower_AnimateDrawAndCheckCollisions helper. Bridge body in
     * enemy_jumper_bridge.c (carries RedLeeverState* tables and the
     * shared burrower-animate helper). */
    [0x10] = enrt_update_red_leever,        /* RedLeever */
    /* Plan v5c — dropped-item ($60) UPDATE. NES UpdateItem @ Z_04.asm:11236.
     * After SetUpDroppedItem converts a dead-monster slot into a $60
     * dropped item (lifetime $FF, item id at $00AC), this row ticks the
     * lifetime + checks Link bbox 9x9 for pickup. On hit it calls
     * item_take_item(id) and clears the slot (DestroyMonster_Bank4).
     *
     * Greenfield port (no _runtime.c candidate). Stance: EXTEND — single
     * table row + native body in src/game/items/item_object.c. Without
     * this row the type-$60 slot tick is a no-op: drops appear but
     * never decay and never get picked up. */
    [0x60] = item_object_update,            /* DroppedItem */
    /* T-050 OW tile objects (NES UpdateObject_JumpTable $61-$67); UW
     * block $68 is T-054. Dock $61 / raft: T-056 (dock.c). */
    [0x61] = world_update_dock,               /* Dock */
    [0x2F] = enrt_update_pond_fairy,          /* PondFairy */
    [0x5E] = room_update_flute_secret,        /* FluteSecret (T-171) */
    [0x62] = room_update_rock_or_gravestone,  /* Rock */
    [0x63] = room_update_rock_wall,           /* RockWall */
    [0x64] = room_update_tree,                /* Tree */
    [0x65] = room_update_rock_or_gravestone,  /* Gravestone */
    [0x66] = room_update_rock_or_gravestone,
    [0x67] = room_update_rock_wall,
    [0x68] = roomrom_pushblock_update,         /* UW block moving sprite */
};

/* Internal: clear an enemy slot's scratch state per NES room-init
 * (Z_05.asm:1684-1699 — the per-slot init loop run for slots $B..1
 * BEFORE InitObject_JumpTable[ObjType,X]).
 *
 * Step 10 finding: prior implementation zeroed WALK_SPEED / ANIM_TIMER
 * / METASTATE — wrong vs NES, which seeds defaults:
 *   ObjQSpeedFrac = $20  (Z_05.asm:1696 — DEFAULT speed)
 *   ObjAnimCounter = 1   (Z_05.asm:1694 — INC from 0)
 *   ObjMetastate   = 1   (Z_05.asm:1695 — INC from 0; "first cloud state")
 * Octorok/Darknut init wrappers explicitly override WALK_SPEED so they
 * are unaffected. Bare-InitWalker types ($01-$06,$2A) inherit the $20
 * default — that's what unblocks goriya/stalfos walking (their UPDATE
 * bodies don't re-seed WALK_SPEED; first quest stalfos UPDATE BEQs out
 * past the @SetSpeed branch entirely). */
static void clear_slot_scratch(unsigned int slot)
{
    ENEMY_DIR(slot)            = 0u;
    ENEMY_STATE_TIMER(slot)    = 0u;
    ENEMY_METASTATE(slot)      = 1u;          /* NES default: first cloud state */
    ENEMY_PUSH_TIMER(slot)     = 0u;
    ENEMY_AIR_SPEED(slot)      = 0u;
    ENEMY_TURN_TIMER(slot)     = 0u;
    ENEMY_AI_STATE(slot)       = 0u;
    ENEMY_BLOATED_TIMER(slot)  = 0u;
    ENEMY_BOUNCE_FLAGS(slot)   = 0u;
    ENEMY_CHARGE_SPEED(slot)   = 0u;
    ENEMY_COLLIDED_TILE(slot)  = 0u;
    ENEMY_INVINCIBILITY(slot)  = 0u;
    ENEMY_HIT_REACTION(slot)   = 0u;
    ENEMY_DRAW_FRAME(slot)     = 0u;
    ENEMY_WALK_SPEED(slot)     = 0x20u;       /* NES Z_05.asm:1696 default */
    ENEMY_ANIM_TIMER(slot)     = 1u;          /* NES default INC from 0 */
    ENEMY_PUSH_DIR_SCRATCH(slot) = 0u;
    ENEMY_FLAP_PHASE(slot)     = 0u;
    ENEMY_FLYER_X_FINE(slot)   = 0u;
    ENEMY_STUN_TIMER(slot)     = 0u;          /* Z_05.asm:1693 ObjStunTimer */
    ENEMY_OBJ_SHOVE_DIR(slot)  = 0u;          /* ResetShoveInfo */
    OBJ(0x00D3u, slot)         = 0u;          /* ObjShoveDistance */
    /* ObjTimer ($28+slot) is not touched by InitMode_EnterRoom: it keeps
     * running down from the old room (T-171: t012_route t1385 NES $0C
     * after $73). ENEMY_LIFE ($BF+slot) was cleared here too: that is
     * ObjShoveDir of slot-1, which ResetShoveInfo already owns. */
    ENEMY_ALIVE_FLAG(slot)     = 0xFFu;       /* ObjUninitialized: InitMode_EnterRoom DEC from 0 */
}

/* Public (T-143, T-171): NES InitMode_EnterRoom for a cave entry
 * (InitModeB_EnterCave_Bank5 -> InitMode_EnterRoom, Z_05.asm:1543):
 * ClearRam0300UpTo $051F, the whole page (T-171: clearing only the object
 * arrays left ZoraActive $514, FlyingMaxSpeedFrac $4D1, Link's collided
 * tile and invincibility timer, the Gleeok cells ... from the OW room; a
 * Zora room's cave then never ran CheckZora as the NES does). Then what
 * InitMode_EnterRoom rebuilds: LevelBlockAttrsByteF, SetupObjRoomBounds,
 * CurObjIndex $B, and for slots $B..1 (Z_05.asm:1684-1699) DEC
 * ObjUninitialized, ResetShoveInfo, ObjState/ObjDir/ObjStunTimer = 0, INC
 * ObjAnimCounter, INC ObjMetastate (first cloud state), ObjQSpeedFrac
 * $20; ObjX/ObjY (outside the page) keep their values. Link's part after
 * the monster list: ObjAnimCounter 4, InitLinkSpeed $60 (the collided
 * tile was just cleared, so not the mountain-stairs $30). The swap then
 * sets Link's walk-in cells (InitModeB_EnterCave_Bank5). */
void enemy_loop_enter_cave_slots(void)
{
    unsigned int slot;
    core_clear_ram0300_up_to(5u, 0x1Fu);
    RAM(0x04CDu) = (unsigned char)DUNGEON_LBA_F(RAM(0x00EBu));   /* LevelBlockAttrsByteF */
    room_setup_obj_room_bounds();
    RAM(0x0340u) = 0x0Bu;                 /* CurObjIndex */
    for (slot = ENEMY_LOOP_SLOT_LAST; slot >= ENEMY_LOOP_SLOT_FIRST; --slot) {
        RAM(0x0492u + slot) = (unsigned char)(RAM(0x0492u + slot) - 1u); /* ObjUninitialized */
        RAM(0x00C0u + slot) = 0u;         /* ObjShoveDir */
        RAM(0x00D3u + slot) = 0u;         /* ObjShoveDistance */
        RAM(0x00ACu + slot) = 0u;         /* ObjState */
        RAM(0x0098u + slot) = 0u;         /* ObjDir */
        RAM(0x003Du + slot) = 0u;         /* ObjStunTimer */
        RAM(0x03D0u + slot) = (unsigned char)(RAM(0x03D0u + slot) + 1u); /* ObjAnimCounter */
        RAM(0x0405u + slot) = (unsigned char)(RAM(0x0405u + slot) + 1u); /* ObjMetastate */
        RAM(0x03BCu + slot) = 0x20u;      /* ObjQSpeedFrac */
    }
    RAM(0x03D0u) = 4u;                    /* Link ObjAnimCounter */
    RAM(0x03BCu) = 0x60u;                 /* InitLinkSpeed */
}

/* T-171: the object part of InitMode_EnterRoom for a cave entry
 * (InitModeB_EnterCave_Bank5, Z_01.asm:2956 -> Z_05.asm:1700-1850), after
 * enemy_loop_enter_cave_slots: the OW room's monster list and count
 * (RoomObjCount / RoomObjTemplateType keep them), AssignObjSpawnPositions
 * against the cave layout and Link's descent position (ObjX/ObjY of slots
 * 1..9, SpawnCycle $524), its @InCave branch (types 1..8 cleared, the
 * dweller in slot 1), then SetupTileObjectOW (slot $B from RoomTileObj*,
 * which the OW entry reset). The Genesis skipped all of it: SpawnCycle
 * stayed behind the NES and moved the next OW room's spawn spots. */
void enemy_loop_enter_cave_objects(unsigned char room_id)
{
    (void)enemy_room_load_objects(room_id);
    enemy_assign_spawn_positions(room_id, enemy_room_template_id());
    room_setup_tile_object_ow();
}

/* enemy_fix probe arm-hook suppression flag. Set by enemy_loop_arm_fix_probe
 * to skip the next room_init pass (otherwise natural-spawn rolls clobber
 * the force-spawned probe target). */
static unsigned char s_fix_arm_suppress_room_init = 0u;

/* T-050: NES SetupTileObjectOW, run for OW rooms at room entry after
 * AssignObjSpawnPositions (Z_05.asm, "LDA CurLevel / BNE / JSR
 * SetupTileObjectOW"). RoomTileObjType/X/Y are what LayoutRoomOW recorded
 * for this room (roomrom_ow_room_tile_object replays that layout scan).
 * Slot $B then gets the common room-entry object reset (Z_05.asm, the
 * $B..1 loop before the monster list: ObjUninitialized $FF, metastate 1,
 * default q-speed $20) so the first tick runs InitObject ->
 * InitTileObjOrItem, as on the NES. */
static void ow_tile_object_room_setup(unsigned char scene_id, unsigned char level,
                                      unsigned char room_id)
{
    unsigned char t, x, y;
    if (scene_id != 0u || level != 0u) return;
    roomrom_ow_room_tile_object(room_id, &t, &x, &y);
    RAM(0x052Bu) = t;                    /* RoomTileObjType */
    RAM(0x052Cu) = x;                    /* RoomTileObjX */
    RAM(0x052Du) = y;                    /* RoomTileObjY */
    room_setup_tile_object_ow();
    if ((unsigned char)ENEMY_TYPE(11u) == 0u) {
        ENEMY_ALIVE_FLAG(11u) = 0xFFu;
        return;
    }
    clear_slot_scratch(11u);
    ENEMY_ALIVE_FLAG(11u) = 0xFFu;       /* NES ObjUninitialized $FF */
    ENEMY_METASTATE(11u) = 1u;
    OBJ(NES_OBJ_QSPD_FRAC, 11u) = 0x20u;
    OBJ(0x0098u, 11u) = 0u;              /* ObjDir */
    OBJ(NES_OBJ_GRID_OFFSET, 11u) = 0u;
    OBJ(NES_OBJ_POS_FRAC, 11u) = 0u;
}

static void enemy_loop_room_init_impl(unsigned char room_id, unsigned char scene_id,
                                      unsigned char level, unsigned char quest,
                                      unsigned char force_entry)
{
    unsigned int slot;
    static unsigned char s_last_room_id  = 0xFFu;
    static unsigned char s_last_scene_id = 0xFFu;
    static unsigned char s_last_level = 0xFFu;
    static unsigned char s_last_quest = 0xFFu;
    unsigned char same_room = (!force_entry && s_last_room_id == room_id &&
                               s_last_scene_id == scene_id &&
                               s_last_level == level &&
                               s_last_quest == quest);
    /* NES InitMode_EnterRoom (Z_05.asm:1546): ClearRam0300UpTo $051F
     * before any room object setup (ZoraActive $514, object q-speeds,
     * counters, ...). Only on a real entry: the repeat-entry guard below
     * returns without touching room state. */
    if (!same_room && !s_fix_arm_suppress_room_init) {
        core_clear_ram0300_up_to(5u, 0x1Fu);
        /* ResetInvObjState's LadderSlot half (T-056): no ladder is out in
         * a newly entered room. Its weapon-slot half stays with the
         * weapon owners (bomb.c room-change reset). */
        RAM(0x0064u) = 0u;
    }
    /* InitMode_EnterRoom @PlaceObjects: CurObjIndex = $B, ready for the
     * first mode 5 frame (T-171: t013_route t883 after a cave exit). */
    RAM(0x0340u) = 0x0Bu;
    /* NES SetupObjRoomBounds (Z_05.asm:6409), run by InitMode_EnterRoom
     * after placing Link and by RunCrossRoomTasks: RoomBoundLeft/Right/
     * Up/Down ($346-$349) + ObjectFirstUnwalkableTile ($34A). */
    room_setup_obj_room_bounds();
    /* T-116: Link's part of the NES room-entry object setup (Z_05.asm,
     * after AssignObjSpawnPositions / SetupTileObjectOW): stun timer and
     * shove cleared, animation counter 4, InitLinkSpeed q-speed $60. NES
     * then animates Link once in mode 4 (AnimateLinkBase); Genesis has no
     * mode-4 frame (T-096), so store the result: NES captures show $3D0 = 3
     * at every mode 4 -> 5 transition (t105_scroll, t105_horizontal, t114). */
    RAM(0x003Du) = 0u;       /* ObjStunTimer */
    RAM(0x00C0u) = 0u;       /* ObjShoveDir */
    RAM(0x00D3u) = 0u;       /* ObjShoveDistance */
    /* ObjAnimCounter after the mode-4 step; a whirlwind teleport does not
     * animate (Link_EndMoveAndAnimate returns), so it stays 4 (T-171). */
    RAM(0x03D0u) = (RAM(0x0522u) != 0u) ? 4u : 3u;
    RAM(0x03BCu) = 0x60u;    /* ObjQSpeedFrac */
    /* enemy_fix arm hook: bail this room_init pass exactly once. */
    if (s_fix_arm_suppress_room_init) {
        s_fix_arm_suppress_room_init = 0u;
        return;
    }
    /* NES source: Z_05.asm:InitMode_EnterRoom.
     * Drained C: enemy_loop_room_init; Coverage: PARTIAL entry identity.
     * Stance: EXTEND. Room numbers are local to level/quest, so include
     * both in the repeat-entry guard. The caller owns scene identity.
     * Scroll-glitch guard: track last room identity. Probe
     * build/probes/track_all.lua captured Link X-wrap $00->$F0 that
     * re-triggers room_init without room_id change, smashing enemy
     * slot types + replaying spawn-cloud. Skip whole init on repeat. */
    s_last_level = level;
    s_last_quest = quest;
    s_last_room_id  = room_id;
    s_last_scene_id = scene_id;
    if (same_room) {
        return;
    }

    /* NES source: Z_05.asm:InitMode_EnterRoom / ModifyObjCountByHistoryUW.
     * Drained C: enemy_room_load_objects / room_get_room_flags.
     * Coverage: PARTIAL (native room entry); Stance: EXTEND.
     * History reads RoomId implicitly; install incoming identity before it.
     * RoomKillCount is per visit and was saved by the departure path. */
    RAM(0x00EBu) = room_id;
    /* NES source: Z_05.asm:InitMode_EnterRoom caches the active room's
     * LevelBlockAttrsF byte at $04CD. Drained C: native room init.
     * Coverage: PARTIAL room-entry consumers. Stance: EXTEND. */
    RAM(0x04CDu) = (unsigned char)DUNGEON_LBA_F(room_id);
    /* NES source: Z_07.asm:ResetPlayerState / Walker_Move:CheckStunned.
     * Drained C: c_walker_move and native room entry.
     * Coverage: PARTIAL clock lifetime; Stance: EXTEND.
     * Clock lasts for the room, not one enemy tick. */
    RAM(0x066Cu) = 0u;
    /* NES Z_07.asm:MarkRoomVisited; existing room_dispatch drain.
     * Installed dungeon flags retain visits across room transitions. */
    /* RunCrossRoomTasksAndBeginUpdateMode_EnterPlayModes (UW): then
     * WriteBlankPrioritySprites refills OAM slots 0..15. Gleeok's heads
     * live there; the boss OAM sweep drew the last Gleeok's heads in the
     * next boss room without it. */
    if (scene_id == 1u && level >= 1u && level <= 9u) {
        room_mark_room_visited();
        core_write_blank_priority_sprites();
    }
    RAM(0x034Fu) = 0u;

    /* 2026-05-24 — reset ROOM_MONSTER_ALL_DEAD ($034D) + ROOM_SHUTTER_TRIGGERED
     * ($04CE) on room load. Pre-fix, $034D held stale value from prior room
     * (e.g. $D1 garbage post-warp), causing shutter auto-trigger fix to fire
     * immediately on room entry before enemies spawned. NES Z_05.asm InitMode4
     * clears via @ClearRamPage in mode-transition sequence. */
    RAM(0x034Du) = 0u;
    RAM(0x04CEu) = 0u;

    /* Clear all enemy slots on room load. 2026-05-22 — was clearing
     * only TYPE/ALIVE/X/Y; shove + state cells inherited from prior
     * room's slot occupant (e.g. octorok died → set shove during death
     * → next room spawn reused slot with stale shove $40) → first
     * post-cloud frame c_walker_move saw SHOVE_DIR != 0 → c_obj_shove
     * 4-pixel-per-frame loop = user-visible "fly across screen".
     * Clear ALL state cells the per-frame walker/AI paths read. */
    for (slot = ENEMY_LOOP_SLOT_FIRST; slot <= ENEMY_LOOP_SLOT_LAST; ++slot) {
        /* NES source: Z_05.asm:InitMode_EnterRoom common object defaults.
         * Drained C: clear_slot_scratch (previously debug-spawn only).
         * Coverage: PARTIAL room-spawn movement; Stance: EXTEND.
         * Type-specific init overrides these after room placement. */
        clear_slot_scratch(slot);
        ENEMY_TYPE(slot) = 0u;        /* type 0 = DoNothing = empty */
        ENEMY_ALIVE_FLAG(slot) = 0xFFu;  /* ObjUninitialized, NES DEC from 0 */
        /* ObjX/ObjY keep their values (outside NES ClearRam0300UpTo). */
        ENEMY_OBJ_SHOVE_DIR(slot) = 0u;        /* $00C0+slot */
        OBJ(0x00D3u, slot)         = 0u;       /* shove distance */
        OBJ(NES_OBJ_POS_FRAC, slot) = 0u;      /* $03A8+slot */
        OBJ(NES_OBJ_GRID_OFFSET, slot) = 0u;   /* $0394+slot */
        ENEMY_STUN_TIMER(slot)     = 0u;       /* $003D+slot */
        ENEMY_HIT_REACTION(slot)   = 0u;       /* $04F0+slot */
        /* NES room-entry object reset (Z_05.asm, slots $B..1): default
         * q-speed $20 before the monster list / tile object. */
        OBJ(NES_OBJ_QSPD_FRAC, slot) = 0x20u;
    }

    /* Phase 7 Task 7.7 step 1+2 — wire NES InitMode_EnterRoom monster-list
     * parser + AssignObjSpawnPositions.
     *   step 1: enemy_room_load_objects fills ObjType[1..count] from
     *           LevelBlockAttrs C/D + LevelInfo_FoeCounts (Z_05.asm:1700-1820).
     *   step 2: enemy_assign_spawn_positions walks SpawnPosListAddrs[ObjDir]
     *           with IsSafeToSpawn + cellar/cave overrides (Z_05.asm:1885-1996).
     * After spawn assignment, dispatch enemy_init_fns[ENEMY_TYPE(slot)]
     * for each occupied slot per NES init-fan-out at Z_05.asm:1818.
     * Cave dweller / cellar keese paths run inside step 2 even when
     * step 1 returns 0 — handle those by re-checking template type
     * after spawn-pos call. */
    (void)scene_id;
    unsigned char loaded = enemy_room_load_objects(room_id);
    enemy_assign_spawn_positions(room_id, enemy_room_template_id());

    /* Phase 8 Task 8.1 step 3 — room-item slot 19 reward setup.
     * NES InitMode_EnterRoom (Z_05.asm:1700-1820) tail-calls
     * CreateRoomObjects (Z_05.asm:8154) AFTER monster placement,
     * which writes the heart-container / triforce-piece / per-room
     * L-block reward into ObjType[$13] / ObjState[$13]. Done as a
     * native body in src/game/enemies/bosses/boss_framework.c. */
    boss_framework_room_init(room_id);

    /* NES source: reference/aldonunez/Z_07.asm:
     * RunCrossRoomTasksAndBeginUpdateMode @LoopHistory.
     * Drained C: src/game/room/room_dispatch.c:room_record_history.
     * Coverage: PARTIAL (shared native enemy-room initialization).
     * Stance: EXTEND.
     * Record only after enemy_room_load_objects has consumed the previous
     * history, matching the NES CreateRoomObjects -> history order. */
    room_record_history(room_id);

    if (loaded == 0u && DUNGEON_ROOM_OBJ_COUNT == 0u) {
        ow_tile_object_room_setup(scene_id, level, room_id);
        return;
    }
    /* T-012: NES InitMode_EnterRoom (Z_05.asm:1689) only flags the
     * slots uninitialized ($FF); UpdateObject runs InitObject on the
     * object's first update (mode 5), which rolls Random for the start
     * direction. Initializing here, frames early, drew other Random
     * values (t012_route room $67: octorok dirs 02/02/02/08 at the load,
     * NES 04/02/04/04 on its first update). enemy_loop_tick's
     * uninitialized branch runs enemy_loop_init_object. */
    for (slot = ENEMY_LOOP_SLOT_FIRST; slot <= ENEMY_LOOP_SLOT_LAST; ++slot) {
        unsigned char t = ENEMY_TYPE(slot);
        if (t == 0u) continue;
        if (t >= ENEMY_LOOP_TYPE_MAX) continue;
        ENEMY_ALIVE_FLAG(slot) = 0xFFu;
    }
    ow_tile_object_room_setup(scene_id, level, room_id);
}

void enemy_loop_room_init(unsigned char room_id, unsigned char scene_id,
                          unsigned char level, unsigned char quest)
{
    enemy_loop_room_init_impl(room_id, scene_id, level, quest, 1u);
}

/* NES source: Z_05.asm InitMode_EnterRoom, reached from mode 4 after a
 * mode 3 Continue; drained C: enemy_loop_room_init_impl; coverage: room
 * re-entry; stance: EXTEND. Bypass only the duplicate-scroll guard. */
void enemy_loop_room_reenter(unsigned char room_id, unsigned char scene_id,
                             unsigned char level, unsigned char quest)
{
    enemy_loop_room_init_impl(room_id, scene_id, level, quest, 1u);
}

static void loop_object_wrapper(unsigned int slot)
{
    /* NES @LoopObject post-update wrapper (Z_07.asm:1928-1945).
     * After UpdateObject returns, NES runs:
     *   if metastate == 0 && (ObjAttr & 0x01) == 0:
     *     if (ObjAttr & 0x04) == 0: AnimateAndDrawObjectWalking
     *     CheckMonsterCollisions
     *
     * Moblin/Goriya/Lynel have attr $00 — wrapper does BOTH draw and
     * collide. Without it, Moblin walks invisibly and Link can't
     * touch-damage. Octorok/Stalfos have bit 0 set, skip wrapper. */
    if (ENEMY_METASTATE(slot) == 0u) {
        unsigned char attr = (unsigned char)RAM(0x04BFu + slot);
        if ((attr & 0x01u) == 0u) {
            if ((attr & 0x04u) == 0u) {
                extern void z07_animate_object_walking(unsigned int slot);
                extern void c_draw_object_not_mirrored_with_frame(unsigned int frame, unsigned int slot);
                z07_animate_object_walking(slot);
                /* 2026-05-22 — NES SetUpWalkingSprites (Z_07.asm:5059)
                 * picks frame index from ObjDir:
                 *   dir & $08 (UP)   -> frame=3
                 *   dir & $04 (DOWN) -> frame=2
                 *   horizontal       -> frame = ObjAnimFrame (0/1)
                 * Without this Moblin always renders horizontal
                 * frame regardless of facing → looked like "only
                 * faces L/R". */
                unsigned char d = (unsigned char)ENEMY_DIR(slot);
                unsigned char draw_frame;
                if ((d & 0x0Cu) != 0u) {
                    draw_frame = ((d & 0x08u) != 0u) ? 3u : 2u;
                } else {
                    draw_frame = (unsigned char)ENEMY_DRAW_FRAME(slot);
                }
                c_draw_object_not_mirrored_with_frame(draw_frame, slot);
            }
            extern void c_check_monster_collisions(unsigned int slot);
            c_check_monster_collisions(slot);
        }
    }
}

/* NES UpdateObject's uninitialized path (Z_07.asm:5244) + InitObject
 * (Z_07.asm:5466). $492 keeps the NES meaning: 0 = initialized, anything
 * else = InitObject on the next update. */
void enemy_loop_init_object(unsigned int slot, unsigned char t)
{
    enemy_init_fn fn;
    const unsigned char cloud = (unsigned char)(t < 0x53u && t != 0x1Eu && t != 0x22u);
    RAM(0x000Fu) = (unsigned char)ENEMY_ALIVE_FLAG(slot);   /* STY $0F: uninit flag */
    /* UpdateObject: cloud types start with ObjTimer 7 (InitObject sets
     * the real value); flagged initialized before InitObject runs. */
    if (cloud) ENEMY_MOVE_TIMER(slot) = 7u;
    ENEMY_ALIVE_FLAG(slot) = 0u;

    /* InitObject: OW rooms with "enemies from edges" (LevelBlockAttrsF
     * bit 3, read from the room-entry copy LevelBlockAttrsByteF $4CD as the
     * NES does) bring monsters in from the edge, one per long-timer period;
     * zora, fire, armos, whirlwind and non-cloud objects spawn normally. */
    if ((unsigned char)RAM(0x0010u) == 0u &&
        ((unsigned char)DUNGEON_ATTRS_BYTE_F & 0x08u) != 0u &&
        t != 0x11u && t != 0x40u && t != 0x1Eu && t != 0x2Eu && t < 0x53u) {
        if ((unsigned char)RAM(0x004Bu) != 0u) {
            /* @UninitMonsterFromEdge: stay uninitialized. */
            ENEMY_ALIVE_FLAG(slot) = (unsigned char)RAM(0x004Bu);
            return;
        }
        enemy_edge_spawn_next(slot);
        RAM(0x004Bu) = (unsigned char)(((unsigned char)ENEMY_RNG_B(slot) & 3u) + 2u);
        if (!enemy_edge_distance_safe(slot)) {
            ENEMY_ALIVE_FLAG(slot) = (unsigned char)RAM(0x004Bu);
            return;
        }
        ENEMY_METASTATE(slot) = 0u;        /* no spawn cloud */
    }

    /* @NormalSpawn: cloud monsters start moving at slot-staggered times. */
    if (cloud) ENEMY_MOVE_TIMER(slot) = (unsigned char)slot;
    RAM(0x0000u) = t;               /* [00] holds the object type */
    native_init_obj_attr(slot, t);  /* NES Z_07.asm:5566 @FetchAttrs */
    native_init_obj_hp(slot, t);    /* NES Z_07.asm:5576 HP nibble */
    if (t >= 0x6Au) {               /* Z_07.asm:5581 JMP InitCave */
        cave_init_person(slot);
        return;
    }
    fn = enemy_init_fns[t];
    if (fn != (enemy_init_fn)0) fn(slot);
}

/* Forward decl for arm hook (defined later in this file). */
void enemy_loop_force_spawn_typed(unsigned int slot,
                                  unsigned char enemy_type,
                                  unsigned char x,
                                  unsigned char y,
                                  unsigned char dir);

/* enemy_fix arm hook. Probe Lua writes 'FX' magic + 7 param bytes at
 * NES $77D0..$77D8 (Genesis $FF77D0..$FF77D8). On first tick after the
 * write, this consumes the magic, pins determinism cells (FrameCounter,
 * ChaseTarget toggle, Random[0..2]), writes RoomId + UnwalkableTile per
 * habitat, clears FoeCounts (suppresses natural spawn rolls), clears
 * all enemy slots, then force-spawns the requested type into the
 * requested slot. Subsequent ticks run normally.
 *
 *   $77D0 = 'F' (0x46)        magic byte 0
 *   $77D1 = 'X' (0x58)        magic byte 1
 *   $77D2 = type              enemy type ($01..$5C)
 *   $77D3 = x                 spawn X
 *   $77D4 = y                 spawn Y
 *   $77D5 = dir               spawn dir
 *   $77D6 = slot              spawn slot (1..11)
 *   $77D7 = habitat           0 = OW, 1 = UW
 *   $77D8 = room_id           test room (e.g. $77 for OW, $70 for UW)
 *
 * Plan: docs/superpowers/plans (enemy_fix v2) Step 0. */
static void enemy_loop_arm_fix_probe(void)
{
    /* Magic block at ABSOLUTE $FF77D0 (matches the existing
     * enemy_loop_probe arm at $FF73F8). NOT relative to A4 — Debug.md
     * pins A4 at $FF8000, so RAM(0x77D0) would write $FFFF7D0 which is
     * outside 68K work RAM. Probe Lua writes domain "68K RAM" offset
     * 0x77D0 = physical $FF77D0. */
    volatile unsigned char *arm = (volatile unsigned char *)0x00FF77D0UL;

    unsigned char m0 = arm[0];
    unsigned char m1 = arm[1];
    if (m0 != 0x46u || m1 != 0x58u) return;  /* not 'F','X' */

    unsigned char type    = arm[2];
    unsigned char x       = arm[3];
    unsigned char y       = arm[4];
    unsigned char dir     = arm[5];
    unsigned char slot    = arm[6];
    unsigned char habitat = arm[7];
    unsigned char room    = arm[8];

    /* Pin determinism cells. */
    RAM(0x0015u) = 0u;        /* FrameCounter */
    RAM(0x0060u) = 1u;        /* ChaseTarget toggle (Z_07.asm:1873) */
    RAM(0x0018u) = 0u;        /* Random[0] */
    RAM(0x0019u) = 1u;        /* Random[1] */
    RAM(0x001Au) = 2u;        /* Random[2] */
    RAM(0x00EBu) = room;      /* RoomId */
    RAM(0x004Au) = 8u;        /* ChaseLongTimer (non-zero suppresses toggle) */
    /* Clear InvClock ($066C) — debug-boot leaves it $01 which freezes
     * walker_move CheckStunned + flyer DrawAndCheck branches → all
     * enemies static. NES Variables.inc: InvClock := $66C. */
    RAM(0x066Cu) = 0u;

    /* Habitat: OW=$89 (per ObjectRoomBoundsOW[4]), UW=$78. */
    RAM(0x034Au) = (habitat == 0u) ? 0x89u : 0x78u;

    /* Clear FoeCounts so natural spawn rolls suppressed. */
    RAM(0x6BA2u) = 0u;
    RAM(0x6BA3u) = 0u;
    RAM(0x6BA4u) = 0u;
    RAM(0x6BA5u) = 0u;

    /* Clear all enemy slots. */
    {
        unsigned int s;
        for (s = ENEMY_LOOP_SLOT_FIRST; s <= ENEMY_LOOP_SLOT_LAST; ++s) {
            ENEMY_TYPE(s) = 0u;
            ENEMY_ALIVE_FLAG(s) = 0xFFu;
        }
    }

    /* Force-spawn target type into target slot. */
    if (slot >= ENEMY_LOOP_SLOT_FIRST && slot <= ENEMY_LOOP_SLOT_LAST) {
        enemy_loop_force_spawn_typed(slot, type, x, y, dir);
    }

    /* Suppress next room_init pass so the spawn isn't clobbered. */
    s_fix_arm_suppress_room_init = 1u;

    /* Clear magic so the hook fires exactly once per probe. */
    arm[0] = 0u;
    arm[1] = 0u;
}

/* Combat force-kill hook. Probe Lua writes 'DD' (damage death) at
 * absolute $FF77E0..$FF77E4. On first tick after write, consumes
 * magic, sets COMBAT_DAMAGE_TYPE + COMBAT_DAMAGE_AMOUNT, and calls
 * combat_deal_damage(slot). Enables drop/death dimension testing
 * via real combat dispatch (audit_inscope_combat probe).
 *
 *   $77E0 = 'D' (0x44)        magic byte 0
 *   $77E1 = 'D' (0x44)        magic byte 1
 *   $77E2 = slot              target enemy slot (1..11)
 *   $77E3 = damage_type       e.g. $10 sword, $20 bomb, $08 arrow
 *   $77E4 = damage_amount     e.g. $10 L1 sword, $20 L2, $40 L3
 */
extern void combat_deal_damage(unsigned int slot);
static void enemy_loop_combat_force_kill_hook(void)
{
    volatile unsigned char *arm = (volatile unsigned char *)0x00FF77E0UL;
    unsigned char m0 = arm[0];
    unsigned char m1 = arm[1];
    if (m0 != 0x44u || m1 != 0x44u) return;  /* not 'D','D' */

    unsigned char slot      = arm[2];
    unsigned char dmg_type  = arm[3];
    unsigned char dmg_amt   = arm[4];

    if (slot < ENEMY_LOOP_SLOT_FIRST || slot > ENEMY_LOOP_SLOT_LAST) {
        arm[0] = 0u; arm[1] = 0u;
        return;
    }

    RAM(0x0009u) = dmg_type;   /* COMBAT_DAMAGE_TYPE (ZP_TMP9) */
    RAM(0x0007u) = dmg_amt;    /* COMBAT_DAMAGE_AMOUNT (ZP_TMP7) */
    combat_deal_damage((unsigned int)slot);

    /* SetShoveInfo equivalent (NES Z_01.asm SetShoveInfo): real
     * combat path runs shove via collision handler. Hook bypasses
     * that, so apply shove here for parity-test correctness.
     * Shove dir 0x02 (right) + dist 0x40 per NES default. */
    RAM(0x00C0u + slot) = 0x02u;    /* ObjShoveDir */
    RAM(0x00D3u + slot) = 0x40u;    /* ObjShoveDistance */

    arm[0] = 0u;
    arm[1] = 0u;
}

/* NES CheckZora (Z_04.asm:1760): in a room with the Zora attribute
 * (LevelBlockAttrsA bit 3) and no Zora active, put one on a random water
 * square ($8D..$98) in the first empty monster slot. */
static void enemy_loop_check_zora(void)
{
    unsigned char room = RAM(0x00EBu);
    unsigned char slot, y;
    if ((RAM(0x687Eu + room) & 0x08u) == 0u) return;
    if (RAM(0x0514u) != 0u) return;                 /* ZoraActive */
    slot = (unsigned char)enemy_find_empty_monster_slot();
    if (slot == 0u) return;
    for (y = 0x0Du; y != 0u; --y) {
        unsigned char r = RAM(0x0018u + y);         /* Random, Y */
        unsigned char x = (unsigned char)(r & 0xF0u);
        unsigned char py = (unsigned char)(r << 4);
        unsigned char t;
        OBJ(0x0070u, slot) = x;
        if (x == 0u || x == 0xF0u) continue;
        if (py < 0x50u || py >= 0xE0u) continue;
        OBJ(0x0084u, slot) = (unsigned char)(py | 0x0Du);
        t = collision_get_collidable_tile_still(slot);
        if (t < 0x8Du || t >= 0x99u) continue;
        RAM(0x0514u) = (unsigned char)(RAM(0x0514u) + 1u);
        core_set_type_and_clear_object(0x11u, slot);
        return;
    }
}

/* NES UpdateStatues: Z_04.asm:1578-1752, called by UpdateMode5Play's
 * underworld tail in Z_07.asm:1978. Drain stance: promote the existing
 * src/oracle/enemies/enemy_boss_runtime.c body into the linked native loop.
 * T-171 L5 boss room $24: without this, all four statue shooters vanish. */
static void enemy_loop_update_statues(void)
{
    static const unsigned char layouts[2] = { 0x24u, 0x23u };
    static const unsigned char counts[3] = { 3u, 1u, 1u };
    static const unsigned char start_times[4] = { 0x50u, 0x80u, 0xF0u, 0x60u };
    static const unsigned char base_index[3] = { 0u, 4u, 6u };
    static const unsigned char xs[8] = { 0x24u, 0xC8u, 0x24u, 0xC8u, 0x64u, 0x88u, 0x48u, 0xA8u };
    static const unsigned char ys[8] = { 0xC0u, 0xBCu, 0x64u, 0x5Cu, 0x94u, 0x8Cu, 0x82u, 0x86u };
    unsigned char pattern = 2u;
    unsigned char source_slot;
    signed char fireball_idx;
    extern void c_shoot_fireball(unsigned int type, unsigned int slot);

    if (ENEMY_STATUE_PERSON_FIREBALLS == 0u) {
        unsigned char layout = room_get_unique_room_id();
        if (layout == layouts[0]) pattern = 0u;
        else if (layout == layouts[1]) pattern = 1u;
        else return;
    }

    source_slot = (unsigned char)enemy_find_empty_monster_slot();
    if (source_slot < 6u) return;

    for (fireball_idx = (signed char)counts[pattern];
         fireball_idx >= 0; --fireball_idx) {
        unsigned char idx = (unsigned char)fireball_idx;
        unsigned char timer = (unsigned char)(ENEMY_STATUE_FIREBALL_TIMER(idx) - 1u);
        unsigned char pos_idx;
        unsigned char fire_x, fire_y, mask;
        ENEMY_STATUE_FIREBALL_TIMER(idx) = timer;
        if ((unsigned char)(timer + 1u) != 0u) continue;
        if (ENEMY_RNG_A(idx) >= 0xF0u) continue;

        ENEMY_STATUE_FIREBALL_TIMER(idx) = start_times[ENEMY_RNG_A(idx) & 3u];
        pos_idx = (unsigned char)(idx + base_index[pattern]);
        fire_x = xs[pos_idx];
        fire_y = ys[pos_idx];
        ENEMY_X(source_slot) = fire_x;
        ENEMY_Y(source_slot) = fire_y;

        mask = 3u;
        if ((unsigned char)(ENEMY_PLAYER_OBJ_Y - fire_y) < 0x18u ||
            (unsigned char)(ENEMY_PLAYER_OBJ_Y - fire_y) >= 0xE8u)
            mask = (unsigned char)(mask >> 1);
        if ((unsigned char)(ENEMY_PLAYER_OBJ_X - fire_x) < 0x18u ||
            (unsigned char)(ENEMY_PLAYER_OBJ_X - fire_x) >= 0xE8u)
            mask = (unsigned char)(mask >> 1);
        if (mask != 0u) c_shoot_fireball(0x55u, source_slot);
    }
}

/* NES UpdateMode5Play after the object loop (Z_07.asm:1955): heart
 * warning, then in the OW the sea sound and CheckZora. The UW tail
 * (statues, secrets, shutters, doors) is run by the caller. */
void enemy_loop_play_tail(unsigned char in_uw)
{
    if ((RAM(0x066Fu) & 0x0Fu) == 0u)               /* HeartValues full */
        RAM(0x0604u) = (unsigned char)(RAM(0x0604u) | 0x40u);
    if (in_uw) return;
    if (RAM(0x0012u) == 0x05u)                      /* sea sound effect */
        RAM(0x0603u) = (unsigned char)(RAM(0x0603u) |
            ((RAM(0x687Eu + RAM(0x00EBu)) & 0x04u) << 3));
    enemy_loop_check_zora();
}

void enemy_loop_tick(void)
{
    unsigned int slot;
    /* enemy_fix arm-hook: check magic + consume on first call only. */
    enemy_loop_arm_fix_probe();
    /* combat-death hook: check magic + invoke combat_deal_damage. */
    enemy_loop_combat_force_kill_hook();
    /* Q3=(b): function-pointer table dispatch. NULL = no-op (family
     * not yet wired). Q2=(c) gating done by caller — this function is
     * ONLY called inside the scroll-stable + non-paused branch of the
     * gameplay tick. */
    /* 2026-05-15 perf: cache armed-flag once per tick. is_armed reads
     * 4 volatile RAM cells; saved ~1% PC samples per tick by avoiding
     * two function-call paths to it. */
    unsigned char armed = enemy_loop_probe_is_armed();
    if (armed) {
        enemy_loop_probe_publish_pre();
    }

    /* NES @CheckChaseTarget (Z_07.asm:1855-1914). Sets ChaseTargetX/Y
     * per frame from Link's ObjX/Y; periodically flips to mirror
     * (across-room) coords so walkers chase TOWARD Link instead of
     * away. ChaseLongTimer ($4A) gates the flip; main.c NMI port
     * already decrements it (in $27..$4E range).
     *
     * Probe build/probes/walker_init_{nes,gen}.lua captured Genesis
     * ChaseX/Y stuck at $00/$00 -> InitWalker chose wrong direction
     * (chase-away from origin instead of from mirror-Link).
     *
     * NES Random+1 is at $0019 -- main.c NMI port @ScrambleRandom
     * cycles $18..$24 each frame. */
    {
        unsigned char chase_other = (unsigned char)RAM(0x0060u);
        if (chase_other == 0u) {
            RAM(0x0061u) = (unsigned char)RAM(0x0070u);  /* Chase X = Link X */
            RAM(0x0062u) = (unsigned char)RAM(0x0084u);  /* Chase Y = Link Y */
        }
        /* T-057: the food in slot $0F (UpdateBoomerangOrFood) overrides
         * the target before the ChaseLongTimer flip below. */
        roomrom_food_apply_chase_target();
        if ((unsigned char)RAM(0x004Au) == 0u) {  /* ChaseLongTimer expired */
            RAM(0x004Au) = (unsigned char)((unsigned char)RAM(0x0019u) & 0x07u);
            chase_other = (unsigned char)(chase_other ^ 0x01u);
            RAM(0x0060u) = chase_other;
            if (chase_other != 0u) {
                /* Mirror swap: only if ChaseX still matches Link's X
                 * (Link hasn't moved this frame -- NES idle gate). */
                if ((unsigned char)RAM(0x0061u) == (unsigned char)RAM(0x0070u)) {
                    RAM(0x0061u) = (unsigned char)(RAM(0x0061u) ^ 0xFFu);
                    RAM(0x0062u) = (unsigned char)(RAM(0x0062u) ^ 0xFFu);
                }
            }
        }
    }

    /* Step 20 DecTimers prepass. NES IsrNmi @UpdateTimers /
     * @LoopTimer (z_07.asm:1604-1616) decrements ObjTimer ($0028..)
     * for every slot every VBlank. Walker direction-decision logic
     * (enemy_walker_runtime.c:107) and update_meta_object spark/cloud
     * progression both gate on ObjTimer == 0. Without a tick the
     * walker never picks a new direction and the spawning-cloud /
     * death-spark animation never advances. This prepass is the
     * native equivalent of the VBlank dec loop. */
    {
        /* NES DecrementInvincibilityTimer (Z_07.asm:5756) — every 2 frames
         * (FrameCounter bit 0 == 0), decrement ObjInvincibilityTimer for
         * each slot. Called from @LoopObject (Z_07.asm:1919) once per slot.
         *
         * ObjTimer ($0028+slot) + ObjStunTimer ($003D+slot) are ALREADY
         * decremented by main.c NES @UpdateTimers port (RoomRom/src/main.c
         * line ~1606 loops $27..$3C/$4E). Don't double-dec here. */
        unsigned char dec_inv = ((unsigned char)RAM(0x0015u) & 1u) == 0u;
        if (dec_inv) {
            for (slot = ENEMY_LOOP_SLOT_FIRST; slot <= ENEMY_LOOP_SLOT_LAST; ++slot) {
                if (ENEMY_HIT_REACTION(slot) != 0u) {
                    ENEMY_HIT_REACTION(slot) =
                        (unsigned char)(ENEMY_HIT_REACTION(slot) - 1u);
                }
            }
        }
    }

    /* NES @LoopObject (Z_07.asm:1915) updates objects from $B down to 1:
     * a shot a monster spawns into a lower free slot (FindEmptyMonsterSlot
     * scans down from $B) is initialised later in the same frame. */
    for (slot = ENEMY_LOOP_SLOT_LAST; slot >= ENEMY_LOOP_SLOT_FIRST; --slot) {
        unsigned char t;
        enemy_update_fn fn;
        t = (unsigned char)ENEMY_TYPE(slot);
        if (t == 0u || t >= ENEMY_LOOP_TYPE_MAX) continue;
        ENEMY_THROWER_SLOT = (unsigned char)slot;
        /* NES UpdateObject checks ObjUninitialized ($492, nonzero) before
         * metastate: room objects, shots and dropped items are all
         * initialized on their first update. The loop's wrapper then
         * draws/collides the object if it came out with metastate 0 (an
         * edge monster). */
        if (ENEMY_ALIVE_FLAG(slot) != 0u) {
            enemy_loop_init_object(slot, t);
            if (ENEMY_TYPE(slot) != 0u && ENEMY_ALIVE_FLAG(slot) == 0u)
                loop_object_wrapper(slot);
            continue;
        }
        /* Step 20 metastate gate. NES UpdateObject (Z_07.asm:5275) checks
         * ObjMetastate before falling through to the per-type body — when
         * non-zero, control diverts to UpdateMetaObject which animates the
         * death-spark / spawning-cloud and (on completion) converts the
         * slot to a $60 dropped-item via SetUpDroppedItem. Drained as
         * update_meta_object() in enemy_walker_bridge.c. */
        if (ENEMY_METASTATE(slot) != 0u) {
            update_meta_object(slot);
            /* @LoopObject reads ObjMetastate after UpdateObject: the
             * frame a spawn cloud ends (metastate back to 0) the monster
             * already walks-animates, draws and collides (T-171:
             * t013_route t7046 Goriya ObjAnimCounter 6 vs 1). */
            if (ENEMY_TYPE(slot) != 0u) loop_object_wrapper(slot);
            continue;
        }
        t = (unsigned char)ENEMY_TYPE(slot);
        if (t >= ENEMY_LOOP_TYPE_MAX) continue;
        if (t >= 0x6Au) {
            /* UpdateObject (Z_07.asm:5285): types >= $6A are cave people,
             * updated by UpdateCavePerson once initialized and out of
             * their spawn cloud (T-133, T-143). */
            ENEMY_THROWER_SLOT = (unsigned char)slot;
            cave_update_cave_person(slot);
            loop_object_wrapper(slot);
            continue;
        }
        fn = enemy_update_fns[t];
        /* Task 7.4 step 2a — write CurObjIndex per-slot. NES UpdateObject
         * uses the X register implicitly; drained C primitives that call
         * c_turn_towards_player8 / c_shoot / enrt_shoot read
         * ENEMY_THROWER_SLOT ($0340 = NES CurObjIndex) instead. Closes
         * Task 7.3 known gap (vire split spawn slot computation). */
        ENEMY_THROWER_SLOT = (unsigned char)slot;
        if (fn != 0) fn(slot);
        if (ENEMY_TYPE(slot) == 0u) continue;
        loop_object_wrapper(slot);
    }

    if (armed) {
        enemy_loop_probe_publish_live();
    }

    /* NES UpdateMode5Play: LDA #$0B / STA CurObjIndex after the object
     * loop. CheckHasLivingMonsters scans slots CurObjIndex+1..1, so with the
     * loop's last slot (1) left there it saw only slots 2..1 and declared a
     * room with live keese clear (T-171: t013_route room $52 t5413,
     * RoomAllDead / shutter trigger early). */
    ENEMY_THROWER_SLOT = 0x0Bu;

    if (RAM(0x0010u) != 0u) enemy_loop_update_statues();

    /* NES UpdateMode5Play @CheckUW: CheckUnderworldSecrets runs only with
     * CurLevel != 0 (T-171: the OW start room counted RoomAllDead up).
     * room_check_underworld_secrets is the whole NES routine: every secret
     * trigger (all dead, ringleader, last boss, block door, block stairs,
     * money or life, foes for item) and the room item activation; the
     * door state opens shutters from ShutterTrigger ($4CE). */
    if ((unsigned char)RAM(0x0010u) != 0u)
        room_check_underworld_secrets();
}

void enemy_loop_force_spawn_slow_octorock(unsigned int slot,
                                          unsigned char x,
                                          unsigned char y,
                                          unsigned char dir)
{
    /* Q4=(c) first probe seed: one slow octorok at known (x,y,dir).
     * Used by tools/debug/probes/probe_walker_parity.lua to capture a
     * 60-frame X/Y/DIR trace at matched RNG seed for diff vs NES.
     *
     * Step 2 writes state cells only (no enrt_init_ call yet — see
     * file-level comment). Probe verifies iterator + type lookup +
     * force_spawn cell writes. Task 7.3 wires walker init + ticks. */
    enemy_loop_force_spawn_typed(slot, 0x07u, x, y, dir);
}

void enemy_loop_force_spawn_typed(unsigned int slot,
                                  unsigned char enemy_type,
                                  unsigned char x,
                                  unsigned char y,
                                  unsigned char dir)
{
    /* Step 8 generic seed. Probe-side hook used to verify step-7
     * dispatch rows ($03/$04 moblin, $05/$06 goriya, $2A stalfos)
     * actually tick without crashing. Same shape as the octorok
     * seed: clear scratch, set type/x/y/dir, dispatch init row. */
    enemy_init_fn fn;
    if (slot < ENEMY_LOOP_SLOT_FIRST || slot > ENEMY_LOOP_SLOT_LAST) return;
    if (enemy_type >= ENEMY_LOOP_TYPE_MAX) return;

    ENEMY_TYPE(slot) = enemy_type;
    ENEMY_X(slot) = x;
    ENEMY_Y(slot) = y;
    clear_slot_scratch(slot);
    /* NES @InitObject preamble (Z_07.asm:5541-5557): set DIR=0 so
     * per-type init can compute it from ChaseTarget. Pre-2026-05-21
     * the arm-hook wrote caller-supplied dir BEFORE init_fn, causing
     * enrt_init_walker (Z_04.asm:209) early-return when DIR!=0 — and
     * walker init's WALK_SPEED/MOVE_TIMER/STATE never landed. Result:
     * arm-spawned enemies stuck at spawn cell with qspd=0. */
    ENEMY_DIR(slot) = 0;
    native_init_obj_hp(slot, enemy_type);    /* NES Z_07.asm:5576 HP nibble */
    native_init_obj_attr(slot, enemy_type);  /* NES Z_07.asm:5566 @FetchAttrs */

    /* NES @InitObject cloud-spawn preamble (Z_07.asm:5543-5557): for
     * cloud-spawn types (most enemies), ObjTimer = slot (cloud-anim
     * countdown) + ObjMetastate = $01 (cloud state). Skip for Armos
     * ($1E), FlyingGhini ($22), and projectile types ($53+) per NES
     * CPY/BEQ at lines 5550-5555.
     *
     * 2026-05-21 — metastate cell at $0405+slot per
     * reference/aldonunez/Variables.inc (`ObjMetastate := $405`).
     * Previously wrote $04D8 which is GleeokSignedRefSegmentDistance —
     * harmless aliasing but invisible to update_meta_object (which
     * reads via ENEMY_METASTATE macro = $0405). */
    if (enemy_type != 0x1Eu && enemy_type != 0x22u && enemy_type < 0x53u) {
        ENEMY_MOVE_TIMER(slot) = (unsigned char)slot;  /* ObjTimer $0028+slot */
        ENEMY_METASTATE(slot)  = 0x01u;                /* ObjMetastate $0405+slot = cloud */
    }

    /* Per-NES-probe-parity init contract (2026-05-21): mirror the
     * per-family init cells that the NES Lua probe pokes so Gen-vs-NES
     * diff measures TICK-EVOLUTION parity (how state machine advances)
     * not INIT-CONTRACT skew. enrt_init_walker only sets ENEMY_DIR;
     * walker types need WALK_SPEED + ANIM_TIMER for tick to advance.
     * Per-family overrides match NES Init<Family> bodies. */
    {
        /* Default walker init (Lynel/Moblin/Goriya/Vire/Stalfos/etc) */
        unsigned char qspd = 0x20u;
        if (enemy_type == 0x08u || enemy_type == 0x0Au)         qspd = 0x30u;  /* Fast Octorok */
        else if (enemy_type == 0x0Cu)                            qspd = 0x28u;  /* Red Darknut */
        else if (enemy_type == 0x2Bu || enemy_type == 0x2Cu ||
                 enemy_type == 0x2Du)                            qspd = 0x40u;  /* Bubble */
        RAM(0x03BCu + slot) = qspd;                  /* ENEMY_WALK_SPEED / ObjQSpeedFrac */
        RAM(0x03D0u + slot) = 0x10u;                 /* ENEMY_ANIM_TIMER / ObjAnimCounter */
        RAM(0x03E4u + slot) = 0x00u;                 /* ENEMY_DRAW_FRAME / ObjAnimFrame */
    }

    fn = enemy_init_fns[enemy_type];
    if (fn != 0) fn(slot);
    ENEMY_ALIVE_FLAG(slot) = 0u;   /* initialized */

    /* Caller can still override DIR after init (e.g., probe wants
     * fixed direction). Pass dir=0 to use chase-target computation. */
    if (dir != 0) ENEMY_DIR(slot) = dir;
}

unsigned int enemy_loop_alive_count(void)
{
    unsigned int slot, n = 0u;
    for (slot = ENEMY_LOOP_SLOT_FIRST; slot <= ENEMY_LOOP_SLOT_LAST; ++slot) {
        if (ENEMY_TYPE(slot) != 0u) n++;
    }
    return n;
}

unsigned char enemy_loop_get_type(unsigned int slot)
{
    if (slot < ENEMY_LOOP_SLOT_FIRST || slot > ENEMY_LOOP_SLOT_LAST) return 0u;
    return (unsigned char)ENEMY_TYPE(slot);
}
