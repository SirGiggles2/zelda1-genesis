from __future__ import annotations

import os
import re
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SGDK = ROOT / "sgdk"
TOOLBIN = ROOT / "sgdk" / "bin"   # SGDK's bundled m68k-elf GCC 13.2 (Windows; Wine elsewhere)
LIB = SGDK / "lib"
PROJ = ROOT / "build" / "rom_project"
OUT = PROJ / "out"
ROM_RAW = OUT / "Zelda_raw.md"
ROM_OUT = ROOT / "builds" / "Zelda.md"

GCC = TOOLBIN / "gcc.exe"
OBJCOPY = TOOLBIN / "objcopy.exe"
NM = TOOLBIN / "nm.exe"
# NES RAM mirror (A4 base, platform_abi.h). .bss must end before the heap's
# reserved block header at $FF7FFE (src/platform/game_main.c heap wall).
NES_MIRROR_RESERVED = 0xFF7FFE

# Non-Windows hosts (cloud sessions) run the same bundled SGDK toolchain
# under Wine: same compiler, same libmd.a, same ROM as the Windows build.
# (The distribution m68k-linux-gnu-gcc is not usable: its ABI returns
# pointers in a0, and it merges byte accesses into odd-address word/long
# accesses that fault on the 68000.)
WINE = os.name != "nt"
if WINE:
    os.environ.setdefault("WINEDEBUG", "-all")


def exe(tool: Path) -> list[str | Path]:
    return ["wine", tool] if WINE else [tool]


CFLAGS = [
    "-DSGDK_GCC",
    "-DROOMROM_NO_STANDALONE_MAIN",
    "-m68000",
    "-Wall",
    "-Wno-main",
    "-Wno-unused-parameter",
    "-fno-builtin",
    "-ffunction-sections",
    "-fdata-sections",
    "-fms-extensions",
    "-O3",
    "-fomit-frame-pointer",
    "-ffixed-a4",
]
# build.py --music: your own VGMs (tools/converter/local_music.py writes
# build/local_music/local_music.c). Off by default; local builds only.
LOCAL_MUSIC = os.environ.get("ZELDA_LOCAL_MUSIC") == "1"
NES_MUSIC = os.environ.get("ZELDA_NES_MUSIC") == "1"
if NES_MUSIC:
    CFLAGS += ["-DZELDA_NES_MUSIC", f"-I{ROOT / 'build' / 'nes_music'}"]
if LOCAL_MUSIC:
    CFLAGS += ["-DZELDA_LOCAL_MUSIC", f"-I{ROOT / 'build' / 'local_music'}"]

# T-118: link-time optimization for C translation units. The gameplay
# frame makes ~270 small cross-file calls (drained NES routines call each
# other across TUs); on the 68000 each costs ~80-120 cycles of JSR/RTS,
# argument pushes and register saves that -O3 cannot remove across files.
# PC profile, L1 room $53 with 5 Stalfos + bomb: gameplay lag frames 13 -> 1.
# Assembly units and SGDK libmd.a stay non-LTO. LTO_LINK_FLAGS repeats the
# codegen flags that matter at link time (LTO re-runs code generation):
# -ffixed-a4 keeps the nes_ram register binding (platform_abi.h).
# One partition so every TU sees every other for inlining.
LTO_CFLAGS = ["-flto"]
LTO_LINK_FLAGS = [
    "-flto",
    "-flto-partition=none",
    "-O3",
    "-fomit-frame-pointer",
    "-ffixed-a4",
    "-fno-builtin",
    "-fms-extensions",
]

INCS = [
    ROOT / "src",
    ROOT / "src" / "abi",
    ROOT / "src" / "platform",
    ROOT / "src" / "frontend",
    ROOT / "src" / "frontend" / "intro",
    ROOT / "src" / "sgdk_adapter",
    ROOT / "engine" / "src",
    ROOT / "src" / "state",
    ROOT / "src" / "game",
    ROOT / "src" / "game" / "cave",
    ROOT / "src" / "game" / "cave" / "probes",
    ROOT / "src" / "game" / "world",
    ROOT / "src" / "game" / "world" / "probes",
    ROOT / "src" / "game" / "core",
    ROOT / "src" / "game" / "enemies",
    ROOT / "src" / "game" / "enemies" / "probes",
    ROOT / "src" / "game" / "combat",
    ROOT / "src" / "game" / "room",
    ROOT / "src" / "game" / "hud",
    ROOT / "src" / "game" / "hud" / "probes",
    ROOT / "src" / "game" / "items",
    ROOT / "src" / "game" / "options",
    ROOT / "src" / "game" / "options" / "probes",
    ROOT / "src" / "game" / "audio",
    ROOT / "src" / "state" / "probes",
    ROOT / "src" / "oracle" / "room",
    ROOT / "src" / "core",
    ROOT / "data" / "audio",
    ROOT / "data" / "audio_music",
    SGDK / "inc",
    SGDK / "res",
]

