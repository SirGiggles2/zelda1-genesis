#include <genesis.h>
#ifdef RAM
#undef RAM
#endif
#include "platform_abi.h"
#include "audio_abi.h"
#include "intro_phase.h"
#include "render_abi.h"
#include "engine_runtime.h"
#include "engine_state.h"   /* roomrom_main_set_quest */

#define A4_EXPECTED 0x00FF8000UL
#define PROBE_BASE ((volatile u8 *) 0x00FF7000UL)
#define ABS_RAM ((volatile u8 *) 0x00FF8000UL)
#define VDP_CTRL_WORD (*(volatile u16 *) 0x00C00004UL)
#define PASS_FRAME_LIMIT 180U
#include "../game/items/debug_unlock_all.h" /* gameplay session flag; no title shortcut */
#include "../game/debug/state_dump.h"   /* A+B+C+Start freeze + dump */
#include "../game/audio/audio_dispatch.h"

/* File Select. Declared here rather than including fs_main.h /
 * fs_handoff.h: those headers pull in the proof-ROM stdint typedefs,
 * which collide with SGDK's types.h (conflicting types for u32/s32/s8)
 * in this TU. The three symbols are stable and plain. */
extern void fs_enter(void);
extern void fs_tick(void);
extern unsigned char g_fs_handoff_requested;
extern unsigned char g_fs_handoff_slot;

/* Persistent NES-format saves (T-100). src/state is on the include path. */
#include "save_game.h"
#include "../game/world/bg_palette.h"   /* refresh_link_color */
#include "../game/world/startup_triangle.h"
extern void room_patch_level_palette_link_color(void);   /* room_dispatch.c */

/* The room (and its palette) loads in roomrom_debug_enter, before the
 * save or the debug unlock sets InvRing. NES InitMode3_Sub1 patches
 * Link's color into the level palette after the File Select; do the same
 * once the profile is in RAM (t121_ring1). */
static void link_color_after_profile(void)
{
    room_patch_level_palette_link_color();
    roomrom_bg_palette_refresh_link_color();
}

/* Debug/probe sentinel block (platform_abi.h DBG_SENTINEL, T-109). */
volatile unsigned char g_debug_sentinel[32];

typedef enum {
    COMBINED_STATE_TITLE = 0,
    COMBINED_STATE_ROOMROM = 1,
    COMBINED_STATE_FS = 2
} combined_state_t;

extern u32 debug_get_a4(void);

static combined_state_t s_state;
static u16 s_frame;
static u16 s_fail_stage;
static u16 s_prev_joy;
static u32 s_last_a4;

static void probe_write_u16(u16 offset, u16 value)
{
    PROBE_BASE[offset + 0U] = (u8) (value >> 8);
    PROBE_BASE[offset + 1U] = (u8) value;
}

static void probe_write_u32(u16 offset, u32 value)
{
    PROBE_BASE[offset + 0U] = (u8) (value >> 24);
    PROBE_BASE[offset + 1U] = (u8) (value >> 16);
    PROBE_BASE[offset + 2U] = (u8) (value >> 8);
    PROBE_BASE[offset + 3U] = (u8) value;
}

static void probe_publish(void)
{
    PROBE_BASE[0] = 0xA4U;
    PROBE_BASE[1] = 0x4AU;
    probe_write_u16(2U, s_frame);
    probe_write_u16(4U, s_fail_stage);
    probe_write_u32(6U, s_last_a4);
    PROBE_BASE[10] = ABS_RAM[0x0012U];
    PROBE_BASE[11] = ABS_RAM[0x0013U];
    PROBE_BASE[12] = s_fail_stage ? 0xEEU : ((s_frame >= PASS_FRAME_LIMIT) ? 1U : 0U);
    PROBE_BASE[13] = (u8) s_state;
    PROBE_BASE[14] = roomrom_debug_get_scene();
    PROBE_BASE[15] = roomrom_debug_get_room_id();
    probe_write_u16(16U, (u16)roomrom_debug_get_link_x());
    probe_write_u16(18U, (u16)roomrom_debug_get_link_y());
}

static void probe_fail(u16 stage)
{
    if (!s_fail_stage)
    {
        s_fail_stage = stage;
    }
}

