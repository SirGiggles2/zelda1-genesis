/* Plan v5b Tier-5 T5.5 — audio dispatcher implementation.
 *
 * NES source: reference/aldonunez/Z_07.asm UpdateMode dispatch +
 *             SongRequest writes per gamemode (multiple writers).
 * Drained C : NEW (this file).
 * Coverage  : NONE (NES is byte-cell SongRequest writer; we replace
 *             with gamemode-keyed dispatch — equivalent observable
 *             behavior, different mechanism).
 * Stance    : EXTEND.
 *
 * Design: poll-and-edge-fire. On tuple change, emit one music_play().
 * Same-tuple frames are silent. Idempotent on re-entry: audio_dispatch_reset
 * forces re-fire on the next tick.
 */

#include "audio_dispatch.h"
#include "platform_abi.h"          /* A4-pinned nes_ram pointer */

/* Route through the SGDK adapter so SONG_OW can branch into the XGM Z80
 * driver (VGM playback) while other songs continue down the legacy 68k
 * FM path. See src/sgdk_adapter/audio_adapter.c::audio_music_play. */
extern void audio_music_play(unsigned char song_bitmap);

/* Scene constants — must mirror RoomRom/src/main.c scene_t. */
#define SCENE_OW    0u
#define SCENE_UW    1u
#define SCENE_CAVE  2u

/* GameMode IDs per NES Z_07.asm:1613 UpdateMode_JumpTable (HEX indices).
 * Mode names "Mode 10/11/12/13" in NES asm are HEX, not decimal. */
#define GM_DEMO              0x00u  /* UpdateMode0Demo */
#define GM_FILE_SELECT       0x01u  /* UpdateMode1Menu */
#define GM_LOAD              0x02u  /* UpdateMode2Load */
#define GM_UNFURL            0x03u  /* UpdateMode3Unfurl */
#define GM_ENTER             0x04u  /* UpdateMode4and6EnterLeave */
#define GM_PLAY              0x05u  /* UpdateMode5Play */
#define GM_LEAVE             0x06u  /* UpdateMode4and6EnterLeave */
#define GM_SCROLL            0x07u  /* UpdateMode7Scroll */
#define GM_CONTINUE_QUESTION 0x08u  /* UpdateMode8ContinueQuestion */
/* $09..$0C = UpdateMode5Play variants */
#define GM_SAVE              0x0Du  /* UpdateModeDSave */
#define GM_REGISTER          0x0Eu  /* UpdateModeERegister */
#define GM_ELIMINATION       0x0Fu  /* UpdateModeFElimination */
#define GM_STAIRS            0x10u  /* UpdateMode10Stairs */
#define GM_CAVE              0x0Bu  /* UpdateMode5Play in a regular cave */
#define GM_SHORTCUT_CAVE     0x0Cu  /* UpdateMode5Play in a shortcut cave */
#define GM_DEATH             0x11u  /* UpdateMode11Death — fires own dirge */
#define GM_END_LEVEL         0x12u  /* UpdateMode12EndLevel */
#define GM_WIN_GAME          0x13u  /* UpdateMode13WinGame */

/* Song bitmap IDs — match audio_driver.asm change_song dispatch (line 692+)
 * AND NES Z_07.asm LevelSongIds (line 1725):
 *   LevelSongIds[0] = $01  → OW (overworld) — first_ow path phrase $08
 *   LevelSongIds[1..8] = $40 → UW (dungeon) — first_uw path phrase $0F
 *   LevelSongIds[9] = $20  → L9 death-mountain (driver: single-phrase bit-6)
 *   $80 → title/demo (multi-phrase)
 *   $10 → ending (multi-phrase)
 *   $06 → zelda boss-kill fanfare (phrase $24)
 * Previous SONG_OW=$20 was a transcription bug — driver routed it to the
 * single-phrase bit-6 sample instead of the OW multi-phrase song. */
#define SONG_TITLE      0x80u
#define SONG_UW         0x40u
#define SONG_OW         0x01u
#define SONG_LEVEL9     0x20u
#define SONG_ENDING     0x10u
#define SONG_ZELDA      0x06u   /* boss-kill / triforce reveal fanfare */
#define SONG_END_LEVEL  0x04u   /* NES InitMode12 SongRequest = $04 — distinct from $10 ending */
#define SONG_SILENCE    0x00u

/* Last-published tuple. Initialized to sentinel so first tick fires. */
#define TUPLE_SENTINEL 0xFFu
static unsigned char s_last_gm    = TUPLE_SENTINEL;
static unsigned char s_last_scene = TUPLE_SENTINEL;
static unsigned char s_last_song  = TUPLE_SENTINEL;

/* Resolve target song from (gamemode, scene).
 * Per docs/audit/audio_routing.md routing table. */
static unsigned char resolve_song(unsigned char gm, unsigned char scene)
{
    switch (gm) {
    case GM_DEMO:
        return SONG_TITLE;

    case GM_FILE_SELECT:
    case GM_REGISTER:
    case GM_ELIMINATION:
    case GM_CONTINUE_QUESTION:
    case GM_SAVE:
        return SONG_SILENCE;

    case GM_LOAD:
    case GM_UNFURL:
    case GM_ENTER:
    case GM_LEAVE:
    case GM_SCROLL:
    case GM_STAIRS:
        /* Transient — keep last song through the transition. */
        return s_last_song;

    case GM_CAVE:
    case GM_SHORTCUT_CAVE:
        /* HandleWarpOW -> SetTargetMode silences music before the stairs.
         * Cave mode must not restart OW while Link is underground. */
        return SONG_SILENCE;

    case GM_PLAY:
        if (scene == SCENE_UW)   return SONG_UW;
        if (scene == SCENE_CAVE) return SONG_SILENCE;
        return SONG_OW;

    case GM_DEATH:
        /* Mode 11 Death fires its own death-tune ($80 Tune1Request) at
         * Sub1 and Game Over music ($40 Tune1Request) at SubC. Don't
         * double-fire here — return last so dispatcher stays quiet. */
        return s_last_song;

    case GM_END_LEVEL:
        /* NES Z_05.asm:5516 InitMode12 writes SongRequest = $04 — the
         * "End Level" tune (single-phrase, change_song bitloop path
         * @audio_driver.asm:748 -> phrase 3). Distinct from the
         * multi-phrase $10 Ending. */
        return SONG_END_LEVEL;
    case GM_WIN_GAME:
        return SONG_ENDING;

    default:
        /* Unknown gamemode → no change. */
        return s_last_song;
    }
}

void audio_dispatch_tick(unsigned char scene, unsigned char room_id)
{
    unsigned char gm = nes_ram[0x0012u];
    (void)room_id;  /* reserved for future cave/boss room keying */

    if (gm == s_last_gm && scene == s_last_scene) {
        return;  /* tuple unchanged */
    }

    unsigned char song = resolve_song(gm, scene);
    if (song != s_last_song && song != TUPLE_SENTINEL) {
        audio_music_play(song);
        s_last_song = song;
    }
    s_last_gm    = gm;
    s_last_scene = scene;
}

void audio_dispatch_reset(void)
{
    s_last_gm    = TUPLE_SENTINEL;
    s_last_scene = TUPLE_SENTINEL;
    s_last_song  = TUPLE_SENTINEL;
}