TITLE_C_SOURCES = [
    ("src/sgdk_adapter/render_adapter.c", "render_adapter.o"),
    # Phase 10.3 audio link, VBlank tick slice + XGM SFX path.
    ("src/sgdk_adapter/audio_vblank_hook.c", "audio_vblank_hook.o"),
    ("src/sgdk_adapter/audio_adapter.c",    "audio_adapter.o"),
    ("data/audio/sfx_pcm.c",                "sfx_pcm.o"),
    ("data/audio/sfx_pcm_stairs.c",         "sfx_pcm_stairs.o"),
    ("data/audio/sfx_pcm_noise.c",          "sfx_pcm_noise.o"),   # NES noise effects (tools/audio/synth_noise_sfx.py)
    ("data/audio/sfx_pcm_tunes.c",          "sfx_pcm_tunes.o"),   # NES square tunes (tools/audio/synth_square_sfx.py)
    # OW/UW themes: XGC binaries (xgmtool output) dispatched via
    # src/sgdk_adapter/audio_adapter.c::audio_music_play(SONG_OW=$01) →
    # XGM_startPlay(*_theme_xgm).
    ("data/audio_music/ow_theme_xgm.c",     "ow_theme_xgm.o"),
    ("data/audio_music/uw_theme_xgm.c",     "uw_theme_xgm.o"),
    # Alternate underworld theme (Cyberdeous), picked in Options > UW MUSIC.
    ("data/audio_music/uw_theme_cyberdeous_xgm.c", "uw_theme_cyberdeous_xgm.o"),
    ("src/frontend/intro/intro_phase.c", "intro_phase.o"),
    ("src/frontend/intro/intro_title.c", "intro_title.o"),
    ("src/frontend/intro/intro_story.c", "intro_story.o"),
    ("data/intro/intro_font_chr.c", "intro_font_chr.o"),
    ("data/intro/intro_art_chr.c", "intro_art_chr.o"),
    ("data/intro/intro_palette.c", "intro_palette.o"),
    ("data/intro/intro_story_tilemap.c", "intro_story_tilemap.o"),
    ("data/intro/intro_title_bg_chr.c", "intro_title_bg_chr.o"),
    ("data/intro/intro_title_sprite_chr.c", "intro_title_sprite_chr.o"),
    ("data/intro/intro_title_palette.c", "intro_title_palette.o"),
    ("data/intro/intro_title_tilemap.c", "intro_title_tilemap.o"),
    ("data/intro/intro_title_fade.c", "intro_title_fade.o"),
    ("data/intro/intro_title_glow.c", "intro_title_glow.o"),
    ("data/intro/intro_common_bg_chr.c", "intro_common_bg_chr.o"),
    ("data/intro/intro_sprite_chr.c", "intro_sprite_chr.o"),
    ("data/intro/intro_misc_chr.c", "intro_misc_chr.o"),
    ("src/frontend/intro/intro_punct_chr.c", "intro_punct_chr.o"),
    ("data/intro/intro_blink_chr.c", "intro_blink_chr.o"),
    ("data/intro/intro_combined_palette.c", "intro_combined_palette.o"),
    ("data/intro/intro_treasures_tilemap.c", "intro_treasures_tilemap.o"),
    # Phase 9 Task 9.3 — File Select OPTIONS submenu (compile-only; FS
    # frontend wire-up into Zelda.md gameplay path is a follow-up task).
    ("src/frontend/fs/fs_options.c", "fs_options.o"),
    # File Select: the implementation existed but only the options
    # submenu was linked, so Zelda.md had no FS at all.
    ("src/frontend/fs/fs_main.c",   "fs_main.o"),
    ("src/frontend/fs/fs_render.c", "fs_render.o"),
    ("src/frontend/fs/fs_input.c",  "fs_input.o"),
    ("src/frontend/fs/fs_phase.c",  "fs_phase.o"),
    ("src/frontend/fs/fs_handoff.c","fs_handoff.o"),
    ("data/fs/fs_bg_chr_full.c",    "fs_bg_chr_full.o"),
    ("data/fs/fs_link_sprite_chr.c","fs_link_sprite_chr.o"),
    ("data/fs/fs_heart_cursor_chr.c","fs_heart_cursor_chr.o"),
    ("data/fs/fs_palette.c",        "fs_palette.o"),
    ("data/fs/fs_static_tilemap.c", "fs_static_tilemap.o"),
    ("data/fs/fs_static_attr.c",    "fs_static_attr.o"),
    ("data/fs/fs_font_chr.c",       "fs_font_chr.o"),
    ("data/fs/fs_border_chr.c",     "fs_border_chr.o"),
    ("src/frontend/fs/fs_options_render.c", "fs_options_render.o"),
    # 2026-05-19 — title-MODE-button debug tile-grid scene for atlas audit.
    ("src/game/debug/debug_tilegrid.c", "debug_tilegrid.o"),
    # A+B+C+Start anywhere: freeze and page through all state (docs/debug/state_dump.md).
    ("src/game/debug/state_dump.c", "state_dump.o"),
    ("src/game/debug/state_dump_font.c", "state_dump_font.o"),  # tools/build/gen_dump_font.py
]