static void probe_check(u16 stage)
{
    s_last_a4 = debug_get_a4();
    if (s_last_a4 != A4_EXPECTED)
    {
        probe_fail(stage);
        probe_publish();
        return;
    }

    RAM(0x0012) = 0xCDU;
    RAM(0x0013) = (u8) stage;
    if ((ABS_RAM[0x0012U] != 0xCDU) || (ABS_RAM[0x0013U] != (u8) stage))
    {
        probe_fail((u16) (stage | 0x8000U));
    }

    probe_publish();
}

/* Front-end video layout (title and File Select). */
static void frontend_video_layout(void)
{
    /* Match the native title boot layout from genesis_shell.asm. */
    VDP_CTRL_WORD = 0x8134u; /* display off, VBlank IRQ, DMA, M5 */
    VDP_CTRL_WORD = 0x8230u; /* Plane A @ $C000 */
    VDP_CTRL_WORD = 0x832Cu; /* Window @ $B000 */
    VDP_CTRL_WORD = 0x8407u; /* Plane B @ $E000 */
    VDP_CTRL_WORD = 0x857Cu; /* SAT @ $F800 */
    VDP_CTRL_WORD = 0x8B00u; /* full-screen scroll */
    VDP_CTRL_WORD = 0x8C00u; /* H32, no interlace, no shadow/highlight */
    VDP_CTRL_WORD = 0x8D3Fu; /* H-scroll @ $FC00 */
    VDP_CTRL_WORD = 0x8F02u; /* word auto-increment */
    render_mode_set_v32();
    render_window_v_set(0u);
}

static void debug_enter_title(void)
{
    s_state = COMBINED_STATE_TITLE;
    s_prev_joy = 0u;
    frontend_video_layout();

    DBG_SENTINEL(0x1Fu) = 0xA1u;
    DBG_SENTINEL(0x11u) = 0u;
    DBG_SENTINEL(0x12u) = 0u;
    intro_phase_init();
    intro_phase_step();
}

static void debug_poll_title(void)
{
    u16 joy;

    probe_check(2U);
    SYS_doVBlankProcess();
    ++s_frame;
    state_dump_poll(STATE_DUMP_CTX_TITLE);   /* A+B+C+Start: freeze + dump */
    probe_check(3U);
    DBG_SENTINEL(0x11u) = (u8) s_frame;
    intro_phase_step();

    joy = JOY_readJoypad(JOY_1);
    /* Start enters File Select. ABC+Start is handled by state_dump_poll
     * above; no title shortcut bypasses the normal new/save-game path. */
    {
        unsigned char start_now  = (joy & BUTTON_START) ? 1u : 0u;
        unsigned char start_prev = (s_prev_joy & BUTTON_START) ? 1u : 0u;
        if (start_now && !start_prev)
        {
            s_prev_joy = joy;
            s_state = COMBINED_STATE_FS;
            /* NES title Start runs UpdateMode0Demo_Sub1/Sub2: validate each
             * save file A and fill the slot info File Select shows. */
            save_game_boot();
            fs_enter();
            return;
        }
    }

    s_prev_joy = joy;

}

extern void audio_vblank_hook_install(void);
extern void music_play(unsigned char song_bitmap);

/* 2026-05-15 perf fix: per-frame probe_check(4U) was unconditional,
 * eating ~7-12% of frame budget on accessor calls + 20 byte writes.
 * Gate behind enemy_loop_probe_is_armed() so default gameplay skips
 * the heartbeat. Probes that need it write the arm magic first. */
extern unsigned char enemy_loop_probe_is_armed(void);

