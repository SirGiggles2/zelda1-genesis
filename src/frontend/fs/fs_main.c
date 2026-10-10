/* src/fs_main.c — entry from boot.asm (proof ROM) or intro_handoff (main ROM v6).
 * v1: render static layout + Link sprites once at fs_init, hand off to phase loop.
 * v2: phase machine + input dispatch + cursor nav (FS_LOAD → FS_NAV).
 */
#include "fs_main.h"
#include "fs_render.h"
#include "fs_phase.h"
#include "fs_input.h"
#include "fs_options.h"
#include "render_abi.h"
#include "audio_abi.h"
#include "save_game.h"   /* T-099 register / erase / copy */
#include "../../game/options/options_persistence.h"

/* Music driver hooks — proof ROM links music_stub.c (no-op);
 * main ROM links the real audio driver. */
extern void music_play(unsigned char bit);

/* SONG_FS_BIT = 0 — original NES FS is silent; intro_to_file_select_trampoline
 * (genesis_shell.asm:681) clears SongRequest before Mode 1. Call kept so call
 * site exists if the design ever changes. */
#define SONG_FS_BIT  0x00

/* Generated assets — defined in src/gen/. */
extern const uint8_t  fs_bg_chr_full[];        /* 242 tiles × 32 bytes = 7744 bytes */
extern const uint8_t  fs_link_sprite_chr[];    /* 4 tiles × 32 bytes = 128 bytes */
extern const uint8_t  fs_heart_cursor_chr[];   /* 1 tile  × 32 bytes =  32 bytes */
extern const uint16_t fs_palettes[4][4];       /* 4 Genesis CRAM palettes × 4 colors */
extern const unsigned char common_chr[];       /* data/chr/common.c (ROM-extracted) */


/* fs_init: upload CHR data and all palettes once at boot.
 *
 * CRAM layout (64 words = 4 palettes × 16 colors on Genesis):
 *   Palette 0 (CRAM byte 0)  : NES BG pal 0 (attr=0 cells: black bg, white text)
 *   Palette 1 (CRAM byte 32) : NES BG pal 1 (attr=1 cells: LIFE/heart icon area)
 *   Palette 2 (CRAM byte 64) : NES sprite pal 0 / Redux green-Link override (Link sprites)
 *   Palette 3 (CRAM byte 96) : NES sprite pal 3 (heart cursor sprite)
 *
 * v1.fix2: Link slot tinting (blue/red) deferred — Gen 4-pal budget spent on
 * BG 0/1 + Link + cursor. Multi-pal BG (LIFE column) is the visual gate.
 *
 * VRAM layout:
 *   Tile 0x00..0xF1  BG CHR full block (242 tiles)
 *   Tile 0x100..0x103  Link sprite CHR
 *   Tile 0x104        Heart cursor CHR
 */
