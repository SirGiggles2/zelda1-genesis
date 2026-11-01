#include "audio_requests.h"
#include "platform_abi.h"
#include "../../abi/audio_abi.h"
#include "../../state/game_profile.h"

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
 * source. XGM has four PCM voices; Original reserves CH1 for music drums:
 * noise effects CH3, Tune0 CH4, Tune1 CH2. Original retains its DMC/CH2
 * sharing. Redux gives DMC CH1 so a door's independent DMC cannot truncate
 * the push-block secret tune on CH2. DMC priority8 briefly preempts music
 * percussion (XGM priorities0..3); later music commands resume normally.
 * A monster kill requests Tune0 $02 and Tune1 $20 in the
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

static unsigned char s_sample_id, s_sample_channel;
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
    unsigned char req = F_ram_0601_N;
    if (req == 0u) return;
    F_ram_0601_N = 0u;
    req &= 0x7Fu;
    if (req) {
        /* NES source: DriveSample and DriveTune1 are independent voices.
         * Drained C: this consumer; Coverage: PARTIAL(Redux PCM routing).
         * Stance: EXTEND, retaining Original and same-source replacement. */
        unsigned char channel=game_profile_is_remix() ? 1u : CH_SAMPLE;
        unsigned char id=(unsigned char)(64u + lowest_bit(req));
        if(s_sample_id && s_sample_channel!=channel)
            audio_pcm_stop(s_sample_id,s_sample_channel);
        audio_pcm_play(id,8u,channel);
        s_sample_id=id;s_sample_channel=channel;
    }
}

static void start_effect(unsigned char i)
{
    s_effect = (unsigned char)(1u << i);
    s_effect_frames = k_effect_frames[i];
    audio_pcm_play(k_effect_id[i], 8u, CH_EFFECT);
}

static void drive_effect(void)
{
    unsigned char req = F_ram_0603_N;
    unsigned char i;
    F_ram_0603_N = 0u;
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
    unsigned char r0 = F_ram_0604_N;
    unsigned char r1 = F_ram_0602_N2;
    F_ram_0604_N = 0u;
    F_ram_0602_N2 = 0u;

    /* Tune1 (square 1). */
    if (r1 != 0u) {
        /* NES @SilenceThenPlay forces $80 even when lower bits are set. */
        unsigned char b = (r1 & 0x80u) ? 7u : lowest_bit(r1);
        /* NES source: Z_00.asm:DriveTune1 @SilenceThenPlay/SilenceSong.
         * Drained C: existing drive_tunes; Coverage: FULL; Stance: EXTEND.
         * Silence the song before starting the death PCM. This request
         * belongs to this NMI consumer, never the legacy VBlank poll. */
        if (r1 & 0x80u) audio_music_play(0u);
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