ROOMROM_C_SOURCES = [
    ("src/game/cave/cave_dispatch.c", "cave_dispatch.o"),
    ("src/game/cave/cave_entrance.c", "cave_entrance.o"),
    ("src/game/world/world_dispatch.c", "world_dispatch.o"),
    ("src/game/world/object_dispatch.c", "object_dispatch.o"),
    ("src/game/world/sprite_dispatch.c", "sprite_dispatch.o"),
    ("src/game/world/progress_dispatch.c", "progress_dispatch.o"),
    ("src/game/world/trap_dispatch.c", "trap_dispatch.o"),
    ("src/game/core/core_dispatch.c", "core_dispatch.o"),
    ("src/game/enemies/enemy_dispatch.c", "enemy_dispatch.o"),
    ("src/game/combat/collision_dispatch.c", "collision_dispatch.o"),
    ("src/game/room/room_dispatch.c", "room_dispatch.o"),
    ("src/game/hud/hud_dispatch.c", "hud_dispatch.o"),
    ("src/game/items/weapon_dispatch.c", "weapon_dispatch.o"),
    ("src/game/combat/targeting_dispatch.c", "targeting_dispatch.o"),
    ("src/game/combat/combat_dispatch.c", "combat_dispatch.o"),
    ("src/game/cave/uw_person_dispatch.c", "uw_person_dispatch.o"),
    ("src/game/combat/link_collision_dispatch.c", "link_collision_dispatch.o"),
    ("src/game/world/draw_dispatch.c", "draw_dispatch.o"),
    # Phase 7 Task 7.4 step 6c — native ChangeTileObjTiles drain. Shared
    # play-area dynamic-tile editing primitives consumed by armos secret
    # reveals, push-block secrets, bombable walls, burning brush.
    ("src/game/world/dyn_tile_dispatch.c", "dyn_tile_dispatch.o"),
    # Phase 9.7 — Modes 8/11/12 native bodies + gameplay-mode dispatcher.
    ("src/game/world/mode_continue_question.c", "world_mode_continue_question.o"),
    ("src/game/world/mode_death.c",             "world_mode_death.o"),
    ("src/game/world/mode_endlevel.c",          "world_mode_endlevel.o"),
    ("src/game/world/mode_save.c",              "world_mode_save.o"),
    ("src/game/world/mode_dispatch.c",          "world_mode_dispatch.o"),
    # Substrate fix 2026-05-15 — install NES SRAM LBA + LevelInfo at boot.
    ("src/game/world/level_info_install.c",     "level_info_install.o"),
    # Plan v5a Tier-1 bridge 2026-05-16 — sync C-side state into NES RAM
    # mirror cells ($00FA/$00FB input, $066F/$0670 hearts, $008C face).
    ("src/state/nes_ram_sync.c",                "nes_ram_sync.o"),
    # Persistent save bridge 2026-08-04: save_serializer.c builds slot
    # images in the A4 mirror ($FFE000); sram_adapter.c moves bytes via a
    # different mirror ($FF6000). save_game.c owns the copy between them,
    # which is why save_slot_serialize had no callers before now.
    ("src/state/save_game.c",                   "save_game.o"),
    # Cart SRAM backend. sram_adapter.c's _sram_* externs claimed to come
    # from src/nes_io.asm, which is NOT linked into Zelda.md (nor is
    # genesis_shell.asm, which called _sram_load_save_slots at boot), so
    # Zelda.md had no cart SRAM at all. Implemented on SGDK's SRAM API in
    # the adapter layer per SGDK-1 rather than linking 142 KB of ASM.
    ("src/sgdk_adapter/sram_backend.c",         "sram_backend.o"),
    ("src/sgdk_adapter/sram_adapter.c",         "sram_adapter.o"),
    # Plan v5b Tier-5 T5.5 2026-05-16 — audio dispatcher: gamemode+scene
    # tuple change -> single music_play() per docs/audit/audio_routing.md.
    ("src/game/audio/audio_dispatch.c",         "audio_dispatch.o"),
    ("src/game/audio/audio_requests.c",         "audio_requests.o"),  # NES Sample/EffectRequest consumer
    # Phase 7 enemy render bridge — NES OAM mirror -> Genesis SAT sweep.
    ("src/game/enemies/enemy_render.c",         "enemy_render.o"),
    # Phase 7 substrate — ROOM_BOUNDS setup (drained roomld_setup_obj_room_bounds).
    ("src/oracle/room/room_load_runtime.c",     "room_load_runtime.o"),
    ("src/game/items/item_dispatch.c", "item_dispatch.o"),
    # Plan v5c — dropped-item ($60) slot UPDATE port (NES UpdateItem @
    # Z_04.asm:11236). Wires enemy_update_fns[0x60] so drops decay +
    # Link bbox pickup triggers item_take_item().
    ("src/game/items/item_object.c",   "items_item_object.o"),
    # Plan v5c — asm-bound tables (ItemIdToSlot/ItemIdToDescriptor +
    # MenuPalettesTransferBuf/SaveSlotToPaletteRowOffset) needed once
    # item_take_item() becomes reachable (item_object_update calls it).
    ("src/game/items/item_tables.c",   "items_item_tables.o"),
    ("engine/src/main.c", "roomrom_main.o"),
    ("src/game/world/render/ow_render.c", "world_ow_render.o"),  # Phase 12.2 promoted
    ("src/game/hud/hud_runtime.c", "hud_runtime.o"),  # Phase 12.2 promoted (SGDK-1 clean)
    # Plan v5b T2.7 2026-05-16 — heart-container 3-frame scale-up anim.
    ("src/game/hud/heart_container_anim.c", "heart_container_anim.o"),
    ("src/game/dungeon/uw_render.c", "dungeon_uw_render.o"),  # Phase 12.2 promoted
    ("src/game/dungeon/uw_map_builder.c", "dungeon_uw_map_builder.o"),
    ("engine/src/uw_room_blob.c", "uw_room_blob.o"),
    ("engine/src/uw_collision_data.c", "uw_collision_data.o"),
    # Phase 12.2 family 7: dungeon meta promoted to src/game/dungeon/.
    ("src/game/dungeon/walk_model.c",  "dungeon_walk_model.o"),
    ("src/game/dungeon/door_state.c",  "dungeon_door_state.o"),
    ("src/game/world/render/sprite_render.c", "world_sprite_render.o"),  # Phase 12.2 promoted (SGDK-1 clean)
    ("src/game/combat/combat_runtime.c", "combat_runtime.o"),  # Phase 12.2 promoted (SGDK-1 cleanup)
    # Phase 12.2 family 3: items promoted to src/game/items/.
    ("src/game/items/boomerang.c", "items_boomerang.o"),
    ("src/game/items/arrow.c",     "items_arrow.o"),
    ("src/game/items/bomb.c",      "items_bomb.o"),
    ("src/game/items/debug_unlock_all.c", "items_debug_unlock_all.o"),  # P6.1
    ("src/game/inventory/inventory_render.c", "inventory_render.o"),    # P6.2
    ("src/game/inventory/inventory_palette.c", "inventory_palette.o"),  # L4 Phase 7 v2 CRAM swap
    ("src/game/inventory/inventory_tilemap.c", "inventory_tilemap.o"),  # V2.1 NES subscreen NT blob
    ("src/game/inventory/inventory_sprite_chr.c", "inventory_sprite_chr.o"),  # live subscreen item icons
    ("src/game/inventory/inventory_uw_tilemap.c", "inventory_uw_tilemap.o"),  # UW dungeon subscreen NT
    ("src/game/world/bg_palette.c", "world_bg_palette.o"),  # Phase 12.2 promoted
    ("src/game/world/transfer_buf_drain.c", "world_transfer_buf_drain.o"),  # Plan v5b TRANSFER_BUF -> CRAM bridge
    ("src/game/world/render/cave_palette.c", "world_cave_palette.o"),  # Tier 0 #42 cave palette swap
    ("src/game/world/render/cave_fade.c", "world_cave_fade.o"),  # Tier 1 cave entry/exit fade sequencer
    ("src/game/world/mode_wingame.c", "world_mode_wingame.o"),  # T-098 native ending init/flash/text owner
    ("src/game/world/ending_render.c", "world_ending_render.o"),
    ("src/game/world/scene_load.c", "world_scene_load.o"),  # Phase 12.2 promoted
    # Task 5.4: warp coordinator + OW metadata accessor + level/quest table + Gate D probe
    # Phase 12.2 family 6 partial: SGDK-1-clean world TUs promoted.
    ("src/game/world/ow_meta.c",    "world_ow_meta.o"),
    ("src/game/world/transition.c", "world_transition.o"),
    ("src/game/world/ow_scroll.c", "ow_scroll.o"),
    ("src/game/world/link_ladder.c", "world_link_ladder.o"),  # T-056 NES ladder (CheckLadder + setup)
    ("src/game/world/dock.c", "world_dock.o"),  # T-056 NES UpdateDock (raft)
    ("engine/data/levelinfo_start_rooms.c", "levelinfo_start_rooms.o"),
    ("engine/src/probes/metadata_probe.c", "metadata_probe.o"),
    # Task 5.5: door-type expected table for L1Q1 verification
    ("engine/data/uw_l1q1_expected_doors.c", "uw_l1q1_expected_doors.o"),
    # Task 5.6: cellar pair table + accessor module + LevelInfo offsets
    ("data/rooms/dungeons_offsets.c", "dungeons_offsets.o"),
    ("engine/data/uw_l1q1_cellar_pairs.c", "uw_l1q1_cellar_pairs.o"),
    ("src/game/dungeon/cellar_mode.c", "dungeon_cellar_mode.o"),  # T-187 native lifecycle
    ("src/game/dungeon/cellar_meta.c", "dungeon_cellar_meta.o"),  # Phase 12.2 promoted
    # Task 5.7: push-block manifest + accessor + state machine
    ("src/game/world/pushblock.c", "world_pushblock.o"),  # Phase 12.2 promoted
    # Task 5.8: dark-room manifest + accessor + lit-state
    ("engine/data/uw_dark_rooms.c", "uw_dark_rooms.o"),
    ("src/game/dungeon/dark_meta.c", "dungeon_dark_meta.o"),  # Phase 12.2 promoted
    ("src/game/dungeon/uw_dark.c", "dungeon_uw_dark.o"),  # T-111 dark rooms by palette
    ("src/game/dungeon/link_doorway.c", "dungeon_link_doorway.o"),  # T-131 NES Link_FilterInput + CheckDoorway
    # Task 5.9: item-room manifest + accessor + pickup wrapper
    ("engine/data/uw_item_rooms.c", "uw_item_rooms.o"),
    ("src/game/dungeon/item_room_meta.c", "dungeon_item_room_meta.o"),  # Phase 12.2 promoted
    # Task 5.8.1: candle fire projectile (slot 8)
    # Phase 12.2 family 3: items promoted to src/game/items/.
    ("src/game/items/candle_fire.c", "items_candle_fire.o"),
    ("src/game/items/sword_shot.c",  "items_sword_shot.o"),
    ("src/game/world/ow_palette.c", "world_ow_palette.o"),  # Phase 12.2 promoted
    ("src/game/world/ow_bg_palram_table.c", "world_ow_bg_palram_table.o"),  # Plan v5: NES per-room BG palram capture
    ("src/state/palette_tick.c", "palette_tick.o"),
    # Task 6.1: PlayerState[4] shape (Phase 13 multiplayer-ready by construction).
    ("src/state/player_state.c", "player_state.o"),
    # Task 6.10.4: inventory_t struct mirroring NES Variables.inc cells.
    # Phase 12.2 family migration: substrate-singletons promoted to src/state/.
    # engine/src/inventory.c -> src/state/inventory.c; consumer includes
    # point at the new path directly (no shim header in engine).
    ("src/state/inventory.c", "inventory.o"),
    # Task 6.10.1: Paused flag (NES $E0).
    # Phase 12.2: pause state promoted to src/state/pause_state.c.
    ("src/state/pause_state.c", "pause_state.o"),
    # Task 6.11.1/6.11.3: HeartValues damage path + ObjInvincibilityTimer.
    # Phase 12.2 family 4: link_damage promoted to src/game/combat/.
    # combat_runtime not yet promoted (SGDK-1 violation: includes <genesis.h>).
    ("src/game/combat/link_damage.c", "combat_link_damage.o"),
    ("src/game/world/palette_tick_runtime.c", "world_palette_tick_runtime.o"),  # Phase 12.2 promoted (renamed to avoid clash with src/state/palette_tick.c)
    # Task 7.1: enemy framework (RNG byte-for-byte port of @ScrambleRandom).
    # Phase 12.2: RNG state promoted to src/state/rng_state.c.
    ("src/state/rng_state.c", "rng_state.o"),
    # Task 7.2: walker-family drain (octorok / moblin / stalfos / goriya /
    # darknut / rope / gel). enemy_walker_runtime.c carries init+update for
    # walker types; enemy_wanderer_runtime.c is the perpendicular-turn
    # helper; enemy_common_runtime.c is the shared zol/gel update; c_wanderer.c
    # is the c_walker_move primitive.
    ("src/oracle/enemies/c_wanderer.c", "oracle_c_wanderer.o"),
    ("src/oracle/enemies/enemy_common_runtime.c", "oracle_enemy_common.o"),
    ("src/oracle/enemies/enemy_wanderer_runtime.c", "oracle_enemy_wanderer.o"),
    ("src/oracle/enemies/enemy_walker_runtime.c", "oracle_enemy_walker.o"),
    # Task 7.2 step 12: shot UPDATE rows ($53/$54/$57-$5A monster shots,
    # $55/$56 fireballs). enemy_projectile_runtime.c carries
    # enrt_update_monster_shot / enrt_update_fireball / enrt_destroy_monster_shot
    # + L_DrawShot fall-through.
    ("src/oracle/enemies/enemy_projectile_runtime.c", "oracle_enemy_projectile.o"),
    # Task 7.3 step 1: flyer/jumper-family drain (keese, peahat). Drain Rule
    # D1 ADOPT — enemy_flyer_runtime.c carries enrt_init_peahat +
    # enrt_update_keese. --gc-sections strips until dispatch rows wire
    # them in step 2+.
    ("src/oracle/enemies/enemy_flyer_runtime.c", "oracle_enemy_flyer.o"),
    # Task 7.3 step 7: boss-family runtime TU. enrt_update_vire chain
    # ($12 Vire) lives here. --gc-sections + -ffunction-sections retains
    # only enrt_update_vire transitive callees; aquamentus/jumper/gleeok/
    # dodongo/manhandla/lamnola bodies stay stripped until their rows wire.
    ("src/oracle/enemies/enemy_boss_runtime.c", "oracle_enemy_boss.o"),
    # Phase 8 Task 8.3: Dodongo drained primitives. enrt_init_dodongo +
    # enrt_dodongo_check_collisions / _check_bomb_hit / _draw +
    # enrt_update_dodongo_state2_stunned + enrt_update_dodongo_state1_bloated_sub_die
    # + enrt_update_dodongo_bloated_sub_end live here. Native bridge body
    # for UpdateDodongo composes them in src/game/enemies/bosses/boss_dodongo.c.
    ("src/oracle/enemies/enemy_dodongo_runtime.c", "oracle_enemy_dodongo.o"),
    # Phase 8 Task 8.4: Manhandla drained primitives. enrt_init_manhandla +
    # enrt_update_manhandla (full UpdateManhandla body) +
    # enrt_manhandla_set_all_segments_direction / _check_collisions /
    # _move / _draw live here. Callee shims in
    # src/game/enemies/bosses/boss_manhandla.c.
    ("src/oracle/enemies/enemy_manhandla_runtime.c", "oracle_enemy_manhandla.o"),
    # Phase 8 Task 8.5: Gleeok drained segment-mgmt primitives.
    # enrt_init_gleeok_head + enrt_update_gleeok +
    # enrt_gleeok_check_collisions / _store_ref_seg_distance /
    # _set_segment_x/y / _contract_segment_x/y/segment / _dec_head_timer /
    # _ignore_segment live here. Native InitGleeok + UpdateGleeokHead +
    # 8 c_gleeok_* primitives in src/game/enemies/bosses/boss_gleeok.c.
    ("src/oracle/enemies/enemy_gleeok_runtime.c", "oracle_enemy_gleeok.o"),
    # Task 7.5 step 4: Wallmaster scratch/draw helpers. Drained
    # enrt_wallmaster_calc_start_position +
    # enrt_wallmaster_put_sprite{,s}_behind_bg_if_needed live here. Pulled
    # in by enemy_special_bridge.c's enrt_update_wallmaster ($27 UPDATE).
    ("src/oracle/enemies/enemy_wallmaster_runtime.c", "oracle_enemy_wallmaster.o"),
    # Task 7.2 step 2: enemy slot iterator + dispatch + ObjLists port (WT-5
    # promotion — gameplay code under src/game/, not engine/).
    ("src/game/enemies/enemy_loop.c", "game_enemy_loop.o"),
    ("src/game/enemies/obj_lists.c", "game_enemy_obj_lists.o"),
    # Task 7.2 step 4: walker UPDATE primitives bridge — forwarders to
    # already-drained native bodies (link_collision/draw/sprite/core) +
    # Walker_Move stub. Per debate 2026-05-09 verdict (Option C). Wired
    # into UPDATE table below; --gc-sections retains only what enrt_update_*
    # transitively reaches.
    ("src/game/enemies/enemy_walker_bridge.c", "game_enemy_walker_bridge.o"),
    # Step 12: shot UPDATE primitives bridge (forwarders for c_move_object,
    # z01_bound_by_room, z07_destroy_monster, etc — same model as walker_bridge).
    ("src/game/enemies/enemy_projectile_bridge.c", "game_enemy_projectile_bridge.o"),
    # Task 7.3 step 3: flyer UPDATE primitives bridge — Directions8 +
    # c_move_flyer/c_control_keese_flight/c_reset_shove_info/
    # c_draw_object_mirrored_with_frame, with Flyer_Chase + Flyer_Wander
    # native drains from NES Z_04.asm:11707/11844.
    ("src/game/enemies/enemy_flyer_bridge.c", "game_enemy_flyer_bridge.o"),
    # Task 7.3 step 4: zol/gel UPDATE primitives bridge — forwarders to
    # already-drained enemy_common_runtime.c twins
    # (c_update_zol_state/c_zol_check_collisions/c_gel_move/
    # c_gel_check_collisions) + native c_shoot_limited drain
    # (NES Z_04.asm:11369). Wires $13 Zol / $14 RedZol / $15 Gel rows.
    ("src/game/enemies/enemy_common_bridge.c", "game_enemy_common_bridge.o"),
    # Task 7.3 step 7: vire UPDATE primitives bridge — c_gel_move_splitting,
    # z04_update_common_wanderer, c_anim_advance_and_fetch,
    # c_find_empty_monster_slot, c_shoot. Forwarders to drained twins in
    # enemy_common_runtime / enemy_wanderer_runtime / enemy_runtime / boss
    # runtime + sprite_dispatch back-end. Wires $12 Vire UPDATE row.
    ("src/game/enemies/enemy_boss_bridge.c", "game_enemy_boss_bridge.o"),
    # Phase 7 Task 7.4 step 2a — jumper/projectile bridge.
    # TektiteStartingDirs data drain + c_bound_flyer forwarder +
    # z07_find_empty_monster_slot native body. Wires $1F BoulderSet +
    # $20 Boulder dispatch rows.
    ("src/game/enemies/enemy_jumper_bridge.c", "game_enemy_jumper_bridge.o"),
    # Phase 7 Task 7.5 step 2 — special-enemy UPDATE bridge.
    # Native enrt_update_like_like body (NES Z_04.asm:6818) — the top-level
    # UPDATE state machines for $16 PolsVoice / $17 LikeLike / $27
    # Wallmaster are not directly drained, only their helpers are; this
    # bridge carries the per-line NES translation (same model as
    # enemy_boss_bridge.c for Aquamentus / Vire).
    ("src/game/enemies/enemy_special_bridge.c", "game_enemy_special_bridge.o"),
    # Phase 8 Task 8.1 — Boss Framework. Native CreateRoomObjects body
    # (Z_05.asm:8154-8250) wires the room-item slot 19 reward path.
    # Per-boss INIT/UPDATE bodies live in enemy_boss_bridge.c +
    # enemy_boss_runtime.c; this TU only carries the framework shell.
    ("src/game/enemies/bosses/boss_framework.c", "game_enemy_boss_framework.o"),
    # Phase 8 Task 8.3 — Dodongo bridge body. UpdateDodongo native umbrella
    # (Z_04.asm:5856) plus native State0_Move + State1_Bloated dispatcher +
    # Sub_Wait that the drain doesn't carry. Forwards c_get_object_middle /
    # c_check_monster_sword_collision / z07_update_dead_dummy /
    # z04_update_dodongo_bloated_sub_end to the dispatcher entry points
    # already in the link.
    ("src/game/enemies/bosses/boss_dodongo.c", "game_enemy_boss_dodongo.o"),
    # Phase 8 Task 8.4 — Manhandla callee shims. Resolves
    # c_turn_randomly_dir8 / c_play_boss_hit_cry_if_needed /
    # c_play_boss_death_cry / c_draw_object_mirrored to dispatcher entry
    # points (enemy_play_boss_*_cry, draw_object_mirrored) for the
    # drained enemy_manhandla_runtime.c body.
    ("src/game/enemies/bosses/boss_manhandla.c", "game_enemy_boss_manhandla.o"),
    # Phase 8 Task 8.5 — Gleeok native bridge. Carries InitGleeok
    # (Z_04.asm:7649) + UpdateGleeokHead (Z_04.asm:8527) + 8 c_gleeok_*
    # primitives (draw_body, fetch_neck_addrs, move_neck, move_head,
    # calc_segment_limits, stretch_neck, draw_head_and_check_collisions,
    # draw_segment_and_check_collisions). Drained per-segment helpers
    # consumed verbatim from enemy_gleeok_runtime.c.
    ("src/game/enemies/bosses/boss_gleeok.c", "game_enemy_boss_gleeok.o"),
    # Phase 8 Task 8.7 — Gohma callee shims. Resolves c_gohma_animate_and_draw
    # (Z_04.asm:8392) + c_gohma_check_collisions (Z_04.asm:8453) for the
    # drained enrt_update_gohma body in enemy_boss_runtime.c. Composes
    # sprite_anim_* + draw_object_*_with_frame + enrt_gohma_set_sprite_attributes
    # + c_check_monster_collisions — all already linked.
    ("src/game/enemies/bosses/boss_gohma.c", "game_enemy_boss_gohma.o"),
    # Phase 8 Task 8.8 — Patra drain. Carries kPatraSines /
    # kPatraChildStartAngles / kPatraChild1{Cosine,Sine}Bits /
    # kPatraChild2Bits + ShiftMultiply / DecreaseObjectAngle /
    # RotateObjectLocation helpers + enrt_init_patra (Z_04.asm:9552) +
    # enrt_update_patra_child (Z_04.asm:10164 — State 0 staged spawn +
    # State 1 orbit/draw/collision/dead-dummy). All math local to TU.
    ("src/oracle/enemies/enemy_patra_runtime.c", "oracle_enemy_patra.o"),
    # Phase 8 Task 8.8 — Patra bridge. boss_patra_update orchestrator
    # (NES UpdatePatra @ Z_04.asm:10070 + ControlPatraFlight @ 10124).
    # Composes enrt_flyer_speed_up + enrt_flyer_patra_decide_state +
    # c_control_keese_flight (states 2/3 reuse keese-head Chase/Wander) +
    # c_move_flyer + enrt_animate_and_draw_common_object(2) + child-loop
    # + TryChangeManeuver flip. ADOPT stance.
    ("src/game/enemies/bosses/boss_patra.c", "game_enemy_boss_patra.o"),
    # Phase 8 Task 8.9 — Lamnola drain. enrt_init_lamnola (Z_04.asm:9502)
    # + enrt_update_lamnola (Z_04.asm:9699) + enrt_lamnola_update_head +
    # enrt_lamnola_move. Composes c_anim_write_sprite,
    # c_check_monster_collisions, c_reset_shove_info, c_reset_obj_metastate,
    # c_get_opposite_dir, c_bound_by_room, c_get_colliding_tile_moving.
    # ADOPT stance.
    ("src/oracle/enemies/enemy_lamnola_runtime.c", "oracle_enemy_lamnola.o"),
    # PersonText data — 38 cave/UW NPC text blobs + addr table.
    # NES Z_01.asm:50 PersonText. Unblocks UW NPC dispatch ($36/$4B-$52).
    ("src/data/person_text_data.c", "person_text_data.o"),
    # enemy_fix Wave 5 — Wizzrobe family drain. UpdateBlueWizzrobe
    # (Z_04.asm:7034) + UpdateRedWizzrobe (Z_04.asm:7474) + shared
    # primitives. GREENFIELD stance. Wires $23/$24 dispatch rows.
    ("src/oracle/enemies/enemy_wizzrobe_runtime.c", "oracle_enemy_wizzrobe.o"),
    # Phase 8 Task 8.9 — Moldorm drain. enrt_init_moldorm (Z_04.asm:4763)
    # + enrt_update_moldorm (Z_04.asm:4907) + ControlMoldormFlight JT +
    # Moldorm_{Chase,Wander,ChangeFlyingState,PropagateDirs}. Composes
    # already-drained primitives (c_flyer_chase, c_flyer_wander,
    # c_move_flyer, c_check_monster_collisions, c_anim_write_sprite,
    # c_reset_obj_metastate, enrt_check_boss_hit_reaction,
    # enrt_flyer_moldorm_decide_state). ADOPT stance.
    ("src/oracle/enemies/enemy_moldorm_runtime.c", "oracle_enemy_moldorm.o"),
    # Phase 8 Task 8.9 — Lamnola+Moldorm shim bridge. Native shims for
    # c_anim_write_sprite (stub) / c_get_opposite_dir / c_bound_by_room /
    # c_get_colliding_tile_moving / z04_play_boss_death_cry_if_needed /
    # z07_set_shove_info_with0 — first dispatch wiring that exposes these
    # transitive callees. EXTEND stance.
    ("src/game/enemies/enemy_lamnola_bridge.c", "game_enemy_lamnola_bridge.o"),
    # Phase 8 Task 8.10 — Ganon ($3E) drain. enrt_init_ganon (Z_04.asm:9599)
    # + enrt_update_ganon umbrella (Z_04.asm:10321) + ScenePhase0/1/2 +
    # Ganon_Dying + DrawBody + DrawAshes + DrawCloud + DrawBurst +
    # SetUpBurstRays + CheckCollisions +
    # AppendPaletteRowTransferRecord_{Brown,Blue,Triforce}. Composes
    # already-drained enrt_ganon_{randomize_location,activate_room_item,
    # get_cur_cloud_*}, enrt_play_boss_{hit_cry_if_needed,death_cry},
    # enrt_update_candle, c_draw_object_{,not_}mirrored_with_frame,
    # c_anim_write_sprite, c_shoot_fireball, c_reset_obj_metastate,
    # core_reset_{obj_metastate_and_timer,shove_info_and_inv_timer},
    # colrt_check_monster_{sword,arrow_or_rod}_collision,
    # lcrt_check_link_collision_preinit, sprrt_anim_fetch_obj_pos.
    # PARTIAL stance — wizzrobe family motion stubbed (deferred to Phase 8
    # Task 8.11+); slot still ticks + scene phase machine evaluated.
    ("src/oracle/enemies/enemy_ganon_runtime.c", "oracle_enemy_ganon.o"),
    # Phase 8 Task 8.10 — Ganon shim bridge. Resolves GanonStartXs +
    # z01_* + sprrt_/lcrt_/colrt_ undefined refs surfaced by wiring
    # $3E into the enemy_loop dispatch table.
    ("src/game/enemies/enemy_ganon_bridge.c", "game_enemy_ganon_bridge.o"),
    # Phase 9 Task 9.1 — Redux options runtime. GREENFIELD per debate
    # 004; sanctioned new build under src/game/options/. Subsystem is
    # gameplay-mutable (sound on/off, difficulty); UI lands in 9.3.
    ("src/game/options/options_runtime.c", "game_options_runtime.o"),
    ("src/game/options/options_persistence.c", "game_options_persistence.o"),
    ("src/game/options/options_consumer.c", "game_options_consumer.o"),
    ("src/sgdk_adapter/sram_options_io.c", "sram_options_io.o"),
    ("src/game/options/probes/options_probe.c", "game_options_probe.o"),
    ("src/game/options/probes/options_persistence_probe.c", "game_options_persistence_probe.o"),
    ("src/game/options/probes/options_consumer_probe.c", "game_options_consumer_probe.o"),
    # Phase 9 Task 9.5 — HUD format probe. Verifies drained heart-row
    # formatter (hud_format_status_bar_text) against hand-traced expected
    # byte sequences. Locks the contract any native HUD renderer consumes.
    ("src/game/hud/probes/hud_format_probe.c", "game_hud_format_probe.o"),
    # Phase 9 Task 9.7 — save slot serializer + round-trip probe.
    ("src/state/save_serializer.c", "state_save_serializer.o"),
    ("src/state/probes/save_serializer_probe.c", "state_save_serializer_probe.o"),
    ("src/game/enemies/probes/enemy_loop_probe.c", "game_enemy_loop_probe.o"),
    # Phase E (2026-05-24) — Warp routes static dispatch probe. Fills
    # debug RAM @ $FF7800 with cave_entrance_check results for all 128
    # OW rooms so tools/build/probes/probe_warp_routes.lua can byte-diff
    # vs tools/parity/warp_routes_expected.json.
    ("src/game/cave/probes/warp_routes_probe.c", "game_cave_warp_routes_probe.o"),
    # Phase F (2026-05-25) — Dungeon round-trip synthetic verifier.
    # Fills debug RAM @ $FF7DB0 with 18 (level, quest) outcomes after
    # driving detect_warp_uw_to_ow with a pre-latched canonical OW
    # source. tools/build/probes/probe_dungeon_roundtrip.lua reads +
    # tools/parity/diff_dungeon_roundtrip.py byte-diffs vs oracle.
    ("src/game/world/probes/dungeon_roundtrip_probe.c", "game_world_dungeon_roundtrip_probe.o"),
    # Phase J.2 (2026-05-18): legacy expanded_bg_chr.c retired. All
    # consumers (ow/uw/hud renderer + redux UW upload) migrated to
    # bg_sparse_chr + universal LUT. Saves ~120 KB ROM.
    ("engine/src/bg_sparse_chr.c", "bg_sparse_chr.o"),
    ("engine/src/atlas/items_chr_x4.c", "atlas_items_chr_x4.o"),
    # PR-4a: scene-bank scaffolding + DMA state machine.
    ("engine/src/atlas/roomrom_scene_vram_contracts.c", "atlas_scene_vram_contracts.o"),
    ("engine/src/atlas/level_chr_swap.c", "atlas_level_chr_swap.o"),
    # PR-4b: UWSP enemy CHR banks (3 banks, 4x sub-pal expanded).
    ("engine/src/atlas/enemy_chr.c", "atlas_enemy_chr.o"),
    # PR-5: UWSP boss CHR banks (3 banks, 1x sub-pal, SCENE_OBJ-shared).
    ("engine/src/atlas/boss_chr.c", "atlas_boss_chr.o"),
    ("data/rooms/overworld.c", "overworld.o"),
    ("data/chr/overworld_bg.c", "overworld_bg.o"),
    ("data/rooms/dungeons.c", "dungeons.o"),
    ("data/chr/underworld_bg.c", "underworld_bg.o"),
    ("engine/src/redux_overworld.c", "redux_overworld.o"),
    ("engine/src/redux_overworld_bg.c", "redux_overworld_bg.o"),
    ("engine/src/redux_uw_bg.c", "redux_uw_bg.o"),
    ("engine/src/redux_hud_chr.c", "redux_hud_chr.o"),
    ("data/chr/common.c", "common.o"),
    ("data/chr/sprites.c", "sprites.o"),
    # T-011: NES sprite tiles $70.. (demo block; Link item-lift pose $78/$79).
    ("data/chr/demo.c", "demo_chr.o"),
    ("data/misc/palettes.c", "palettes.o"),
    # T-189: existing ROM-derived piece offsets/replacements/empty tiles.
    ("data/misc/ui_layout.c", "ui_layout.o"),
    # T-098: existing ROM-extracted ending tables, no new game-data input.
    ("data/text/nes_frontend_text.c", "nes_frontend_text.o"),
    ("data/text/nes_frontend_credits.c", "nes_frontend_credits.o"),
]