static void fs_init(void) {
    /* T-204: File Select owns a stationary viewport. Title/story callers
     * may leave either plane scrolled; keep uploads and the initial draw
     * hidden until this scene is complete. */
    render_display_enable(0);
    render_scene_scroll_set(0, 0);

    /* Audio handoff: SongRequest=$08 (LA "Get Item") is fired from
     * intro_start_pressed BEFORE the fade. Jingle is single-phrase and
     * self-silences via song_ended -> music_silence after sq1 hits $00,
     * so FS ends up silent without any explicit silence write here.
     * Calling render_wait_vblank inside fs_init while display is off and
     * the plane size is mid-transition (V64 -> V32) crashed Zelda37.560,
     * so do NOT poll from this function. */

    /* 0. Force plane size H32xV32. Proof ROM boot.asm sets this directly, but
     *    main ROM intro_handoff sets V64 before calling fs_main; our nametable
     *    writes assume V32 stride (32 cells × 2 bytes = 64-byte rows). */
    render_mode_set_v32();

    /* 0a. The File Select is drawn at scroll 0. Every way in must get that:
     *     Start during the story scroll entered with the story's vscroll
     *     still set (screen shifted up, user report 2026-10-09). */
    render_vscroll_reset();

    /* 0b. Start from a known sprite table and CRAM whatever ran before
     *     (title, story, gameplay): the File Select draws only its own
     *     sprites and loads 4 colours per palette. SAT = VRAM $F800 in this
     *     layout (fs_render.c SAT_VRAM, reg 5 = $7C); 80 slots x 4 words. */
    render_plane_fill(0xF800u, 0u, 320u);
    {
        static const unsigned short black[64] = { 0u };
        render_cram_open_write_byte(0u);
        render_vram_write_words(black, 64u);
    }

    /* 1. Upload full BG CHR block to VRAM tile 0x00 (242 tiles × 32 bytes = 7744 bytes). */
    render_chr_upload((unsigned short)(0x00u * 32u), fs_bg_chr_full,
                      (unsigned short)(242u * 32u));

    /* 1a. Zero VRAM tile 0 — both Plane A and Plane B nametables default to cells
     *     that reference tile 0; if tile 0 holds NES font glyph "0" (which it does
     *     in fs_bg_chr_full), the screen background fills with "0" digits. Forcing
     *     tile 0 to all-transparent makes cleared cells render as the BG color. */
    render_vram_write_zero_tile(0x0000u);

    /* 1b. T-099: NES CommonMiscPatterns = BG tiles $F2..$FF (full heart $F2,
     *     etc.), which the captured FS block stops short of. common_chr tiles
     *     224..237 are those 14 tiles, extracted from the supplied ROM
     *     (verified byte-equal to engine/out/prg_blocks CommonMiscPatterns). */
    render_chr_upload((unsigned short)(0xF2u * 32u), common_chr + 224u * 32u,
                      (unsigned short)(14u * 32u));

    /* Redux uses the outlined $F2 heart from the captured FS pattern table. */
    render_chr_upload(0xF2u*32u,fs_heart_cursor_chr,32u);

    /* 1c. T-099: NES font glyphs the captured (Redux) FS block lacks or
     *     moved, for names typed on the NES character board: '0' (tile 0 is
     *     blanked above), $62 '-', $63, $2B. Source: ROM CommonBackground-
     *     Patterns = common_chr tiles 112..223 (pixel-matched 2026-09-24:
     *     only these codes differ from fs_bg_chr_full). fs_render remaps. */
    render_chr_upload((unsigned short)(0x105u * 32u), common_chr + (112u + 0x00u) * 32u, 32u);
    render_chr_upload((unsigned short)(0x106u * 32u), common_chr + (112u + 0x62u) * 32u, 32u);
    render_chr_upload((unsigned short)(0x107u * 32u), common_chr + (112u + 0x63u) * 32u, 32u);
    render_chr_upload((unsigned short)(0x108u * 32u), common_chr + (112u + 0x2Bu) * 32u, 32u);

    /* T-274: private pixel ranges in shared CRAM palettes. The forward
     * walk mirrors the same pose (Z_07.asm:SetUpWalkingSprites). */
    {
        unsigned char tiles[128];
        for (unsigned short i=0u;i<128u;i++) {
            unsigned char b=fs_link_sprite_chr[i], hi=b>>4, lo=b&15u;
            tiles[i]=(unsigned char)(((hi ? hi+3u : 0u)<<4)|(lo ? lo+3u : 0u));
        }
        render_chr_upload(0x100u*32u,tiles,128u);
        for (unsigned short i=0u;i<32u;i++) {
            unsigned char b=fs_heart_cursor_chr[i], hi=b>>4, lo=b&15u;
            tiles[i]=(unsigned char)(((hi ? hi+6u : 0u)<<4)|(lo ? lo+6u : 0u));
        }
        render_chr_upload(0x104u*32u,tiles,32u);
        /* Redux's flashing block is BG tile $25, shown over the glyph. */
        for (unsigned short i=0u;i<32u;i++) {
            unsigned char b=fs_bg_chr_full[0x25u*32u+i], hi=b>>4, lo=b&15u;
            tiles[i]=(unsigned char)(((hi ? hi+6u : 0u)<<4)|(lo ? lo+6u : 0u));
        }
        render_chr_upload(0x10Du*32u,tiles,32u);
    }

    /* 4. Load all 4 CRAM palettes (4 colors each at CRAM byte offsets 0/32/64/96).
     *    render_cram_open_write_byte takes raw byte address; caller streams words
     *    after with render_vram_write_words (reuses the open data port). */
    render_cram_open_write_byte( 0u);
    render_vram_write_words(&fs_palettes[0][0], 4u);  /* pal 0: BG attr=0 */
    render_cram_open_write_byte(32u);
    render_vram_write_words(&fs_palettes[1][0], 4u);  /* pal 1: BG attr=1 (LIFE) */
    render_cram_open_write_byte(38u);
    render_vram_write_word(0x0EEEu); /* Live Redux PALRAM BG1 colour 3 = $30. */
    render_cram_open_write_byte(64u);
    render_vram_write_words(&fs_palettes[2][0], 4u);  /* pal 2: Link sprite */
    render_cram_open_write_byte(96u);
    render_vram_write_words(&fs_palettes[3][0], 4u);  /* pal 3: heart cursor */

    {
        /* NES cursor $15/$27/$30 converted by extract_intro_assets.py. */
        static const unsigned short cursor[3]={0x060Cu,0x008Eu,0x0EEEu};
        render_cram_open_write_byte(32u+14u);
        render_vram_write_words(cursor,3u);
    }

    /* 4a. The OPTIONS submenu shows and edits the saved options: load them
     *     (or defaults) here. Before, they were first loaded at gameplay
     *     start, so the menu showed an all-zero state (START HP blank). */
    options_persistence_load_or_default();

    /* 5. Phase + input init (v2). */
    fs_input_init();
    fs_phase_init();
    fs_options_probe_init();   /* Phase 9 Task 9.3 — OPTIONS submenu. */

    /* 6. Music: silent on FS per NES original; call site preserved. */
    music_play(SONG_FS_BIT);

    /* Draw FS_LOAD now, while blanked, rather than exposing the previous
     * title/story nametable with the new CHR for one host frame. Both the
     * blocking and frame-driven entry paths use this same initialization. */
    fs_phase_step();
    render_display_enable(1);
}

