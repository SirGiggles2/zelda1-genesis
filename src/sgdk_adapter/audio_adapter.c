/*
 * audio_adapter.c — bridge between owned game code and SGDK XGM audio.
 *
 * Music path forwards to the legacy music_play / music_tick in
 * src/audio_driver.asm (FM driver). SFX path is now wired to the SGDK
 * XGM (Doppler) Z80 driver: 4 PCM channels mixed at 14 kHz, with the 7
 * NES DMC samples ripped from the stock ROM registered as XGM SFX
 * sample IDs 64..70 (XGM reserves 1..63 for music).
 */

#include "audio_adapter.h"
#include "sfx_pcm.h"
#include "../../data/audio/sfx_pcm_tunes.h"   /* NES square tunes: Tune0/Tune1 */
#include "../../data/audio/sfx_pcm_noise.h"   /* NES noise effects: sword/arrow/flame/bomb/sea */
#include "../../data/audio/sfx_pcm_stairs.h"  /* NES cave stairs SFX (#8 / XGM id 71), synth */
#include "platform_abi.h"   /* nes_ram (A4-pinned) for probe sentinels */
#include "../../data/audio_music/ow_theme_xgm.h"
#include "../../data/audio_music/uw_theme_xgm.h"
#include <z80_ctrl.h>
#include <snd/sound.h>
#include <snd/xgm.h>

extern void music_play(unsigned char song_bitmap);
extern void music_tick(void);
extern void music_silence(void);

/* Driver storage is linker-owned; fixed low-RAM addresses collide with C BSS.
 * Native RAM base follows platform_abi (A4 Debug or pointer RoomRom). */
/* aligned(4): audio_driver.asm clears these with clr.w/clr.l and keeps a
 * 32-bit script pointer at m_script_ptr (+$08); 68000 word/long access to
 * an odd address is an address-error exception. */
volatile unsigned char audio_apu_shadow[0x16] __attribute__((aligned(4)));
volatile unsigned char *audio_native_ram_base = 0;

/* Legacy 68k music player state (asm MUSIC_BASE, $40 bytes) and DMC
 * register shadows (asm DMC_BASE, $10 bytes). Previously fixed at
 * $FFE000/$FFE100, which is nes_ram[$6000]/[$6100] under the $FF8000 A4
 * base: NES SaveRAM (save image slot 0, WorldFlags0). A Mode $0D save
 * wrote magic $5A/$A5 into m_song/m_song_req and cleared xgm_owns_chip
 * (T-094, probe_music_sram_overlap.lua). Linker-owned storage now. */
volatile unsigned char audio_music_state[0x40] __attribute__((aligned(4)));
volatile unsigned char audio_dmc_state[0x10] __attribute__((aligned(4)));

static u8 xgm_initialized = 0;
static u8 s_current_xgm_song = 0;
static u8 sfx_next_channel = 0;  /* round-robin index 0..2 → CH2..CH4 */
static u8 s_pcm_ids[4];          /* latest requested sample on each PCM channel */

/* XGM-driver song ownership flag, shared with src/audio_driver.asm's
 * music_tick (label `xgm_owns_chip` at MUSIC_BASE+$2C).
 * Writing this from C is sufficient — the asm gate reads the same byte.
 * When 1, the XGM Z80 driver is playing a VGM/XGM song (currently the
 * OW theme blob) and owns FM ch1-5 + PSG. The legacy 68k FM driver MUST
 * NOT tick while this is set, or its tick_sq1 writes will race the
 * Z80's chip accesses. Cleared when XGM_stopPlay hands ownership back. */
static volatile u8 * const xgm_owns_chip_ptr = &audio_music_state[0x2C];
static volatile u8 * const music_song_ptr = &audio_music_state[0x00];
static volatile u8 * const music_song_req_ptr = &audio_music_state[0x01];

#define SONG_OW_BITMAP  0x01u
#define SONG_UW_BITMAP  0x40u

static const u8 *xgm_blob_for_song(unsigned char song)
{
    if (song == SONG_OW_BITMAP) return ow_theme_xgm;
    if (song == SONG_UW_BITMAP) return uw_theme_xgm;
    return 0;
}

void audio_xgm_init(void)
{
    audio_native_ram_base = nes_ram;
    if (xgm_initialized) return;
    xgm_initialized = 1;

    Z80_loadDriver(Z80_DRIVER_XGM, 1);

    for (u8 i = 0; i < SFX_PCM_COUNT; i++) {
        XGM_setPCM(sfx_pcm_table[i].id,
                   sfx_pcm_table[i].data,
                   sfx_pcm_table[i].len);
    }
    /* SFX #8 = NES cave stairs (XGM id 71 = SFX_PCM_ID_BASE + 7), synthesized
     * separately from the DMC bank (it is an APU-noise sound, not a sample). */
    XGM_setPCM(SFX_PCM_STAIRS_ID, sfx_pcm_stairs, SFX_PCM_STAIRS_LEN);
    /* SFX #9..#13 = the other NES noise effects (EffectRequest bits 0, 1,
     * 2, 4, 5), XGM ids 72..76 (tools/audio/synth_noise_sfx.py). */
    XGM_setPCM(SFX_PCM_SWORD_ID, sfx_pcm_sword, SFX_PCM_SWORD_LEN);
    XGM_setPCM(SFX_PCM_ARROW_ID, sfx_pcm_arrow, SFX_PCM_ARROW_LEN);
    XGM_setPCM(SFX_PCM_FLAME_ID, sfx_pcm_flame, SFX_PCM_FLAME_LEN);
    XGM_setPCM(SFX_PCM_BOMB_ID,  sfx_pcm_bomb,  SFX_PCM_BOMB_LEN);
    XGM_setPCM(SFX_PCM_SEA_ID,   sfx_pcm_sea,   SFX_PCM_SEA_LEN);
    /* NES square-channel tunes (tools/audio/synth_square_sfx.py): ids
     * 77..91 = Tune0 bits 0..6, Tune1 bits 0..7. */
    for (u8 i = 0; i < SFX_PCM_TUNE_COUNT; i++)
        XGM_setPCM((u8)(SFX_PCM_TUNE_FIRST_ID + i), sfx_pcm_tune_data[i],
                   sfx_pcm_tune_len[i]);
}