def run(args: list[str | Path], *, cwd: Path = ROOT) -> None:
    subprocess.run([str(arg) for arg in args], cwd=cwd, check=True)


def gcc_prefix() -> list[str | Path]:
    return exe(GCC) + ["-B", f"{TOOLBIN}\\"]


def include_args() -> list[str]:
    args: list[str] = []
    for inc in INCS:
        args.append(f"-I{inc}")
    return args


def depfile_inputs(dep: Path) -> list[Path] | None:
    """Inputs listed in a gcc -MMD depfile (target first, spaces escaped '\\ ')."""
    try:
        text = dep.read_text(encoding="utf-8", errors="replace").replace("\\\n", " ")
    except OSError:
        return None
    toks = [t.replace("\\ ", " ") for t in re.findall(r"(?:\\ |\S)+", text)]
    for i, t in enumerate(toks):
        if t.endswith(":"):
            return [Path(re.sub(r"(?i)^z:(?=/)", "", x) if WINE else x)
                    for x in toks[i + 1:]]
    return None


def up_to_date(obj: Path, cmd: list[str]) -> bool:
    """T-141 incremental build: reuse obj when its command line is unchanged and
    no input in its depfile (source + every included header) is newer."""
    stamp, dep = obj.with_suffix(".cmd"), obj.with_suffix(".d")
    if not (obj.exists() and stamp.exists() and dep.exists()):
        return False
    if stamp.read_text(encoding="utf-8") != "\n".join(cmd):
        return False
    inputs = depfile_inputs(dep)
    if not inputs:
        return False
    t = obj.stat().st_mtime
    try:
        return all(p.stat().st_mtime <= t for p in inputs)
    except OSError:
        return False