/* T-168 / T-171: SGDK's heap runs from the end of .bss to MEMORY_HIGH
 * ($FFF600), straight through the NES RAM mirror at $FF8000 (A4 base).
 * At boot, SGDK internal_reset -> VDP_init -> VDP_resetScreen ->
 * VDP_loadFont unpacks the font into a heap buffer and writes 2 bytes past
 * its end (write watch, run_lockstep --write-watch FF7FFE: PC $770
 * .loop_do_copy, buffer $FF73FC, frame 16), over the next block header.
 * Where that lands moves with the size of .bss: the old filler/wall
 * allocation then missed $FF8000 and the free space ran into NES RAM
 * (T-171: 257 more bytes of .bss turned ObjX+2 to $FF in t129 at t122).
 * At main the heap holds only SGDK's persistent blocks, the DMA queue and
 * DMA buffer, both allocated from the heap start: with .bss ending at
 * $FF7C5A the 80-entry queue ($500 bytes) ran to $FF815C over NES RAM
 * $000-$15B and the buffer started at $FF815E (T-080). Rebuild the heap
 * from scratch around the mirror:
 *   [heap start, $FF7FFE)   free
 *   [$FF7FFE, $FF8800)      one used block that is never freed (NES RAM)
 *   [$FF8800, MEMORY_HIGH)  free, end-of-heap marker at MEMORY_HIGH-2
 * then let DMA_initEx free and reallocate the queue and buffer with the
 * sizes SGDK set up; both now land above the mirror. No allocation can
 * reach NES RAM. (DMA_initEx's MEM_free of the old buffer clears bit 0 of
 * its old header word, NES $15C; the memset below runs after it.) */
extern u32 _bend;
#define NES_MIRROR_LO 0xFF8000ul
#define NES_MIRROR_HI 0xFF8800ul
static void heap_wall_nes_mirror(void)
{
    const u32 heap = (((u32)&_bend) + 1ul) & ~1ul & 0xFFFFFFul;  /* MEM_init's heap */
    const u32 top = ((u32)MEMORY_HIGH & 0xFFFFFFul) - 2ul;       /* end marker */
    const u32 reserved = NES_MIRROR_LO - 2ul;                    /* its header */
    const u16 queue_size = DMA_getMaxQueueSize();
    const u16 buffer_size = DMA_getBufferSize();
    const u16 capacity = DMA_getMaxTransferSize();
    unsigned char ok = 0u;
    if (heap <= reserved) {
        if (heap < reserved)
            *(volatile u16 *)heap = (u16)(reserved - heap);
        *(volatile u16 *)reserved = (u16)((NES_MIRROR_HI - reserved) | 1u);
        *(volatile u16 *)NES_MIRROR_HI = (u16)(top - NES_MIRROR_HI);
        *(volatile u16 *)top = 0u;
        MEM_pack();
        DMA_initEx(queue_size, capacity, buffer_size);
        ok = (((u32)dmaQueues & 0xFFFFFFul) >= NES_MIRROR_HI
              || ((u32)dmaQueues & 0xFFFFFFul) + (u32)queue_size * sizeof(DMAOpInfo) <= reserved)
             && (dmaDataBuffer == NULL
                 || ((u32)dmaDataBuffer & 0xFFFFFFul) >= NES_MIRROR_HI
                 || ((u32)dmaDataBuffer & 0xFFFFFFul) + buffer_size <= reserved);
    }
    /* The font buffer sat wherever .bss ends and could spill into NES RAM
     * before main (the pad test: [$FF74FC, $FF80FE) left font bytes in
     * ObjX). _start_entry cleared all RAM before it; clear the 2 KB NES
     * RAM again, as it was at power-on. */
    memset((void *)NES_MIRROR_LO, 0, 0x800u);
    /* Probe-visible result (masked stack-page cell NES $01F8): 1 = the
     * heap is walled around NES RAM and the DMA queue/buffer are outside. */
    *(volatile unsigned char *)0xFF81F8ul = ok;
}