/* Cursor row indices (matches fs_render_cursor table + fs_phase contract). */

/* ---- T-099: register / erase / copy --------------------------------- */
#define FS_ROW_COPY   3u
#define FS_ROW_ERASE  4u
#define FS_ROW_RENAME 5u
#define FS_ROW_SOUND 6u
#define SPACE_TILE 0x24u

static uint8_t s_reg_slot;
static uint8_t s_reg_name[8];
static uint8_t s_reg_pos;
static uint8_t s_reg_board;
static uint8_t s_reg_rename;
static uint8_t s_copy_src;
static uint8_t s_confirm_choice;
static uint8_t s_new_mode;
static uint8_t s_file_slot, s_file_row;
static uint8_t s_sound_row;
static void sound_redraw(void) {
    uint8_t start=(uint8_t)((s_sound_row/7u)*7u),count=audio_sound_test_count();
    fs_render_page("SOUND TEST");
    for (uint8_t i=0u;i<7u && start+i<count;i++)
        fs_render_text(8u+2u*i,6u,audio_sound_test_name(start+i));
    fs_render_text(23u,6u,audio_sound_test_source(s_sound_row));
    fs_render_text(25u,6u,"A PLAY START STOP");
    fs_render_text(27u,6u,"B OR C BACK");
    fs_render_page_cursor(8u+2u*(s_sound_row-start));
}
static void sound_step(uint8_t edge) {
    uint8_t count=audio_sound_test_count();
    if (edge&(FS_BTN_B|FS_BTN_C)) {
        audio_music_play(0u); s_fs_cursor=FS_ROW_SOUND; s_fs_phase=FS_LOAD; return;
    }
    if (edge&FS_BTN_UP) s_sound_row=s_sound_row ? s_sound_row-1u : count-1u;
    if (edge&FS_BTN_DOWN) s_sound_row=(uint8_t)((s_sound_row+1u)%count);
    if (edge&FS_BTN_A) audio_sound_test_play(s_sound_row);
    if (edge&FS_BTN_START) audio_music_play(0u);
    if (edge) sound_redraw();
}

