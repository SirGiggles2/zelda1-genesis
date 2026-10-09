/*
 * Audio API public surface (S1 Phase D, Task D2).
 *
 * Owned C in src/frontend/ and src/game/ calls audio_* functions
 * here instead of calling music_play / music_tick directly. The
 * implementation (src/sgdk_adapter/audio_adapter.c) initially forwards
 * to existing music_play / music_tick in src/audio_driver.asm; Phase F
 * migrates those forwarders onto SGDK audio API per call site.
 *
 * Spec ref: 2026-04-27-native-genesis-rewrite-design.md Section 4.3
 * Plan retarget (step 3/4) deferred to Phase F - D2 is compile-only.
 */

#ifndef AUDIO_ABI_H
#define AUDIO_ABI_H

/* One-time XGM driver upload + sample registration. Safe to call
 * multiple times; subsequent calls are no-ops. Runs Z80 reset release,
 * uploads the XGM Doppler driver, and registers all 7 ripped DMC SFX
 * samples (IDs 64..70) so XGM_startPlayPCM can dispatch them. */
void audio_xgm_init(void);

/* Request a song change. song is the song bitmap passed to music_play.
 * Call from any mode; music_tick in the VBlank handler picks it up. */
void audio_music_play(unsigned char song);

/* Request-engine PCM channels (1..4). Stop only the requested sample
 * while it still owns that channel; a newer DMC/SFX must survive. */
void audio_pcm_play(unsigned char id, unsigned char prio, unsigned char ch);
void audio_pcm_stop(unsigned char id, unsigned char ch);

/* Play a one-shot sound effect. sfx is the 1-based DMC sample index
 * (1..7) that the NES $4015 stub recovers via DMC_SAMPLE_LOOKUP. Maps
 * to XGM SFX ID 64+(sfx-1) and dispatches on a round-robin XGM PCM
 * channel (CH2..CH4) so music's CH1 stays clear. */
void audio_sfx_play(unsigned char sfx);

/* VBlank audio tick. Call once per VBlank (replaces direct music_tick
 * call in genesis_shell.asm VBlank handler). Retarget deferred to
 * Phase F; genesis_shell.asm still calls music_tick directly in D-phase. */
void audio_tick_vblank(void);

#endif /* AUDIO_ABI_H */
