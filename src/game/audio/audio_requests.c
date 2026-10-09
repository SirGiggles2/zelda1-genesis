#include "audio_requests.h"
#include "platform_abi.h"
#include "../../abi/audio_abi.h"

/* T-127: NES sound requests -> Genesis PCM, the only place gameplay sound
 * is started.
 *
 * NES source: Z_00.asm DriveSample (SampleRequest $0601: lowest bit picks
 * the DMC sample; a new request replaces the playing one), DriveEffect
 * (EffectRequest $0603: noise effects in priority order sword, arrow,
 * flame, stairs, bomb, sea; a request starts its effect, a lower one is
 * ignored while a higher effect plays, the sea continues before a new sea
 * request), DriveTune0 (Tune0Request $0604: $80 silences, $40 heart
 * warning only when Tune0 is idle, else lowest bit), DriveTune1
 * (Tune1Request $0602: $80 silences then plays Link dying, lowest bit;
 * Game Over $40 repeats).
 * Genesis PCM (XGM ids): DMC samples 64..70 (tools/extract_dmc_samples.py,
 * NES table order = request bit order), stairs 71 (synth_stairs_sfx.py),
 * sword/arrow/flame/bomb/sea 72..76 (tools/audio/synth_noise_sfx.py),
 * Tune0 bits 0..6 = 77..83 and Tune1 bits 0..7 = 84..91
 * (tools/audio/synth_square_sfx.py). NES has one hardware channel per
 * source; XGM gives SFX three (CH1 plays the music's drum samples):
 * noise effects CH3, Tune0 CH4, Tune1 CH2 shared with the DMC samples
 * (latest wins). A monster kill requests Tune0 $02 and Tune1 $20 in the
 * same frame; both are heard, as on NES. */
#define NES_SAMPLE_REQUEST 0x0601u
#define NES_TUNE1_REQUEST  0x0602u
#define NES_EFFECT_REQUEST 0x0603u
#define NES_TUNE0_REQUEST  0x0604u

#define CH_SAMPLE 2u
#define CH_EFFECT 3u
#define CH_TUNE0  4u
#define CH_TUNE1  2u

/* Effect bit -> (PCM id, length in frames). */
static const unsigned char k_effect_id[6]     = { 72u, 73u, 74u, 71u, 75u, 76u };
static const unsigned char k_effect_frames[6] = { 0x0Au, 0x05u, 0x20u, 0x38u, 0x18u, 0xD0u };
/* Tune lengths in frames (sfx_pcm_tunes.c sfx_pcm_tune_frames). */
extern const unsigned short sfx_pcm_tune_frames[];

static unsigned char s_effect, s_effect_frames;
static unsigned char s_tune0, s_tune1;          /* playing request bit, 0 = none */
static unsigned short s_tune0_frames, s_tune1_frames;

static unsigned char lowest_bit(unsigned char v)
{
    unsigned char i = 0u;
    while ((v & 1u) == 0u) { v >>= 1; ++i; }
    return i;
}

/* T-192 NES source: Z_01.asm:SilenceAllSound clears Tune0/Tune1;
 * Z_00.asm:DriveTune1 repeats Game Over only while Tune1 is $40.
 * Drained C: core_runtime.c:corert_silence_all_sound.
 * Coverage: PARTIAL (native tune mirrors outlived the source clear).
 * Stance: EXTEND the shared reset boundary to native state/PCM owners. */
void audio_requests_silence_tunes(void)
{
    if (s_tune0 != 0u)
        audio_pcm_stop((unsigned char)(77u + lowest_bit(s_tune0)), CH_TUNE0);
    if (s_tune1 != 0u)
        audio_pcm_stop((unsigned char)(84u + lowest_bit(s_tune1)), CH_TUNE1);
    s_tune0 = s_tune1 = 0u;
    s_tune0_frames = s_tune1_frames = 0u;
}

static void drive_sample(void)
{
    unsigned char req = nes_ram[NES_SAMPLE_REQUEST];
    if (req == 0u) return;
    nes_ram[NES_SAMPLE_REQUEST] = 0u;
    req &= 0x7Fu;
    if (req) audio_pcm_play((unsigned char)(64u + lowest_bit(req)), 8u, CH_SAMPLE);
}

static void start_effect(unsigned char i)
{
    s_effect = (unsigned char)(1u << i);
    s_effect_frames = k_effect_frames[i];
    audio_pcm_play(k_effect_id[i], 8u, CH_EFFECT);
}

static void drive_effect(void)
{
    unsigned char req = nes_ram[NES_EFFECT_REQUEST];
    unsigned char i;
    nes_ram[NES_EFFECT_REQUEST] = 0u;
    if (req & 0x80u) { s_effect = 0u; return; }
    for (i = 0u; i < 5u; ++i) {
        unsigned char bit = (unsigned char)(1u << i);
        if (req & bit) { start_effect(i); return; }
        if (s_effect == bit) {
            if (--s_effect_frames == 0u) s_effect = 0u;
            return;
        }
    }
    if (s_effect == 0x20u) {
        if (--s_effect_frames == 0u) s_effect = 0u;
        return;
    }
    if (req & 0x20u) start_effect(5u);
}

static void drive_tunes(void)
{
    unsigned char r0 = nes_ram[NES_TUNE0_REQUEST];
    unsigned char r1 = nes_ram[NES_TUNE1_REQUEST];
    nes_ram[NES_TUNE0_REQUEST] = 0u;
    nes_ram[NES_TUNE1_REQUEST] = 0u;

    /* Tune1 (square 1). */
    if (r1 != 0u) {
        unsigned char b = lowest_bit(r1);          /* $80 -> bit 7 (Link dying) */
        s_tune1 = (unsigned char)(1u << b);
        s_tune1_frames = sfx_pcm_tune_frames[7u + b];
        audio_pcm_play((unsigned char)(84u + b), 8u, CH_TUNE1);
    } else if (s_tune1 != 0u) {
        if (--s_tune1_frames == 0u) {
            if (s_tune1 == 0x40u) {                /* Game Over repeats */
                s_tune1_frames = sfx_pcm_tune_frames[7u + 6u];
                audio_pcm_play(84u + 6u, 8u, CH_TUNE1);
            } else {
                s_tune1 = 0u;
            }
        }
    }

    /* Tune0 (square 0). */
    if (r0 & 0x80u) {
        s_tune0 = 0u;
        /* NES DriveTune0 routes bit 7 to SilenceSong, which clears the
         * currently playing song as well as Tune0. The XGM-owned OW track
         * bypasses legacy music_tick, so consume that request here. */
        audio_music_play(0u);
    } else if (r0 != 0u && !(r0 == 0x40u && s_tune0 != 0u)) {
        unsigned char b = lowest_bit(r0);
        s_tune0 = (unsigned char)(1u << b);
        s_tune0_frames = sfx_pcm_tune_frames[b];
        audio_pcm_play((unsigned char)(77u + b), 8u, CH_TUNE0);
    } else if (s_tune0 != 0u) {
        if (--s_tune0_frames == 0u) s_tune0 = 0u;
    }
}

void audio_requests_consume(void)
{
    drive_sample();
    drive_effect();
    drive_tunes();
}