static void mode_redraw(void) {
    fs_render_page("CHOOSE FILE MODE");
    fs_render_text(10u,7u,"ORIGINAL");
    fs_render_text(14u,7u,"MD REMIX");
    fs_render_text(20u,5u,"MODE LOCKS WHEN CREATED");
    fs_render_text(24u,6u,"A CREATE B BACK");
    fs_render_page_cursor(s_new_mode ? 14u : 10u);
}
static void file_redraw(void) {
    static const uint8_t rows[6]={8u,12u,16u,19u,21u,26u};
    char players[]="PLAYERS 1";
    players[8]=(char)('0'+s_fs_players_value);
    fs_render_page("SELECT QUEST");
    fs_render_text(6u,7u,save_game_slot_mode(s_file_slot) ? "MD REMIX" : "ORIGINAL");
    fs_render_file_identity(s_file_slot);
    fs_render_text(8u,7u,"QUEST 1");
    fs_render_quest_stats(s_file_slot,0u,9u);
    if (save_game_quest_available(s_file_slot,1u)) {
        fs_render_text(12u,7u,"QUEST 2");
        fs_render_quest_stats(s_file_slot,1u,13u);
    } else fs_render_text(12u,7u,"QUEST 2 LOCKED");
    fs_render_text(16u,7u,save_game_gauntlet_available(s_file_slot) ?
                   "GANON'S GAUNTLET" : "???");
    fs_render_text(19u,7u,players);
    fs_render_text(21u,7u,save_game_slot_mode(s_file_slot) ? "OPTIONS" : "OPTIONS REMIX ONLY");
    fs_render_text(26u,7u,"BACK");
    fs_render_page_cursor(rows[s_file_row]);
}
static void file_enter(uint8_t slot) {
    s_file_slot=slot; s_file_row=save_game_slot_quest(slot);
    s_fs_players_value=save_game_slot_players(slot);
    save_game_options_load(slot);
    s_fs_phase=FS_FILE_MENU; file_redraw();
}
void fs_file_options_exit(unsigned char committed) {
    if (committed) save_game_options_store(s_file_slot);
    else save_game_options_load(s_file_slot);
    s_fs_phase=FS_FILE_MENU; s_file_row=4u; file_redraw();
}
static void file_step(uint8_t edge) {
    if (edge & (FS_BTN_B|FS_BTN_C)) {
        s_fs_cursor=s_file_slot; s_fs_phase=FS_LOAD; return;
    }
    if (edge&FS_BTN_UP) s_file_row=s_file_row ? s_file_row-1u : 5u;
    if (edge&FS_BTN_DOWN) s_file_row=(uint8_t)((s_file_row+1u)%6u);
    if (s_file_row==3u) {
        uint8_t players=s_fs_players_value;
        if ((edge&FS_BTN_LEFT) && players>1u) --players;
        if ((edge&FS_BTN_RIGHT) && players<4u) ++players;
        if (players!=s_fs_players_value) {
            s_fs_players_value=players;
            save_game_set_players(s_file_slot,players);
        }
    }
    if (edge&(FS_BTN_A|FS_BTN_START)) {
        if (s_file_row<2u && save_game_select_quest(s_file_slot,s_file_row)) {
            s_fs_cursor=s_file_slot; s_fs_phase=FS_HANDOFF; return;
        } else if (s_file_row==4u && save_game_slot_mode(s_file_slot)) {
            s_fs_phase=FS_OPTIONS; fs_options_enter(); return;
        } else if (s_file_row==5u) { s_fs_cursor=s_file_slot; s_fs_phase=FS_LOAD; return; }
    }
    if (edge) file_redraw();
}