def compile_c(src: str, obj_name: str) -> Path:
    src_path = ROOT / src
    obj_path = OUT / obj_name
    cmd = [str(a) for a in gcc_prefix() + CFLAGS + LTO_CFLAGS + include_args()
           + ["-c", src_path, "-o", obj_path]]
    if up_to_date(obj_path, cmd):
        return obj_path
    print(f"[3] Compiling {src}...")
    obj_path.with_suffix(".cmd").unlink(missing_ok=True)
    run(cmd + ["-MMD", "-MF", obj_path.with_suffix(".d")], cwd=PROJ)
    obj_path.with_suffix(".cmd").write_text("\n".join(cmd), encoding="utf-8")
    return obj_path


def compile_asm(src: Path, obj_name: str, label: str, mri: bool = False) -> Path:
    obj_path = OUT / obj_name
    print(f"[3] Compiling {label}...")
    asm_flags = "-Wa,--register-prefix-optional,--bitwise-or"
    # Phase 12.2 audio link: MRI mode required for vasm-dialect audio_driver.asm.
    # gas `incbin` resolves relative to cwd, so MRI compiles run from ROOT
    # (project root) instead of PROJ (build dir) so `incbin "src/data/..."`
    # finds the music_blob.dat blob.
    cwd = ROOT if mri else PROJ
    if mri:
        asm_flags += ",--mri"
    run(
        gcc_prefix()
        + ["-x", "assembler-with-cpp", asm_flags]
        + CFLAGS
        + include_args()
        + ["-c", src, "-o", obj_path, "-MMD", "-MF", obj_path.with_suffix(".d")],
        cwd=cwd,
    )
    return obj_path