int debug_main_after_a4(bool hardReset)
{
    (void) hardReset;

    heap_wall_nes_mirror();

    /* T-125: pad 2 only ever needs the NES buttons (NES controller 2 has
     * A/B/Start/Select/D-pad); the 6-button read runs every VBlank. */
    JOY_setSupport(PORT_2, JOY_SUPPORT_3BTN);

    /* Phase 10.3 audio link, VBlank tick slice: register music_tick
     * as VBlank callback so the audio driver advances notes once per
     * frame. Must run before any music_play() request. */
    audio_vblank_hook_install();

    /* Phase 10.3 audio link, XGM SFX path: load XGM Z80 driver and
     * register the 7 NES DMC samples (IDs 64..70) for SFX playback.
     * audio_driver.asm@dmc_trigger calls audio_sfx_play -> XGM
     * sample channels. FM/PSG music continues on M68K via music_tick
     * (XGM Z80 driver only owns sample channels by default). */
    audio_xgm_init();

    probe_check(1U);
    debug_enter_title();

    /* Phase 10.3 audio link, per-event music_play slice (title song).
     * NES Z1 title song = bit 7 set on SongRequest ($80). Memory
     * project_midi_substrate_works confirms the driver plays when
     * poked with $80. With the VBlank hook installed above, the driver
     * advances notes each frame. */
    music_play(0x80);

    while (TRUE)
    {
        if (s_state == COMBINED_STATE_TITLE)
        {
            debug_poll_title();
        }
        else if (s_state == COMBINED_STATE_FS)
        {
            SYS_doVBlankProcess();
            ++s_frame;
            state_dump_poll(STATE_DUMP_CTX_FS);
            fs_tick();

            /* The File Select sets this when the player commits to a
             * slot; CurSaveSlot ($0016) is already seeded by then. This
             * is the normal New Game / Continue entry. The player
             * starts with whatever the chosen slot actually holds. */
            if (g_fs_handoff_requested)
            {
                g_fs_handoff_requested = 0u;

                /* fs_handoff_to_transpiled tears the screen down for the
                 * jump it used to make: display off, planes cleared, mode
                 * forced to V64. The gameplay runtime expects the V32
                 * layout debug_enter_title establishes, and nothing in
                 * roomrom_debug_enter re-enables display — so without
                 * this the handoff produced a black screen with no room
                 * loaded. Restore the same video state the title path
                 * hands to gameplay. */
                render_mode_set_v32();
                render_window_v_set(0u);
                render_display_enable(0);
                audio_music_play(0u);  /* NES menu-to-load silence */

                /* NES QuestNumbers: 0 = first quest, 1 = second. Needed
                 * before entry because level data installs per quest. */
                {
                    unsigned char slot = g_fs_handoff_slot;
                    unsigned char q2 = (save_game_slot_active(slot) &&
                                        save_game_slot_quest(slot)) ? 1u : 0u;
                    roomrom_main_set_quest(q2 ? 2u : 1u);
                }
                g_debug_session = 0u;   /* T-090: real game, no debug */
                s_state = COMBINED_STATE_ROOMROM;
                probe_publish();
                roomrom_debug_enter();

                /* Continue vs New Game. roomrom_debug_enter seeds the
                 * default profile, so the restore has to run AFTER it or
                 * the defaults would overwrite the save.
                 *
                 * The slot info was validated at title Start (NES file A
                 * markers + checksum); an inactive slot is a New Game.
                 * CurSaveSlot is set either way so a later save lands in
                 * the chosen slot. */
                RAM(0x0016u) = g_fs_handoff_slot;   /* CurSaveSlot */
                (void) save_game_load_slot(g_fs_handoff_slot);
                link_color_after_profile();
                startup_triangle_arm(save_game_slot_mode(g_fs_handoff_slot) == SAVE_MODE_MD_REMIX);

                /* T-241: restore the selected profile before the existing
                 * InitMode3/Unfurl path. NES Z_05 InitMode3_Sub8 loads
                 * StartRoomId; Z_07 UpdateMode3Unfurl starts the level song
                 * after the center-out reveal. Mode 3 keeps input locked. */
                RAM(0x0012u) = 0x03u;
                RAM(0x0013u) = 0u;
                RAM(0x0011u) = 0u;
                RAM(0x0028u) = 0u;
            }
        }
        else
        {
            roomrom_debug_tick();
            ++s_frame;
            /* T-149: NES UpdateModeDSave_Sub2 sets GameMode 0 submode 1:
             * UpdateMode0Demo_Sub1 validates the save files, then the menu
             * runs. Genesis: the same validation, then its File Select. */
            if (RAM(0x0012u) == 0x00u && RAM(0x0013u) == 0x01u)
            {
                s_state = COMBINED_STATE_FS;
                s_prev_joy = 0u;
                render_cram_defer(0u);   /* the File Select writes CRAM itself */
                frontend_video_layout();
                render_display_enable(1);
                save_game_boot();
                fs_enter();
                music_play(0x80);   /* FS shares the title song */
                continue;
            }
            if (enemy_loop_probe_is_armed())
            {
                probe_check(4U);
            }
        }
    }

    return 0;
}