/* T-127: play PCM `id` on XGM PCM channel `ch` (1..4) with priority `prio`
 * (0..15; a sample only replaces one of equal or lower priority). Used by
 * src/game/audio/audio_requests.c, which gives each NES sound source its own
 * channel: DMC samples 2, noise effects 3, square tunes 4. */
unsigned char  g_audio_pcm_last;
unsigned short g_audio_pcm_calls;

void audio_pcm_play(unsigned char id, unsigned char prio, unsigned char ch)
{
    if (!xgm_initialized) audio_xgm_init();
    if (ch < 1u || ch > 4u) return;
    XGM_startPlayPCM(id, prio, (SoundPCMChannel)(SOUND_PCM_CH1 + (ch - 1u)));
    s_pcm_ids[ch - 1u] = id;
    /* Probe counters (read from the 68K RAM dump by symbol). */
    g_audio_pcm_last = id;
    g_audio_pcm_calls++;
}

void audio_pcm_stop(unsigned char id, unsigned char ch)
{
    if (!xgm_initialized || ch < 1u || ch > 4u) return;
    if (s_pcm_ids[ch - 1u] != id) return;
    XGM_stopPlayPCM((SoundPCMChannel)(SOUND_PCM_CH1 + (ch - 1u)));
    s_pcm_ids[ch - 1u] = 0u;
}

void audio_music_play(unsigned char song)
{
    const u8 *xgm_song = xgm_blob_for_song(song);

    /* NES Tune0Request bit 7 silences the song. Stopping XGM releases
     * the chip for cave event tunes; key-off also clears legacy voices. */
    if (song == 0u) {
        if (*xgm_owns_chip_ptr) XGM_stopPlay();
        *xgm_owns_chip_ptr = 0u;
        s_current_xgm_song = 0u;
        *music_song_req_ptr = 0u;
        music_silence();
        return;
    }

    /* OW and UW route through SGDK's XGM Z80 driver. The checked-in
     * ow_theme_xgm.c and uw_theme_xgm.c arrays are compiled XGC-style
     * blobs produced by xgmtool from the source VGMs. */
    if (xgm_song) {
        if (!xgm_initialized) audio_xgm_init();

        /* Keep legacy debug/probe song cells coherent even while the
         * legacy tick is gated off and cannot consume m_song_req itself. */
        *music_song_ptr = song;
        *music_song_req_ptr = 0;

        if (!*xgm_owns_chip_ptr || s_current_xgm_song != song) {
            if (*xgm_owns_chip_ptr) {
                XGM_stopPlay();
            }
            *xgm_owns_chip_ptr = 1;       /* gate legacy music_tick BEFORE Z80 starts */
            XGM_startPlay(xgm_song);
            s_current_xgm_song = song;
        }
        return;
    }

    /* Non-OW song: hand chip ownership back to legacy FM driver. */
    if (*xgm_owns_chip_ptr) {
        XGM_stopPlay();
        *xgm_owns_chip_ptr = 0;
        s_current_xgm_song = 0;
    }
    music_play(song);
}

void audio_sfx_play(unsigned char sfx)
{
    if (!xgm_initialized) audio_xgm_init();
    /* 1..SFX_PCM_COUNT = DMC bank; 8 = synth stairs (id 71); 9..13 =
     * sword/arrow/flame/bomb/sea (ids 72..76): id = SFX_PCM_ID_BASE+sfx-1. */
    if (sfx == 0 || sfx > SFX_PCM_COUNT + 6) return;

    SoundPCMChannel chan = (SoundPCMChannel)(SOUND_PCM_CH2 + sfx_next_channel);
    sfx_next_channel = (sfx_next_channel + 1) % 3;

    XGM_startPlayPCM(SFX_PCM_ID_BASE + (sfx - 1), 1, chan);
    s_pcm_ids[chan - SOUND_PCM_CH1] = (u8)(SFX_PCM_ID_BASE + (sfx - 1));

    /* Probe sentinel — count XGM SFX dispatches at NES RAM $07F0 (last
     * sfx id) and $07F1 (call counter). Lets bizhawkScript probes verify
     * the audio_sfx_play path fires without going through dmc_trigger
     * (which only writes dmc_last_idx for NES APU $4015 writes). */
    DBG_SENTINEL(0x10u) = sfx;
    DBG_SENTINEL(0x11u) = (unsigned char)(DBG_SENTINEL(0x11u) + 1u);
}

void audio_tick_vblank(void)
{
    /* music_tick itself gates on xgm_owns_chip (set above). The check
     * here is redundant but cheap and makes intent explicit at the call
     * site; the asm gate is the source of truth and covers genesis_shell
     * VBlankISR's direct music_tick callers too. */
    if (*xgm_owns_chip_ptr) return;
    music_tick();
}