def main() -> int:
    if not GCC.exists():
        print(f"ERROR: gcc not found at {GCC}")
        return 1
    if WINE:
        import shutil
        if not shutil.which("wine"):
            print("ERROR: wine not found (non-Windows hosts run the SGDK toolchain under Wine)")
            return 1

    PROJ.mkdir(parents=True, exist_ok=True)
    OUT.mkdir(parents=True, exist_ok=True)
    ROM_OUT.parent.mkdir(parents=True, exist_ok=True)

    print("[Build] check_sgdk_pin.py")
    run([sys.executable, ROOT / "tools" / "check_sgdk_pin.py"])

    # PR-1 CHR-FOUNDATION gates (per docs/superpowers/specs/2026-05-07-
    # whole-chr-rollout-design.md). Zelda.md is sole target; mirror the
    # strict gates the engine dev harness used to run.
    print("[Build][CHR-1 gate] verify_item_chr_manifest.py --strict")
    run([sys.executable, ROOT / "engine" / "tools" / "verify_item_chr_manifest.py", "--strict"])

    # [CHR-1 gate] verify_vram_budget.py — re-enabled PR-2c 2026-05-08.
    # PR-2b switched the runtime to 64x32 plane mode (render_mode_set_h64v32
    # + BGA/BGB address overrides), raising the tile-data ceiling from
    # $A800 (1344 tiles) to $C000 (1536 tiles). ITEM bank end tile 1460
    # now fits with 75-tile headroom.
    print("[Build][CHR-1 gate] verify_vram_budget.py")
    run([sys.executable, ROOT / "engine" / "tools" / "verify_vram_budget.py"])

    print("[Build][CHR-1 gate] check_generated_freshness.py")
    run([sys.executable, ROOT / "tools" / "probes" / "check_generated_freshness.py"])

    print("[1] Compiling sgdk/src/boot/rom_head.c...")
    run(gcc_prefix() + CFLAGS + include_args() + ["-c", SGDK / "src" / "boot" / "rom_head.c", "-o", OUT / "rom_head.o"], cwd=PROJ)

    print("[1] objcopy rom_head.o -> rom_head.bin...")
    run(exe(OBJCOPY) + ["-O", "binary", OUT / "rom_head.o", OUT / "rom_head.bin"], cwd=PROJ)

    print("[2] Compiling sgdk/src/boot/sega.s...")
    run(
        gcc_prefix()
        + ["-x", "assembler-with-cpp", "-Wa,--register-prefix-optional,--bitwise-or"]
        + CFLAGS
        + include_args()
        + ["-c", SGDK / "src" / "boot" / "sega.s", "-o", OUT / "sega.o"],
        cwd=PROJ,
    )

    objects = [
        compile_asm(ROOT / "src" / "platform" / "game_startup.s", "game_startup.o", "src/platform/game_startup.s"),
        compile_c("src/platform/game_main.c", "game_main.o"),
        # Phase 12.2 audio link: audio_driver.asm vasm -> gas MRI mode
        # translation per docs/audit/audio_link_engineering_plan.md.
        # Provides music_play / music_tick / change_song / tick_sq1 etc.
        compile_asm(ROOT / "src" / "audio_driver.asm", "audio_driver.o", "src/audio_driver.asm", mri=True),
    ]

    # T-141: C units compile in parallel; unchanged ones are reused
    # (compile_c / up_to_date). Link order stays the source-list order.
    c_units = list(TITLE_C_SOURCES) + list(ROOMROM_C_SOURCES)
    if NES_MUSIC:
        if not (ROOT / "build/nes_music/nes_music.c").is_file():
            print("ERROR: ROM music table missing; run build.py with your NES ROM")
            return 1
        c_units.append(("build/nes_music/nes_music.c", "nes_music.o"))
    if LOCAL_MUSIC:
        if not (ROOT / "build" / "local_music" / "local_music.c").is_file():
            print("ERROR: ZELDA_LOCAL_MUSIC=1 but build/local_music/local_music.c is missing "
                  "(run tools/converter/local_music.py <folder>)")
            return 1
        c_units.append(("build/local_music/local_music.c", "local_music.o"))
    with ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as ex:
        objects.extend(ex.map(lambda so: compile_c(*so), c_units))

    print("[4] Linking...")
    # Inputs go in a response file, relative to PROJ: with absolute paths
    # the command line grows with the install path and passes the Windows
    # 32767-character limit when the package is extracted somewhere deep.
    link_inputs = [OUT / "sega.o", *objects, LIB / "libmd.a", LIB / "libgcc.a"]
    rsp = OUT / "link.rsp"
    rsp.write_text("".join(f'"{Path(os.path.relpath(p, PROJ)).as_posix()}"\n'
                           for p in link_inputs), encoding="utf-8", newline="\n")
    run(
        gcc_prefix()
        + [
            "-m68000",
            "-n",
            "-T",
            SGDK / "md.ld",
            "-nostdlib",
            f"@{rsp.relative_to(PROJ).as_posix()}",
            "-o",
            OUT / "Zelda.out",
            "-Wl,--gc-sections",
        ]
        + LTO_LINK_FLAGS,
        cwd=PROJ,
    )

    nm = subprocess.run([str(a) for a in exe(NM) + [OUT / "Zelda.out"]], cwd=PROJ,
                        capture_output=True, text=True, check=True).stdout
    bend = next(int(l.split()[0], 16) for l in nm.splitlines()
                if l.split()[-1:] == ["_bend"])
    if (bend & 0xFFFFFF) > NES_MIRROR_RESERVED:
        print(f"ERROR: .bss ends at ${bend & 0xFFFFFF:06X}, past ${NES_MIRROR_RESERVED:06X}: "
              "it would overlap the NES RAM mirror at $FF8000")
        return 1
    print(f"[4] .bss ends at ${bend & 0xFFFFFF:06X} "
          f"({NES_MIRROR_RESERVED - (bend & 0xFFFFFF)} bytes below the NES RAM mirror)")

    print("[5] objcopy ELF -> flat binary...")
    run(exe(OBJCOPY) + ["-O", "binary", OUT / "Zelda.out", ROM_RAW], cwd=PROJ)

    print("[5] fix_checksum...")
    run([sys.executable, ROOT / "tools" / "fix_checksum.py", ROM_RAW, ROM_OUT])
    if ROM_RAW.exists():
        ROM_RAW.unlink()

    print()
    print(f"ROM built: {ROM_OUT}")
    # User 2026-10-09: keep the latest playable test copy on hand after
    # every successful build. Zelda.md remains the sole build target.
    latest = ROOT / "builds" / "playtests" / "Zelda-Latest.md"
    latest.parent.mkdir(parents=True, exist_ok=True)
    latest.write_bytes(ROM_OUT.read_bytes())
    print(f"Latest test ROM: {latest}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