static void register_redraw(void) {
    fs_render_name_field(s_reg_slot, s_reg_name);
    fs_render_board_cursor(s_reg_board);
    fs_render_name_cursor(1u, (uint8_t)(13u + s_reg_pos),
                          fs_slot_name_row(s_reg_slot));
}

static void register_enter(uint8_t slot) {
    uint8_t i;
    s_reg_slot = slot;
    for (i = 0u; i < 8u; ++i)
        s_reg_name[i] = s_reg_rename ? save_game_slot_name(slot)[i] : SPACE_TILE;
    s_reg_pos = 0u;
    s_reg_board = 0u;
    s_fs_phase = FS_REGISTER;
    fs_render_register_board(slot);
    if (s_reg_rename) fs_render_rename_board(slot);
    register_redraw();
}

static void register_step(uint8_t edge) {
    if (edge & FS_BTN_C) {
        s_fs_cursor = s_reg_rename ? FS_ROW_RENAME : s_reg_slot;
        s_fs_phase = FS_LOAD;
        return;
    }
    if (edge & FS_BTN_START) {
        /* NES: a non-blank name on an inactive slot becomes a new file;
         * a blank name leaves the slot empty. */
        if (s_reg_rename) {
            if (!save_game_rename(s_reg_slot, s_reg_name)) return;
        } else {
            uint8_t blank=1u;
            for (uint8_t i=0u;i<8u;i++) if (s_reg_name[i]!=SPACE_TILE) blank=0u;
            if (blank) return;
            s_new_mode=0u; s_fs_phase=FS_MODE_CHOICE; mode_redraw(); return;
        }
        s_fs_cursor = s_reg_rename ? FS_ROW_RENAME : s_reg_slot;
        s_fs_phase = FS_LOAD;
        return;
    }
    if (edge & FS_BTN_LEFT)  s_reg_board = (uint8_t)((s_reg_board + 43u) % 44u);
    if (edge & FS_BTN_RIGHT) s_reg_board = (uint8_t)((s_reg_board + 1u) % 44u);
    if (edge & FS_BTN_UP)    s_reg_board = (uint8_t)((s_reg_board + 33u) % 44u);
    if (edge & FS_BTN_DOWN)  s_reg_board = (uint8_t)((s_reg_board + 11u) % 44u);
    if (edge & FS_BTN_A) {                      /* ModeE_HandleAOrB: A writes */
        s_reg_name[s_reg_pos] = fs_board_char(s_reg_board);
        s_reg_pos = (uint8_t)((s_reg_pos + 1u) & 7u);
    } else if (edge & FS_BTN_B) {               /* B only moves the cursor */
        s_reg_pos = (uint8_t)((s_reg_pos + 1u) & 7u);
    }
    if (edge) register_redraw();
}

/* Redux has separate source/destination screens and explicit confirmation. */
static void leave_operation(void) {
    s_fs_cursor=s_fs_phase==FS_RENAME_PICK ? FS_ROW_RENAME :
        (s_fs_phase==FS_ERASE_PICK || s_fs_phase==FS_ERASE_CONFIRM) ? FS_ROW_ERASE : FS_ROW_COPY;
    s_fs_phase=FS_LOAD;
}
static void confirm_redraw(void) {
    char erase[]="ERASE PLAYER 1?";
    char copy[]="COPY FROM 1 TO 2?";
    erase[13]=(char)('1'+s_fs_cursor);
    copy[10]=(char)('1'+s_copy_src); copy[15]=(char)('1'+s_fs_cursor);
    fs_render_confirmation(s_fs_phase==FS_ERASE_CONFIRM ? erase : copy,s_confirm_choice,
                           s_fs_phase==FS_COPY_CONFIRM,s_copy_src,s_fs_cursor);
}
static void pick_step(uint8_t edge) {
    if (edge & (FS_BTN_B|FS_BTN_C)) { leave_operation(); return; }
    if (s_fs_phase==FS_ERASE_CONFIRM || s_fs_phase==FS_COPY_CONFIRM) {
        if (edge & (FS_BTN_UP|FS_BTN_DOWN)) { s_confirm_choice^=1u; confirm_redraw(); }
        if (edge & (FS_BTN_A|FS_BTN_START)) {
            if (!s_confirm_choice) {
                if (s_fs_phase==FS_ERASE_CONFIRM) save_game_erase(s_fs_cursor);
                else if (!save_game_copy(s_copy_src,s_fs_cursor)) return;
            }
            leave_operation();
        }
        return;
    }
    if (edge & (FS_BTN_UP|FS_BTN_DOWN)) {
        do {
            s_fs_cursor=(uint8_t)((s_fs_cursor + ((edge & FS_BTN_UP) ? 3u : 1u)) % 4u);
        } while (s_fs_cursor<3u && ((s_fs_phase==FS_COPY_DST)
                 ? s_fs_cursor==s_copy_src : !save_game_slot_active(s_fs_cursor)));
    }
    fs_render_misc_cursor(s_fs_cursor);
    if (!(edge & (FS_BTN_A|FS_BTN_START))) return;
    if (s_fs_cursor==3u) { leave_operation(); return; }
    if (s_fs_phase==FS_RENAME_PICK) {
        s_reg_rename=1u;
        register_enter(s_fs_cursor);
        return;
    }
    if (s_fs_phase==FS_COPY_DST) {
        if (s_fs_cursor==s_copy_src) return;
        s_fs_phase=FS_COPY_CONFIRM;
    } else {
        if (!save_game_slot_active(s_fs_cursor)) return;
        if (s_fs_phase==FS_ERASE_PICK) s_fs_phase=FS_ERASE_CONFIRM;
        else {
            s_copy_src=s_fs_cursor;
            s_fs_phase=FS_COPY_DST;
            s_fs_cursor=s_copy_src==0u ? 1u : 0u;
            fs_render_prompt("TO WHICH SLOT?");
            fs_render_copy_destination(s_copy_src);
            fs_render_misc_cursor(s_fs_cursor);
            return;
        }
    }
    s_confirm_choice=0u; /* Redux starts on OKAY; B always cancels. */
    confirm_redraw();
}

static void fs_input_dispatch(uint8_t edge) {
    if (s_fs_phase==FS_SOUND_TEST) { sound_step(edge); return; }
    if (s_fs_phase==FS_FILE_MENU) { file_step(edge); return; }
    if (s_fs_phase==FS_MODE_CHOICE) {
        if (edge&(FS_BTN_B|FS_BTN_C)) {
            s_fs_phase=FS_REGISTER; fs_render_register_board(s_reg_slot); register_redraw(); return;
        }
        if (edge&(FS_BTN_UP|FS_BTN_DOWN|FS_BTN_LEFT|FS_BTN_RIGHT)) { s_new_mode^=1u; mode_redraw(); }
        if (edge&(FS_BTN_A|FS_BTN_START)) {
            if (save_game_register_mode(s_reg_slot,s_reg_name,s_new_mode)) file_enter(s_reg_slot);
        }
        return;
    }
    if (s_fs_phase == FS_OPTIONS) {
        fs_options_step(edge);
        return;
    }
    if (s_fs_phase == FS_REGISTER) { register_step(edge); return; }
    if (s_fs_phase == FS_COPY_SRC || s_fs_phase == FS_COPY_DST ||
        s_fs_phase == FS_ERASE_PICK || s_fs_phase == FS_ERASE_CONFIRM ||
        s_fs_phase == FS_COPY_CONFIRM || s_fs_phase == FS_RENAME_PICK) { pick_step(edge); return; }
    if (s_fs_phase != FS_NAV) return;
    if (s_fs_cursor==FS_ROW_SOUND && (edge&(FS_BTN_A|FS_BTN_START))) {
        s_sound_row=0u; s_fs_phase=FS_SOUND_TEST; sound_redraw(); return;
    }
    if ((edge & (FS_BTN_A|FS_BTN_START)) && s_fs_cursor >= FS_ROW_COPY) {
        s_fs_phase = s_fs_cursor == FS_ROW_RENAME ? FS_RENAME_PICK :
            (s_fs_cursor == FS_ROW_COPY) ? FS_COPY_SRC : FS_ERASE_PICK;
        s_fs_cursor = 0u;
        fs_render_misc_menu(s_fs_phase==FS_COPY_SRC);
        if (s_fs_phase==FS_RENAME_PICK) fs_render_rename_menu();
        fs_render_misc_cursor(s_fs_cursor);
        return;
    }
    if (edge & FS_BTN_UP) {
        s_fs_cursor=s_fs_cursor==0u ? FS_CURSOR_MAX : (uint8_t)(s_fs_cursor-1u);
        fs_render_cursor(s_fs_cursor);
    }
    if (edge & FS_BTN_DOWN) {
        s_fs_cursor=s_fs_cursor==FS_CURSOR_MAX ? 0u : (uint8_t)(s_fs_cursor+1u);
        fs_render_cursor(s_fs_cursor);
    }
    /* v6.handoff (gated): A on slot row should hand off to transpiled
     * gameplay / register-name. Trampoline + fs_handoff_to_transpiled are
     * wired and the trampoline runs (probe CC, vblank_mode=01), but the
     * transpiled FS state machine ends up stuck at GameSubmode=6 with display
     * off after the swap. Suspected: FrontendStartReleaseGate semantics or
     * leftover PPU register state from native fs_main is poisoning the
     * transpiled InitMode1 sub-phase chain. Trigger disabled until rooted —
     * the trampoline + fs_handoff.c stay compiled so re-enabling is one line.
     */
    /* Re-enabled 2026-05-16: fs_handoff.c now seeds SaveFileOpenMarkers,
     * SaveFileCloseMarkers, IsSaveFileBCommitted in addition to
     * IsSaveSlotActive[0]=1. This makes InitMode1_Full Sub0 take the
     * markers-valid @NextSlot path without writing IsSaveSlotActive,
     * so Sub6 @FindActiveSlot exits on first iteration. */
    if (s_fs_cursor <= 2u && (edge & (FS_BTN_A | FS_BTN_START))) {
        /* T-099: an empty slot registers a name first (NES Mode $E);
         * an occupied slot continues that file. */
        if (save_game_slot_active(s_fs_cursor)) file_enter(s_fs_cursor);
        else { s_reg_rename=0u; register_enter(s_fs_cursor); }
    }
}

void fs_main(void) {
    fs_init();
    for (;;) {
        render_wait_vblank();
        fs_phase_step();
        fs_input_dispatch(fs_input_pressed());
        fs_render_tick(s_fs_cursor,s_fs_phase==FS_NAV);
    }
}

/* ---- Frame-driven entry, for hosts that own the main loop ----
 *
 * fs_main() blocks forever, which is fine for the proof ROM but useless
 * to Zelda.md: its loop in src/platform/game_main.c has to keep running
 * so control can come BACK when the player picks a slot. These two split
 * fs_main into "set up once" and "advance one frame", with the caller
 * owning vblank. fs_main() is left untouched so the proof ROM path is
 * unaffected. */
void fs_enter(void) {
    fs_init();
}

void fs_tick(void) {
    fs_phase_step();
    fs_input_dispatch(fs_input_pressed());
    fs_render_tick(s_fs_cursor,s_fs_phase==FS_NAV);
}
