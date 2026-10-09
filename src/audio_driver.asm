; Phase 12.2 audio link: inlined music_blob.inc constants (auto-generated
; by extract_audio.py; manually mirrored here to avoid gas MRI include
; resolution issues with paths containing spaces. Keep these in sync
; with src/data/music_blob.inc whenever extract_audio.py regenerates.)
MUSIC_BLOB_NES_BASE equ $8D60
MUSIC_BLOB_SIZE     equ 2757

;==============================================================================
; audio_driver.asm — YM2612 + PSG driver for NES APU emulation
;
; Maps NES APU channels to Genesis sound hardware using Sonic 2 Emerald Hill
; Zone FM patches:
;   Pulse 1 → YM2612 ch 0 (EHZ Voice $00, Algorithm 7)
;   Pulse 2 → YM2612 ch 1 (EHZ Voice $03, Algorithm 5)
;   Triangle → YM2612 ch 2 (EHZ Voice $07, Algorithm 0)
;   Noise   → SN76489 PSG ch 3
;
; Include BEFORE nes_io.asm so helpers are available to APU stubs.
;==============================================================================

;----------------------------------------------------------------------
; Hardware ports
;----------------------------------------------------------------------
YM_ADDR1        equ $A04000     ; YM2612 Part I address  (ch 0-2 + global regs)
YM_DATA1        equ $A04001     ; YM2612 Part I data
YM_ADDR2        equ $A04002     ; YM2612 Part II address (ch 3-5 / ch 6 DAC pan)
YM_DATA2        equ $A04003     ; YM2612 Part II data
PSG_PORT        equ $C00011     ; SN76489 PSG data

;----------------------------------------------------------------------
; NES source: Z_07.asm:DriveAudio / Tune request cells.
; Drained owner: audio_adapter.c + music_tick bridge.
; Coverage: PARTIAL native request/storage addressing; stance EXTEND.
; Linker-owned shadow storage cannot alias the current C globals.
; Native RAM pointer is installed by audio_xgm_init for the active ABI.
;----------------------------------------------------------------------
    xref audio_apu_shadow
    xref audio_native_ram_base
APU_SH_BASE     equ audio_apu_shadow
APU_SH_4000     equ APU_SH_BASE+$00    ; Pulse 1 duty/volume
APU_SH_4001     equ APU_SH_BASE+$01    ; Pulse 1 sweep (unused)
APU_SH_4002     equ APU_SH_BASE+$02    ; Pulse 1 period low
APU_SH_4003     equ APU_SH_BASE+$03    ; Pulse 1 period high + length
APU_SH_4004     equ APU_SH_BASE+$04    ; Pulse 2 duty/volume
APU_SH_4005     equ APU_SH_BASE+$05    ; Pulse 2 sweep (unused)
APU_SH_4006     equ APU_SH_BASE+$06    ; Pulse 2 period low
APU_SH_4007     equ APU_SH_BASE+$07    ; Pulse 2 period high + length
APU_SH_4008     equ APU_SH_BASE+$08    ; Triangle linear counter
APU_SH_400A     equ APU_SH_BASE+$0A    ; Triangle period low
APU_SH_400B     equ APU_SH_BASE+$0B    ; Triangle period high + length
APU_SH_400C     equ APU_SH_BASE+$0C    ; Noise volume/envelope
APU_SH_400E     equ APU_SH_BASE+$0E    ; Noise mode + period
APU_SH_400F     equ APU_SH_BASE+$0F    ; Noise length counter
APU_SH_4015     equ APU_SH_BASE+$10    ; Channel enable flags

;----------------------------------------------------------------------
; Frequency conversion constants
;
; NES pulse:    f = CPU_CLK / (16 * (P+1)),   CPU_CLK = 1,789,773
; NES triangle: f = CPU_CLK / (32 * (P+1))    (one octave lower)
; YM2612:       f = (F_num * 2^block * YM_CLK) / (144 * 2^20)
;                   YM_CLK = 7,670,453
;
; K_pulse = CPU_CLK * 144 * 2^20 / (16 * YM_CLK) ≈ 4,403,410
; K5_pulse = K_pulse >> 5 = 137,606    (base block = 5)
; K5_tri   = K5_pulse / 2 = 68,803     (triangle is one octave below pulse)
;
; Sanity check: NES A4 (P=253, P+1=254) → K5/254 = 541.75
;               Standard YM2612 F_num for A4 at block 5 = 541. ✓
;----------------------------------------------------------------------
NES_YM_K5_PULSE equ 137606
NES_YM_K5_TRI   equ 68803

;==============================================================================
; ym_write1 — Write one register to YM2612 Part I
;
; Input:  D0.b = register address
;         D1.b = data value
; Output: none
; Preserves: all registers
;
; SR lockout: the address+data pair MUST go out as an atomic latched write.
; With the HBlank-driven DMC streamer enabled, the HINT handler also writes
; $2A + data to YM2612 Part I; if it preempts between our address write and
; data write, the YM latch is left pointing at $2A and our subsequent data
; byte lands in the DAC instead of the intended register.  Mask HINT (level
; 4) for the duration of the pair by raising SR interrupt level to 6, then
; restore the caller's SR on exit.  Cost: ~16 cycles per call.
;==============================================================================
ym_write1:
    move.w  SR,-(SP)                ; save caller's SR
    ori.w   #$0600,SR               ; mask HINT (level 4) + its below
.wait_ym1:
    tst.b   (YM_ADDR1).l            ; bit 7 = busy flag
    bmi.s   .wait_ym1
    move.b  D0,(YM_ADDR1).l         ; write register address
    nop                              ; address setup delay
    nop
    nop
    move.b  D1,(YM_DATA1).l         ; write data
    move.w  (SP)+,SR                ; restore SR (re-enables HINT)
    rts

;==============================================================================
; ym_write2 — Write one register to YM2612 Part II
;
; Used for ch 4-5 regs and ch 6 pan ($B4+2 = $B6 on Part II).  The busy flag
; is shared across both parts so we still poll Part I.  Same SR lockout as
; ym_write1 to prevent HINT-based DMC streamer from stomping the latch.
;
; Input:  D0.b = register address
;         D1.b = data value
; Output: none
; Preserves: all registers
;==============================================================================
ym_write2:
    move.w  SR,-(SP)
    ori.w   #$0600,SR
.wait:
    tst.b   (YM_ADDR1).l            ; busy flag is shared between parts
    bmi.s   .wait
    move.b  D0,(YM_ADDR2).l         ; write register address (Part II)
    nop
    nop
    nop
    move.b  D1,(YM_DATA2).l         ; write data (Part II)
    move.w  (SP)+,SR
    rts

;==============================================================================
; nes_to_ym_freq — Convert NES 11-bit period to YM2612 frequency
;
; Input:  D2.w = 11-bit NES period (0–2047)
;         D3.l = K constant (NES_YM_K5_PULSE or NES_YM_K5_TRI)
; Output: D2.b = A4 register value (block<<3 | F_num bits 10:8)
;         D3.b = A0 register value (F_num bits 7:0)
; Destroys: D4, D5
;==============================================================================
nes_to_ym_freq:
    addq.w  #1,D2                   ; D2 = P + 1
    cmp.w   #2,D2
    blo.s   .silence                ; period 0 → silence
    divu    D2,D3                   ; D3.l / D2.w → quotient in D3.w
    bvs.s   .silence                ; overflow safety
    move.w  D3,D5                   ; D5 = raw quotient = F_num at block 5
    moveq   #5,D4                   ; starting block (K was pre-shifted by 5)
.norm:
    cmp.w   #2047,D5
    bls.s   .done_freq              ; F_num fits in 11 bits
    lsr.w   #1,D5                   ; halve F_num
    addq.b  #1,D4                   ; increment block
    cmp.b   #7,D4
    bhi.s   .clamp                  ; block overflow → clamp
    bra.s   .norm
.done_freq:
    ; D5.w = F_num (0–2047), D4.b = block (0–7)
    move.b  D4,D2
    lsl.b   #3,D2                   ; block << 3
    move.w  D5,D3
    lsr.w   #8,D3                   ; F_num >> 8 = high 3 bits
    or.b    D3,D2                   ; D2.b = (block<<3) | F_num_high
    move.b  D5,D3                   ; D3.b = F_num_low
    rts
.clamp:
    ; Frequency too high for YM2612 — clamp to max
    move.b  #$3F,D2                 ; block 7, F_num high = 7
    move.b  #$FF,D3                 ; F_num low = $FF → F_num = 2047
    rts
.silence:
    moveq   #0,D2
    moveq   #0,D3
    rts

;==============================================================================
; load_fm_patch — Load a 25-byte FM patch into a YM2612 channel
;
; Input:  A0 = pointer to 25-byte patch data
;         D6.b = channel (0, 1, or 2)
; Output: none
; Preserves: D0-D6, A0-A1 (saved/restored internally)
;==============================================================================
load_fm_patch:
    movem.l D0-D2/A0-A1,-(SP)
    lea     FM_PATCH_REGS(PC),A1
    moveq   #24,D2                  ; 25 register/value pairs
.loop:
    move.b  (A1)+,D0                ; register base address
    add.b   D6,D0                   ; + channel offset (0/1/2)
    move.b  (A0)+,D1                ; data value
    bsr     ym_write1
    dbra    D2,.loop
    movem.l (SP)+,D0-D2/A0-A1
    rts

;==============================================================================
; audio_init — Initialize YM2612 and PSG for NES audio emulation
;
; Called once during boot. Sets up FM patches and mutes all channels.
; Preserves: A4, A5, D7 (NES register shadows)
;==============================================================================
audio_init:
    movem.l D0-D6/A0,-(SP)

    ;------------------------------------------------------------------
    ; Key-off all 6 YM2612 channels
    ;------------------------------------------------------------------
    move.b  #$28,D0                 ; Key on/off register
    moveq   #0,D1
    bsr     ym_write1               ; ch 0 off
    move.b  #$01,D1
    bsr     ym_write1               ; ch 1 off
    move.b  #$02,D1
    bsr     ym_write1               ; ch 2 off
    move.b  #$04,D1
    bsr     ym_write1               ; ch 4 off (Part II)
    move.b  #$05,D1
    bsr     ym_write1               ; ch 5 off
    move.b  #$06,D1
    bsr     ym_write1               ; ch 6 off

    ;------------------------------------------------------------------
    ; DMC scaffold (Phase C): enable YM2612 DAC on ch 6 and unmute its
    ; pan register so PCM bytes streamed to reg $2A become audible.
    ;
    ; $2B bit 7 = DAC enable (replaces ch 6's FM synth path).  We also
    ; write a center value ($80) to $2A so the DAC sits at mid-scale
    ; until dmc_feed starts pushing real samples.  $B6 (Part II) is ch 6
    ; pan/AMS/PMS — $C0 enables both L+R outputs.
    ;------------------------------------------------------------------
    move.b  #$2B,D0                 ; DAC enable register
    move.b  #$80,D1                 ; bit 7 = DAC on
    bsr     ym_write1
    move.b  #$2A,D0                 ; DAC data
    move.b  #$80,D1                 ; center value = silence
    bsr     ym_write1
    move.b  #$B6,D0                 ; ch 6 pan/AMS/PMS (Part II)
    move.b  #$C0,D1                 ; L+R enabled
    bsr     ym_write2

    ;------------------------------------------------------------------
    ; LFO off (register $22 = 0)
    ;------------------------------------------------------------------
    move.b  #$22,D0
    moveq   #0,D1
    bsr     ym_write1

    ;------------------------------------------------------------------
    ; Clear SSG-EG for every operator on ch 0, 1, 2 ($90-$9E on Part I).
    ;
    ; The YM2612 SSG-EG register per operator has no power-on guarantee
    ; on Genesis hardware and may hold leftover state across soft resets.
    ; If bit 3 (SSG enable) is set, the envelope generator runs in a
    ; sawtooth/square-like "SSG mode" that produces an audible buzz or
    ; hiss alongside the clean FM output — we were hearing that on ch 1
    ; (Voice $00 pad) and ch 2 (Voice $07 bass).
    ;
    ; Zero-ing $90+op+ch for all 4 operators on each used channel puts
    ; every envelope generator back in standard mode.  12 writes total.
    ;------------------------------------------------------------------
    moveq   #0,D1                   ; data = 0 for all
    move.b  #$90,D0                 ; slot1 ch0
    bsr     ym_write1
    move.b  #$91,D0                 ; slot1 ch1
    bsr     ym_write1
    move.b  #$92,D0                 ; slot1 ch2
    bsr     ym_write1
    move.b  #$94,D0                 ; slot3 ch0
    bsr     ym_write1
    move.b  #$95,D0                 ; slot3 ch1
    bsr     ym_write1
    move.b  #$96,D0                 ; slot3 ch2
    bsr     ym_write1
    move.b  #$98,D0                 ; slot2 ch0
    bsr     ym_write1
    move.b  #$99,D0                 ; slot2 ch1
    bsr     ym_write1
    move.b  #$9A,D0                 ; slot2 ch2
    bsr     ym_write1
    move.b  #$9C,D0                 ; slot4 ch0
    bsr     ym_write1
    move.b  #$9D,D0                 ; slot4 ch1
    bsr     ym_write1
    move.b  #$9E,D0                 ; slot4 ch2
    bsr     ym_write1

    ;------------------------------------------------------------------
    ; Load EHZ Voice $03 into ch 0 (m_sq1 = NES Sq1 = Pulse 2 = LEAD melody)
    ; NES naming: Sq1/Sq0 are reversed from intuition — Sq1 writes $4006/$4007
    ; (Pulse 2) and owns the primary melody.  Put the snappy EHZ lead here.
    ;------------------------------------------------------------------
    lea     PATCH_VOICE03(PC),A0
    moveq   #0,D6                   ; channel 0 (emit_note_sq1 target)
    bsr     load_fm_patch

    ;------------------------------------------------------------------
    ; Load EHZ Voice $00 into ch 1 (m_sq0 = NES Sq0 = Pulse 1 = harmony/arp)
    ; The slow-decay pad fits arpeggiated harmony better than a sharp lead.
    ;------------------------------------------------------------------
    lea     PATCH_VOICE00(PC),A0
    moveq   #1,D6                   ; channel 1 (emit_note_sq0 target)
    bsr     load_fm_patch

    ;------------------------------------------------------------------
    ; Load EHZ Voice $07 into ch 2 (Triangle — Algorithm 0, bass)
    ;------------------------------------------------------------------
    lea     PATCH_VOICE07(PC),A0
    moveq   #2,D6                   ; channel 2
    bsr     load_fm_patch

    ;------------------------------------------------------------------
    ; Set panning: both speakers for ch 0–2
    ; Register $B4+ch, data $C0 = L+R output
    ;------------------------------------------------------------------
    move.b  #$B4,D0
    move.b  #$C0,D1
    bsr     ym_write1               ; ch 0 panning
    move.b  #$B5,D0
    bsr     ym_write1               ; ch 1 panning
    move.b  #$B6,D0
    bsr     ym_write1               ; ch 2 panning

    ;------------------------------------------------------------------
    ; (Phase E polish: the Phase-C FM isolation mute that forced every
    ; operator TL to $7F has been removed — FM music plays again alongside
    ; the DMC DAC stream now that HBlank-driven non-blocking playback is
    ; in place.  Patches loaded by load_fm_patch above already set the
    ; authoritative TL bytes for each voice.)
    ;------------------------------------------------------------------

    ;------------------------------------------------------------------
    ; Mute all 4 PSG channels (attenuation = $F = maximum)
    ;------------------------------------------------------------------
    move.b  #$9F,(PSG_PORT).l       ; Tone 1 mute
    move.b  #$BF,(PSG_PORT).l       ; Tone 2 mute
    move.b  #$DF,(PSG_PORT).l       ; Tone 3 mute
    move.b  #$FF,(PSG_PORT).l       ; Noise mute

    ;------------------------------------------------------------------
    ; Clear shadow APU registers
    ;------------------------------------------------------------------
    lea     (APU_SH_BASE).l,A0
    moveq   #($11/2),D0            ; 18 bytes / 2 = 9 words (round up)
.clr:
    clr.w   (A0)+
    dbra    D0,.clr

    ;------------------------------------------------------------------
    ; Clear native music player state
    ;------------------------------------------------------------------
    lea     (MUSIC_BASE).l,A0
    moveq   #(MUSIC_STATE_SIZE/4)-1,D0
.clr_music:
    clr.l   (A0)+
    dbra    D0,.clr_music

    ;------------------------------------------------------------------
    ; Clear DMC shadow block (DMC_BASE, 16 bytes). The remaining state
    ; is just dmc_last_idx (HUD readout) plus the three NES-reg
    ; shadows ($4010/$4012/$4013) used by the lookup in nes_io.asm —
    ; everything else (active flag, ptr, remain, debug scaffolds)
    ; went away with the HBlank streamer in the XGM migration.
    ;------------------------------------------------------------------
    lea     (DMC_BASE).l,A0
    clr.l   (A0)+
    clr.l   (A0)+
    clr.l   (A0)+
    clr.l   (A0)+

    ;------------------------------------------------------------------
    ; Hand audio off to the SGDK XGM Z80 driver. audio_xgm_init() does:
    ;   1. Z80_loadDriver(Z80_DRIVER_XGM, TRUE)  - upload + wait ready
    ;   2. XGM_setPCM(64..70, sfx_pcm_NN, len)   - register 7 SFX samples
    ; After this, dmc_trigger trampolines to audio_sfx_play and the Z80
    ; owns YM2612 reg $2A. The legacy HBlank streamer is dead.
    ;
    ; 2026-05-17 coexistence enable. Earlier comment about XGM "muting
    ; FM channels" was based on an init-time observation: XGM init
    ; writes PSG OFF once and clears YM2B/2A (DAC). Both happen ONCE
    ; at driver load. After that the FM driver re-writes its own FM
    ; channel state and PSG envelopes on the next music_tick. XGM main
    ; loop continually writes YM $2A (DAC) for PCM mixing but does NOT
    ; touch FM channels 1-5 or PSG when no XGM music is playing.
    ; Net: XGM owns ch 6 DAC; legacy FM driver owns ch 1-5 + PSG.
    ; First SFX trigger pays the lazy-init cost; subsequent SFX use
    ; the already-resident driver. Driver size ~$300 bytes Z80 RAM,
    ; well under SGDK 8 KB Z80 RAM budget.
    ;------------------------------------------------------------------
    xref    audio_xgm_init
    jsr     audio_xgm_init

    movem.l (SP)+,D0-D6/A0
    rts

;==============================================================================
; FM Patch Register Address Table
;
; 25 register base addresses (add channel 0/1/2 for target channel).
; Order matches the 25 data bytes in each PATCH_VOICExx block.
;==============================================================================
    even
FM_PATCH_REGS:
    dc.b    $B0                     ; FB/ALG
    dc.b    $30,$34,$38,$3C         ; DT1/MUL (slot1,slot3,slot2,slot4)
    dc.b    $50,$54,$58,$5C         ; RS/AR
    dc.b    $60,$64,$68,$6C         ; AM/D1R
    dc.b    $70,$74,$78,$7C         ; D2R
    dc.b    $80,$84,$88,$8C         ; DL/RR
    dc.b    $40,$44,$48,$4C         ; TL
    even

;==============================================================================
; FM Patch Data — Sonic 2 Emerald Hill Zone voices
;
; 25 bytes each.  Canonical Sonic 2 voice format from sonicretro/s2disasm
; sound/music/82 - EHZ.asm (label EHZ_Voices):
;
;   byte 0      : FB/ALG (feedback<<3 | algorithm)
;   bytes 1-4   : DT/MUL  — operators in slot order: op1, op3, op2, op4
;   bytes 5-8   : RS/AR    "
;   bytes 9-12  : AM/D1R   "
;   bytes 13-16 : D2R      "
;   bytes 17-20 : DL/RR    "
;   bytes 21-24 : TL       "
;
; The slot order op1,op3,op2,op4 matches YM2612 register offsets 0,4,8,C in
; FM_PATCH_REGS, so the bytes stream directly to the right operator slots
; without any reordering in load_fm_patch.
;
; NOTE: the original SMPS driver uses bit 7 ($80) on TL bytes as an "absolute,
; do not add channel volume" flag — it masks bit 7 before writing and channel
; volume is added for operators without the flag.  We have no per-channel
; volume, so we translate $80 directly to a safe carrier TL of $18 (matching
; Voice $03's proven-clean carrier level) and keep the authoritative
; modulator TL bytes as-is.
;
; ym_trace.lua (2026-04-10) proved the driver was writing these bytes
; verbatim to the YM2612.  A prior transcription of Voice $00 and Voice $07
; had the middle two bytes of DT/MUL, RS/AR, AM/D1R, and TL swapped — putting
; op2's parameters onto op3 and vice versa.  On Voice $03 the middle values
; are near-symmetric so the swap is inaudible; on Voices $00 and $07 the
; middle values differ enough that the swap produced cross-wired FM
; modulation = sustained "crunchy buzz" on both channels.  Restored to the
; authoritative byte order here.
;==============================================================================

; Voice $00 — Bell-like pad.
;
; Classic FM bell recipe:
;   Alg 5 (3 carriers modulated by op1)
;   FB 5 for brightness without the FB 7 "hard bite"
;   Op1 MUL 4 → produces inharmonic overtones at 4x the carrier pitch,
;     which is what gives bells their characteristic "chime" quality
;   Op1 TL $14 → modulator loud enough to shape the bell spectrum
;   Carriers at MUL 1, tuned unison (no detune beating)
;   Fast attack, medium D1R, low DL, slow RR = bell strike + sustain tail
PATCH_VOICE00:
    dc.b    $2D                     ; FB/ALG: (5<<3)|5 — FB 5 bell pad
    dc.b    $04,$01,$01,$01         ; DT/MUL — op1 MUL 4 (bell modulator)
    dc.b    $1F,$1F,$1F,$1F         ; RS/AR max
    dc.b    $08,$04,$04,$04         ; AM/D1R — bell-decay shape
    dc.b    $00,$00,$00,$00         ; D2R
    dc.b    $47,$37,$37,$17         ; DL 4/3 + RR 7/7/7/7 — bell tail
    dc.b    $14,$1E,$1E,$1E         ; TL — op1 mod loud ($14), carriers quieter ($1E)
    even

; Voice $03 — EHZ FM3 opening voice (iconic lead)
; Algorithm 5 (slots 2,3,4 are carriers), Feedback 7
PATCH_VOICE03:
    dc.b    $3D                     ; FB/ALG: (7<<3)|5
    dc.b    $01,$51,$21,$01         ; DT/MUL
    dc.b    $1F,$1F,$1F,$1F         ; RS/AR (max — EHZ's slow-attack swell was delaying Zelda's Pulse 2 melody)
    dc.b    $0A,$05,$05,$05         ; AM/D1R
    dc.b    $00,$00,$00,$00         ; D2R
    dc.b    $2B,$2B,$2B,$1B         ; DL/RR
    dc.b    $19,$18,$18,$18         ; TL (slot1=modulator $19; slots 2/3/4 carriers +$18 for mix)
    even

; Voice $07 — Clean bass.
;
; Pure sine-ish bass: Alg 5, low FB, all operators at MUL 1 with no detune.
; This is the cleanest possible FM bass — no harmonics beyond what the fun-
; damental already provides.  Moderately quiet so it supports the lead
; without fighting it.
PATCH_VOICE07:
    dc.b    $0D                     ; FB/ALG: (1<<3)|5 — FB 1 (minimal grit)
    dc.b    $01,$01,$01,$01         ; DT/MUL — unison, no beating
    dc.b    $1F,$1F,$1F,$1F         ; RS/AR
    dc.b    $0A,$05,$05,$05         ; AM/D1R
    dc.b    $00,$00,$00,$00         ; D2R
    dc.b    $28,$28,$28,$18         ; DL 2 / RR 8 — bass punch
    dc.b    $22,$20,$20,$20         ; TL — moderately quiet (less than 27.81's $28)
    even

;==============================================================================
;==============================================================================
; NATIVE M68K MUSIC PLAYER
;
; Ports the NES Zelda DriveSong engine verbatim, reading the same script
; opcodes but emitting YM2612/PSG writes instead of NES APU writes.
;
; State lives at $FF0B00.  Song data is pulled from the contiguous
; MusicBlob (SongTable + 9 song headers + 9 song scripts, 2757 bytes)
; loaded at label MusicBlob.  NES CPU addresses in song headers are
; converted to M68K pointers via (nes_addr - MUSIC_BLOB_NES_BASE) + MusicBlob.
;==============================================================================
;==============================================================================

;----------------------------------------------------------------------
; Music state RAM ($FF0B00-$FF0B3F, 64 bytes)
;----------------------------------------------------------------------
; ============================================================================
; MUSIC_BASE / DMC_BASE are linker-owned C arrays in audio_adapter.c.
; The old fixed $FFE000/$FFE100 assumed an A4 base of $FF0000; under the
; Zelda.md base of $FF8000 they are nes_ram[$6000]/[$6100] = NES SaveRAM,
; and a Mode $0D save overwrote m_song/m_song_req (T-094).
; ============================================================================
    xref audio_music_state
    xref audio_dmc_state
MUSIC_BASE          equ audio_music_state
MUSIC_STATE_SIZE    equ $40

m_song              equ MUSIC_BASE+$00  ; current song bitmap
m_song_req          equ MUSIC_BASE+$01  ; requested song change
m_phrase            equ MUSIC_BASE+$02  ; current phrase index
m_len_base          equ MUSIC_BASE+$03  ; note length table base
m_env_sel           equ MUSIC_BASE+$04  ; envelope selector
m_mystery           equ MUSIC_BASE+$05  ; header byte 7 ($05F1 equiv)
m_noise_first       equ MUSIC_BASE+$06  ; noise loop start
m_script_ptr        equ MUSIC_BASE+$08  ; long: M68K pointer to script base

; Sq1 (Pulse 1 → YM ch 0)
m_sq1_off           equ MUSIC_BASE+$0C
m_sq1_cnt           equ MUSIC_BASE+$0D
m_sq1_len           equ MUSIC_BASE+$0E
m_sq1_vib           equ MUSIC_BASE+$0F
m_sq1_per           equ MUSIC_BASE+$10

; Sq0 (Pulse 2 → YM ch 1)
m_sq0_off           equ MUSIC_BASE+$14
m_sq0_cnt           equ MUSIC_BASE+$15
m_sq0_len           equ MUSIC_BASE+$16
m_sq0_vib           equ MUSIC_BASE+$17
m_sq0_per           equ MUSIC_BASE+$18

; Triangle (→ YM ch 2)
m_trg_off           equ MUSIC_BASE+$1C
m_trg_cnt           equ MUSIC_BASE+$1D
m_trg_len           equ MUSIC_BASE+$1E
m_trg_vib           equ MUSIC_BASE+$1F
m_trg_per           equ MUSIC_BASE+$20
m_trg_rep_n         equ MUSIC_BASE+$21
m_trg_rep_s         equ MUSIC_BASE+$22

; Noise (→ PSG ch 3)
m_noise_off         equ MUSIC_BASE+$24
m_noise_cnt         equ MUSIC_BASE+$25
; Software ADSR state for the PSG noise drum channel (Kroc Sonic 1 SMS pattern).
; SN76489 has no envelope hardware, so we run a linear software envelope and
; map it to the 4-bit PSG attenuation every frame.  Writing the noise control
; register resets the LFSR, so we also cache the last-written control byte and
; skip redundant writes to preserve the decay tail.
m_noise_level       equ MUSIC_BASE+$26  ; byte: current envelope level ($00-$FF)
m_noise_decay       equ MUSIC_BASE+$27  ; byte: current linear decay rate (per tick)
m_last_psg_nc       equ MUSIC_BASE+$28  ; byte: last noise-control byte written

; Noise-SFX shadow state. Zelda's PlaySfxNote writes $400E/$400C/$400F
; per sfx-tick. The first two cache here; $400F is the per-hit trigger.
m_sfx_vol           equ MUSIC_BASE+$29  ; byte: last $400C byte
m_sfx_ctrl_raw      equ MUSIC_BASE+$2A  ; byte: last $400E byte
; Set to 1 by tick_sq1.song_ended each time the song script reaches its
; loop point (opcode $00). C-side intro_story polls this to sync the
; final-hold-then-loop-back-to-title transition with the music loop.
; Owner clears it; ASM only ever writes 1.
m_song_loop_pending equ MUSIC_BASE+$2B  ; byte: 1 = song just looped

; XGM-owns-chip flag. Set to 1 by audio_adapter.c::audio_music_play when
; the SGDK XGM Z80 driver begins playing a VGM/XGM song (currently the
; overworld theme blob). music_tick checks this and bails immediately
; so the 68k legacy FM driver cannot race the Z80's chip writes. Cleared
; when audio_music_play stops the XGM song to hand FM ownership back.
xgm_owns_chip       equ MUSIC_BASE+$2C  ; byte: 1 = XGM Z80 driver owns FM/PSG

;----------------------------------------------------------------------
; DMC NES-register shadows. The XGM Z80 driver owns playback now; the
; only state we keep on the 68K side is what nes_io.asm needs to
; recover the 1-based sample index from the NES $4010/$4012 writes
; (DMC_SAMPLE_LOOKUP) plus a HUD readout cell.
;----------------------------------------------------------------------
DMC_BASE            equ audio_dmc_state
dmc_last_idx        equ DMC_BASE+$01    ; byte: last triggered sample index (for HUD)
dmc_rate_sel        equ DMC_BASE+$0C    ; byte: shadow of last $4010 write
dmc_addr_sel        equ DMC_BASE+$0D    ; byte: shadow of last $4012 write
dmc_len_sel         equ DMC_BASE+$0E    ; byte: shadow of last $4013 write

;==============================================================================
; dmc_trigger — trampoline to SGDK XGM PCM playback.
;
; D0.b = 1-based sample index (1..7). Forwards to audio_sfx_play, which
; maps to XGM SFX ID 64+(idx-1) and dispatches on a round-robin XGM PCM
; channel (CH2..CH4). Z80 mixer streams to YM2612 reg $2A; the legacy
; HBlank DAC streamer below is dead (kept temporarily for reference, to
; be deleted in step 5).
;
; Sample format conversion lives in tools/extract_dmc_samples.py:
; bandlimited resample (scipy resample_poly + Kaiser FIR) from native NES
; rate to XGM's fixed 14 kHz, padded to 256-byte boundaries, emitted as
; aligned C arrays in data/audio/sfx_pcm.c.
;==============================================================================
; Phase 10.3 audio link: DMC_SAMPLE_COUNT was xref'd from sfx_pcm.h
; (extern const u8). gas MRI can't resolve that as an 8-bit immediate
; (truncation error). Inline the value here; keep in sync with
; data/audio/sfx_pcm.h SFX_PCM_COUNT.
DMC_SAMPLE_COUNT equ 7
    xref    audio_sfx_play
dmc_trigger:
    tst.b   D0
    beq.s   .bad
    cmp.b   #DMC_SAMPLE_COUNT,D0
    bhi.s   .bad
    move.b  D0,(dmc_last_idx).l     ; HUD readout still uses this
    move.l  D0,-(SP)                ; GCC m68k ABI: arg in stack
    jsr     audio_sfx_play
    addq.l  #4,SP
.bad:
    rts

;==============================================================================
; music_play — request a song change
; Input: byte arg on stack (GCC m68k ABI). Low byte of 32-bit slot at 7(SP).
;
; Was D0.b. C callers compiled `music_play(0x80)` as `pea #$80; jsr music_play`
; which pushes the immediate to stack WITHOUT loading D0 — music_play then
; stored garbage D0 to m_song_req. audio_dispatch_tick worked by accident
; because GCC emitted `moveb d2,d0; movel d0,sp@-` for variable args. Read
; from stack to match the documented GCC m68k convention used elsewhere
; (see dmc_trigger:603 "GCC m68k ABI: arg in stack").
;==============================================================================
    xdef    music_play
music_play:
    move.b  7(SP),D0
    move.b  D0,(m_song_req).l
    rts

;==============================================================================
; music_tick — DriveSong equivalent.  Call once per VBlank.
;
; Gated by `xgm_owns_chip` (defined below): when the SGDK XGM Z80 driver
; is playing a VGM/XGM song, it owns FM ch1-5 + PSG and a legacy 68k tick
; would race its writes. Callers from genesis_shell.asm VBlankISR and the
; SGDK VInt hook both hit this entry, so the gate covers every path.
;==============================================================================
    xdef    music_tick
music_tick:
    tst.b   (xgm_owns_chip).l
    bne     .xgm_owned_skip
    movem.l D0-D7/A0-A2,-(SP)
    ;------------------------------------------------------------------
    ; Phase E polish: FM music path restored.  DMC streams independently
    ; from HBlank via dmc_hint_tick, so the main-thread music tick no
    ; longer needs to stay muted.  ym_write1/2 carry SR lockout so the
    ; HINT-driven DAC streamer can't corrupt the YM2612 latch between
    ; address and data bytes.
    ;
    ; Structure matches the pre-isolation (Zelda27.82) tick: consume
    ; song_req via change_song, otherwise tick_sq1 if a song is playing.
    ; dmc_dbg_poll + dmc_feed run after either path.  dmc_feed is now a
    ; labeled rts — the call costs ~20 cycles total and lets us defer
    ; removing it to a later cleanup pass.
    ;------------------------------------------------------------------
    ; Attract-mode-exit hook: the game's DriveAudio is NOP'd, so the
    ; audio driver's own SilenceSong paths (via DriveTune0 → $0604 bit7
    ; and DriveTune1 → $0602 bit7) never run.  Game logic still writes
    ; the request bytes at state transitions.  The attract-to-file-menu
    ; path (UpdateMode0Demo_Sub0 → SilenceAllSound in z_01.asm:4118)
    ; sets $0604 = $80 and $0603 = $80.  Poll both $0604 and $0602 for
    ; the silence-request bit 7 and route it into native music_silence.
    ; Also clear $0605/$0607 (current-tune shadows) so the game's own
    ; "nothing playing" markers stay coherent with our silenced state.
    ; SongRequest bridge: game writes native RAM $0600 (SongRequest) at
    ; every song change (title/gameplay/dungeon/etc.). Forward to m_song_req
    ; and clear the game-side byte so the game's own driver-absent check
    ; ("did my song change get consumed?") stays coherent.
    movea.l (audio_native_ram_base).l,A0
    cmpa.l  #0,A0
    beq.s   .no_silence_req
    move.b  $0600(A0),D0
    beq.s   .no_song_req_bridge
    clr.b   $0600(A0)
    move.b  D0,(m_song_req).l
.no_song_req_bridge:
    move.b  $0604(A0),D0          ; Tune 0 request (SilenceAllSound)
    bmi.s   .do_silence
    move.b  $0602(A0),D0          ; Tune 1 request (silence-then-play)
    bpl.s   .no_silence_req
.do_silence:
    clr.b   $0602(A0)
    clr.b   $0603(A0)
    clr.b   $0604(A0)
    clr.b   $0605(A0)
    clr.b   $0607(A0)
    bsr     music_silence
    bra.s   .done
.no_silence_req:
    move.b  (m_song_req).l,D0
    beq.s   .no_req
    clr.b   (m_song_req).l
    move.b  D0,(m_song).l
    bsr     change_song
    bra.s   .done
.no_req:
    tst.b   (m_song).l
    beq.s   .done
    bsr     tick_sq1
.done:
    ; SFX dispatch lives on the SGDK XGM Z80 driver now (Z80 mixes PCM
    ; on its own clock); no per-VBlank DMC drain or scaffold poll here.
    movem.l (SP)+,D0-D7/A0-A2
    rts
.xgm_owned_skip:
    ; XGM Z80 driver is playing music (OW VGM blob). Skip legacy tick
    ; entirely so the 68k cannot race FM/PSG writes. No registers pushed
    ; on this path, so a bare rts suffices.
    rts

;==============================================================================
; change_song — ported from @ChangeSong
; Input: D0.b = new song bitmap (already stored in m_song)
;
; Selects the starting phrase index for the new song, then falls into
; prep_phrase / play_next_phrase to begin playback.  Final step performs
; an immediate tick_sq1 (matching KeepPlayingSong fall-through).
;==============================================================================
change_song:
    tst.b   D0
    bmi.s   .first_demo                 ; bit 7 → demo/title
    cmp.b   #$06,D0
    bne.s   .not_zelda
    move.b  #$24,(m_phrase).l           ; zelda: phrase $24 directly
    bsr     prep_phrase
    bra     tick_sq1
.not_zelda:
    cmp.b   #$01,D0
    beq.s   .first_ow
    cmp.b   #$40,D0
    beq.s   .first_uw
    cmp.b   #$10,D0
    bne     play_next_phrase            ; single-phrase song
    move.b  #$11,(m_phrase).l           ; ending: SetPrev then fall into PlayNext
    bra     play_next_phrase
.first_ow:
    move.b  #$08,(m_phrase).l
    bra     play_next_phrase
.first_uw:
    move.b  #$0F,(m_phrase).l
    bra     play_next_phrase
.first_demo:
    move.b  #$19,(m_phrase).l
    bra     play_next_phrase

;==============================================================================
; play_next_phrase — ported from PlayNextPhrase
; Increments phrase index for multi-phrase songs (with wrap), then
; calls prep_phrase and falls into tick_sq1.
;==============================================================================
play_next_phrase:
    move.b  (m_song).l,D0
    bmi.s   .next_demo                  ; bit 7 → demo (multi)
    cmp.b   #$01,D0
    beq.s   .next_ow
    cmp.b   #$40,D0
    beq.s   .next_uw
    cmp.b   #$10,D0
    beq.s   .next_ending
    ; Single-phrase song: find bit position, phrase = index
    moveq   #0,D1
    moveq   #0,D2
    move.b  D0,D2
.bitloop:
    addq.b  #1,D1
    lsr.b   #1,D2
    bcc.s   .bitloop
    move.b  D1,(m_phrase).l
    bsr     prep_phrase
    bra     tick_sq1
.next_ending:
    addq.b  #1,(m_phrase).l
    cmpi.b  #$1A,(m_phrase).l
    bne.s   .prep_and_tick
    move.b  #$14,(m_phrase).l
    bra     play_next_phrase
.next_uw:
    addq.b  #1,(m_phrase).l
    cmpi.b  #$12,(m_phrase).l
    bne.s   .prep_and_tick
    move.b  #$0F,(m_phrase).l
    bra     play_next_phrase
.next_ow:
    addq.b  #1,(m_phrase).l
    cmpi.b  #$10,(m_phrase).l
    bne.s   .prep_and_tick
    move.b  #$09,(m_phrase).l
    bra     play_next_phrase
.next_demo:
    addq.b  #1,(m_phrase).l
    cmpi.b  #$24,(m_phrase).l
    bne.s   .prep_and_tick
    move.b  #$19,(m_phrase).l
    bra     play_next_phrase
.prep_and_tick:
    bsr     prep_phrase
    bra     tick_sq1

;==============================================================================
; prep_phrase — ported from PrepPhrase
; Reads 8-byte header at MusicBlob[MusicBlob[m_phrase-1]].
;==============================================================================
prep_phrase:
    lea     (MusicBlob).l,A1
    moveq   #0,D1
    move.b  (m_phrase).l,D1
    subq.w  #1,D1
    move.b  (A1,D1.w),D1                ; indirect: inner index
    andi.w  #$FF,D1

    ; byte 0: NoteLengthTableBase
    move.b  (A1,D1.w),(m_len_base).l
    addq.w  #1,D1
    ; byte 1: script ptr lo
    moveq   #0,D2
    move.b  (A1,D1.w),D2
    addq.w  #1,D1
    ; byte 2: script ptr hi
    moveq   #0,D3
    move.b  (A1,D1.w),D3
    addq.w  #1,D1
    lsl.w   #8,D3
    or.w    D2,D3                        ; D3 = NES CPU address
    sub.w   #MUSIC_BLOB_NES_BASE,D3
    andi.l  #$FFFF,D3
    lea     (MusicBlob).l,A0
    adda.l  D3,A0                        ; A0 = M68K script base
    move.l  A0,(m_script_ptr).l

    ; byte 3: NoteOffsetSongTrg
    move.b  (A1,D1.w),(m_trg_off).l
    addq.w  #1,D1
    ; byte 4: NoteOffsetSongSq0
    move.b  (A1,D1.w),(m_sq0_off).l
    addq.w  #1,D1
    ; byte 5: NoteOffsetSongNoise (also FirstNoiseIdx)
    move.b  (A1,D1.w),(m_noise_off).l
    move.b  (A1,D1.w),(m_noise_first).l
    addq.w  #1,D1
    ; byte 6: SongEnvelopeSelector
    move.b  (A1,D1.w),(m_env_sel).l
    addq.w  #1,D1
    ; byte 7: mystery
    move.b  (A1,D1.w),(m_mystery).l

    ; Reset counters and Sq1 offset
    move.b  #1,(m_sq1_cnt).l
    move.b  #1,(m_sq0_cnt).l
    move.b  #1,(m_trg_cnt).l
    move.b  #1,(m_noise_cnt).l
    clr.b   (m_sq1_off).l
    clr.b   (m_trg_rep_n).l
    rts

;==============================================================================
; tick_sq1 — Sq1 channel update (primary: owns phrase advance on song-end)
;==============================================================================
tick_sq1:
    subq.b  #1,(m_sq1_cnt).l
    bne     tick_sq0                     ; no new note
.read_sq1:
    move.l  (m_script_ptr).l,A0
    moveq   #0,D1
    move.b  (m_sq1_off).l,D1
    addq.b  #1,(m_sq1_off).l
    move.b  (A0,D1.w),D0
    beq.s   .song_ended
    tst.b   D0
    bmi.s   .prep_then_note              ; bit 7 set → control byte
    bra.s   .play_note
.prep_then_note:
    bsr     get_song_note_length         ; D0 in → D0 length
    move.b  D0,(m_sq1_len).l
    move.l  (m_script_ptr).l,A0
    moveq   #0,D1
    move.b  (m_sq1_off).l,D1
    addq.b  #1,(m_sq1_off).l
    move.b  (A0,D1.w),D0
.play_note:
    bsr     emit_note_sq1                 ; D0 = note id
    move.b  (m_sq1_len).l,(m_sq1_cnt).l
    bra     tick_sq0
.song_ended:
    move.b  #1,(m_song_loop_pending).l   ; signal loop point to C poller
    move.b  (m_song).l,D0
    andi.b  #$F1,D0
    bne.s   .play_again
    bra     music_silence
.play_again:
    bsr     play_next_phrase_no_tick
    bra.s   .read_sq1

;----------------------------------------------------------------------
; play_next_phrase_no_tick — variant that ends with rts (via prep_phrase
; tail call) instead of falling into tick_sq1.  Used from inside tick_sq1
; to restart a looping song when $00 opcode is read.
;----------------------------------------------------------------------
play_next_phrase_no_tick:
    move.b  (m_song).l,D0
    bmi     .nsp_demo
    cmp.b   #$01,D0
    beq     .nsp_ow
    cmp.b   #$40,D0
    beq     .nsp_uw
    cmp.b   #$10,D0
    beq     .nsp_ending
    ; single-phrase: rebuild phrase from bit
    moveq   #0,D1
    moveq   #0,D2
    move.b  D0,D2
.nsp_loop:
    addq.b  #1,D1
    lsr.b   #1,D2
    bcc     .nsp_loop
    move.b  D1,(m_phrase).l
    bra     prep_phrase
.nsp_ending:
    addq.b  #1,(m_phrase).l
    cmpi.b  #$1A,(m_phrase).l
    bne     prep_phrase
    move.b  #$14,(m_phrase).l         ; wrap: set prev, re-enter (will +1)
    bra     play_next_phrase_no_tick
.nsp_uw:
    addq.b  #1,(m_phrase).l
    cmpi.b  #$12,(m_phrase).l
    bne     prep_phrase
    move.b  #$0F,(m_phrase).l
    bra     play_next_phrase_no_tick
.nsp_ow:
    addq.b  #1,(m_phrase).l
    cmpi.b  #$10,(m_phrase).l
    bne     prep_phrase
    move.b  #$09,(m_phrase).l
    bra     play_next_phrase_no_tick
.nsp_demo:
    addq.b  #1,(m_phrase).l
    cmpi.b  #$24,(m_phrase).l
    bne     prep_phrase
    move.b  #$19,(m_phrase).l
    bra     play_next_phrase_no_tick

;==============================================================================
; tick_sq0 — Sq0 channel update
;==============================================================================
tick_sq0:
    tst.b   (m_sq0_off).l                 ; if starting offset 0 → channel off
    beq     tick_trg
    subq.b  #1,(m_sq0_cnt).l
    bne     tick_trg
    move.l  (m_script_ptr).l,A0
    moveq   #0,D1
    move.b  (m_sq0_off).l,D1
    addq.b  #1,(m_sq0_off).l
    move.b  (A0,D1.w),D0
    tst.b   D0
    bpl.s   .play
    bsr     get_song_note_length
    move.b  D0,(m_sq0_len).l
    move.l  (m_script_ptr).l,A0
    moveq   #0,D1
    move.b  (m_sq0_off).l,D1
    addq.b  #1,(m_sq0_off).l
    move.b  (A0,D1.w),D0
.play:
    bsr     emit_note_sq0
    move.b  (m_sq0_len).l,(m_sq0_cnt).l
    bra     tick_trg

;==============================================================================
; tick_trg — Triangle channel update (handles $F0/$F1-$FF passage ops)
;==============================================================================
tick_trg:
    tst.b   (m_trg_off).l
    beq     tick_noise
    subq.b  #1,(m_trg_cnt).l
    bne     tick_noise
.prep_note_or_passage:
    move.l  (m_script_ptr).l,A0
    moveq   #0,D1
    move.b  (m_trg_off).l,D1
    addq.b  #1,(m_trg_off).l
    move.b  (A0,D1.w),D0
    beq.s   .silence_trg                  ; $00 → silence triangle this step
    tst.b   D0
    bpl.s   .play_note_trg                ; direct note
    cmpi.b  #$F0,D0
    beq.s   .end_of_passage
    bcs.s   .prep_note_trg                ; $80-$EF → control
    ; $F1-$FF → start passage
    subi.b  #$F0,D0
    move.b  D0,(m_trg_rep_n).l
    move.b  (m_trg_off).l,(m_trg_rep_s).l
    bra.s   .prep_note_or_passage
.end_of_passage:
    subq.b  #1,(m_trg_rep_n).l
    beq.s   .pass_done
    move.b  (m_trg_rep_s).l,(m_trg_off).l
.pass_done:
    bra.s   .prep_note_or_passage
.prep_note_trg:
    bsr     get_song_note_length
    move.b  D0,(m_trg_len).l
    move.l  (m_script_ptr).l,A0
    moveq   #0,D1
    move.b  (m_trg_off).l,D1
    addq.b  #1,(m_trg_off).l
    move.b  (A0,D1.w),D0
    beq.s   .silence_trg
.play_note_trg:
    bsr     emit_note_trg
    move.b  (m_trg_len).l,(m_trg_cnt).l
    bra     tick_noise
.silence_trg:
    bsr     key_off_trg
    move.b  (m_trg_len).l,(m_trg_cnt).l
    bra     tick_noise

;==============================================================================
; tick_noise — PSG noise channel (demo/ending/overworld only)
;==============================================================================
tick_noise:
    move.b  (m_song).l,D0
    andi.b  #$91,D0
    beq     .tn_done
    ; Always run the per-tick envelope decay step, regardless of whether a new
    ; drum hit fires this frame.  set_psg_noise will overwrite the envelope if
    ; a hit fires below.
    bsr     noise_decay_tick
    subq.b  #1,(m_noise_cnt).l
    bne     .tn_done
.read:
    move.l  (m_script_ptr).l,A0
    moveq   #0,D1
    move.b  (m_noise_off).l,D1
    addq.b  #1,(m_noise_off).l
    move.b  (A0,D1.w),D0
    bne.s   .got
    move.b  (m_noise_first).l,(m_noise_off).l
    bra.s   .read
.got:
    move.b  D0,D4                         ; preserve control note for length calc
    ; Length via GetSongNoiseNoteLength: rotate control note bits then AND #$07
    ; Original: ROR (through C), ROL, ROL, ROL, AND #$07, ADC NoteLengthTableBase
    ; Net effect: reorders to "low 3 bits of rotated" which is bits (2,1,0) ←
    ; (0,7,6). For our port we replicate exactly.
    moveq   #0,D2
    move.b  D4,D2
    ; We need bit0→2, bit7→1, bit6→0 of the original → into D0 bits 2,1,0.
    moveq   #0,D0
    btst    #0,D2
    beq.s   .b0
    bset    #2,D0
.b0:
    btst    #7,D2
    beq.s   .b7
    bset    #1,D0
.b7:
    btst    #6,D2
    beq.s   .b6
    bset    #0,D0
.b6:
    moveq   #0,D1
    move.b  (m_len_base).l,D1
    add.w   D0,D1
    lea     (NoteLengthTables).l,A1
    move.b  (A1,D1.w),(m_noise_cnt).l
    ; Extract index 0-3: (note AND #$3E) >> 4
    move.b  D4,D0
    andi.b  #$3E,D0
    lsr.b   #4,D0
    moveq   #0,D1
    move.b  D0,D1
    ; Map to PSG noise: set volume + period
    bsr     set_psg_noise
.tn_done:
    rts

;==============================================================================
; get_song_note_length — equivalent to GetSongNoteLength
; Input: D0 = control byte → Output: D0 = length
;==============================================================================
get_song_note_length:
    andi.w  #$07,D0
    moveq   #0,D1
    move.b  (m_len_base).l,D1
    add.w   D0,D1
    lea     (NoteLengthTables).l,A1
    move.b  (A1,D1.w),D0
    rts

;==============================================================================
; emit_note_sq1 — Play a note on YM ch 0 (Pulse 1 slot)
; Input: D0.b = note id (even byte offset into MusicNotePeriodTable)
;==============================================================================
emit_note_sq1:
    andi.w  #$FF,D0
    lea     (MusicNotePeriodTable).l,A1
    moveq   #0,D2
    move.b  1(A1,D0.w),D2                 ; lo
    beq     key_off_sq1                    ; lo=0 → rest → key off
    moveq   #0,D3
    move.b  0(A1,D0.w),D3                 ; hi
    lsl.w   #8,D3
    or.w    D2,D3                          ; D3.w = period
    andi.w  #$07FF,D3
    move.b  D2,(m_sq1_per).l              ; save low period for vibrato
    move.w  D3,D2
    move.l  #NES_YM_K5_PULSE,D3
    bsr     nes_to_ym_freq                ; D2=A4val, D3=A0val
    ; Key off first so the next key-on retriggers envelopes from attack
    move.b  #$28,D0
    moveq   #$00,D1                        ; ch 0, all ops off
    bsr     ym_write1
    move.b  #$A4,D0                        ; ch 0 freq high
    move.b  D2,D1
    bsr     ym_write1
    move.b  #$A0,D0                        ; ch 0 freq low
    move.b  D3,D1
    bsr     ym_write1
    ; Key on ch 0, all ops
    move.b  #$28,D0
    move.b  #$F0,D1
    bra     ym_write1

;==============================================================================
; emit_note_sq0 — Play a note on YM ch 1 (Pulse 2 slot)
;==============================================================================
emit_note_sq0:
    andi.w  #$FF,D0
    lea     (MusicNotePeriodTable).l,A1
    moveq   #0,D2
    move.b  1(A1,D0.w),D2
    beq     key_off_sq0
    moveq   #0,D3
    move.b  0(A1,D0.w),D3
    lsl.w   #8,D3
    or.w    D2,D3
    andi.w  #$07FF,D3
    move.b  D2,(m_sq0_per).l
    move.w  D3,D2
    move.l  #NES_YM_K5_PULSE,D3
    bsr     nes_to_ym_freq
    ; Key off first so the envelope attack phase retriggers
    move.b  #$28,D0
    moveq   #$01,D1                        ; ch 1 all ops off
    bsr     ym_write1
    move.b  #$A5,D0                        ; ch 1 freq high
    move.b  D2,D1
    bsr     ym_write1
    move.b  #$A1,D0                        ; ch 1 freq low
    move.b  D3,D1
    bsr     ym_write1
    move.b  #$28,D0
    move.b  #$F1,D1
    bra     ym_write1

;==============================================================================
; emit_note_trg — Play a note on YM ch 2 (triangle slot, octave lower)
;==============================================================================
emit_note_trg:
    andi.w  #$FF,D0
    lea     (MusicNotePeriodTable).l,A1
    moveq   #0,D2
    move.b  1(A1,D0.w),D2
    beq     key_off_trg
    moveq   #0,D3
    move.b  0(A1,D0.w),D3
    lsl.w   #8,D3
    or.w    D2,D3
    andi.w  #$07FF,D3
    move.b  D2,(m_trg_per).l
    move.w  D3,D2
    move.l  #NES_YM_K5_TRI,D3
    bsr     nes_to_ym_freq
    ; Key off first so the bass envelope retriggers per note
    move.b  #$28,D0
    moveq   #$02,D1                        ; ch 2 all ops off
    bsr     ym_write1
    move.b  #$A6,D0                        ; ch 2 freq high
    move.b  D2,D1
    bsr     ym_write1
    move.b  #$A2,D0                        ; ch 2 freq low
    move.b  D3,D1
    bsr     ym_write1
    move.b  #$28,D0
    move.b  #$F2,D1
    bra     ym_write1

;==============================================================================
; key_off_{sq1,sq0,trg} — silence the channel without reloading freq
;==============================================================================
key_off_sq1:
    move.b  #$28,D0
    move.b  #$00,D1
    bra     ym_write1
key_off_sq0:
    move.b  #$28,D0
    move.b  #$01,D1
    bra     ym_write1
key_off_trg:
    move.b  #$28,D0
    move.b  #$02,D1
    bra     ym_write1

;==============================================================================
; set_psg_noise — Trigger a drum hit on the PSG noise channel.
; Input: D1.w = drum index 0-3 (engine index, extracted from noise script byte)
;
; Pattern lifted from Kroc's Sonic 1 SMS disassembly (Z80 → M68K):
;   - Each drum has a (noise_mode, attack_level, decay_rate) entry in
;     PsgDrumTable.
;   - The noise control byte is ONLY written when it differs from the cached
;     last-written byte — writing it re-seeds the SN76489 LFSR which would
;     otherwise chop off the decay tail of an in-flight hit.
;   - Envelope level is reset to the attack_level; per-tick decay is done by
;     noise_decay_tick.
;   - Index 0 is treated as a rest: volume goes to mute but the noise control
;     latch is untouched so pending decays on adjacent channels are preserved.
;==============================================================================
native_psg_busy:
    move.l  A0,-(SP)
    movea.l (audio_native_ram_base).l,A0
    cmpa.l  #0,A0
    beq.s   .restore_native_base
    tst.b   $0606(A0)
.restore_native_base:
    movea.l (SP)+,A0                 ; MOVEA preserves the busy test flags
    rts

set_psg_noise:
    ; SFX arbitration: if Zelda's SFX engine has an active hit ($0606 != 0),
    ; skip music drum writes so the SFX owns PSG ch 3. When the SFX ends,
    ; music drums resume on the next beat.
    bsr     native_psg_busy
    bne.s   .sfx_owns_psg
    tst.b   D1
    bne.s   .not_rest
    ; Rest: silence the PSG noise channel immediately.
    clr.b   (m_noise_level).l
    clr.b   (m_noise_decay).l
    move.b  #$FF,(PSG_PORT).l              ; ch3 attenuation = max (mute)
    rts
.sfx_owns_psg:
    rts
.not_rest:
    moveq   #0,D0
    move.b  D1,D0
    lsl.w   #2,D0                          ; 4 bytes per drum entry
    lea     (PsgDrumTable).l,A1
    adda.l  D0,A1
    move.b  (A1)+,D0                       ; byte 0: noise control byte ($Ex)
    ; Write control byte only if different from cached value (preserves LFSR).
    cmp.b   (m_last_psg_nc).l,D0
    beq.s   .nc_same
    move.b  D0,(m_last_psg_nc).l
    move.b  D0,(PSG_PORT).l
.nc_same:
    move.b  (A1)+,D0                       ; byte 1: attack level ($00-$FF)
    move.b  D0,(m_noise_level).l
    move.b  (A1)+,D0                       ; byte 2: decay rate per tick
    move.b  D0,(m_noise_decay).l
    bra     write_psg_noise_vol

;==============================================================================
; write_psg_noise_vol — emit PSG ch3 attenuation from the software envelope.
; Uses m_noise_level (8-bit) → 4-bit PSG atten by shifting and inverting.
; Preserves D1.
;==============================================================================
write_psg_noise_vol:
    bsr     native_psg_busy                  ; SFX owns PSG?
    bne.s   .wpnv_skip
    moveq   #0,D0
    move.b  (m_noise_level).l,D0
    lsr.b   #4,D0                          ; level $00-$FF → 0-15
    eori.b  #$0F,D0                        ; invert: SN76489 0=loud, 15=silent
    ori.b   #$F0,D0                        ; ch3 attenuation latch
    move.b  D0,(PSG_PORT).l
.wpnv_skip:
    rts

;==============================================================================
; noise_decay_tick — linear envelope decay step.
; Called every music tick (before any new-hit check) to fade the current drum
; toward silence.  Underflow clamps to 0 so the envelope stops.
; Preserves D1.
;==============================================================================
noise_decay_tick:
    bsr     native_psg_busy                   ; SFX owns PSG?
    bne.s   .nd_done
    move.b  (m_noise_level).l,D0
    beq.s   .nd_done                        ; already silent
    sub.b   (m_noise_decay).l,D0
    bcc.s   .nd_store                       ; no underflow → new level
    moveq   #0,D0                           ; underflow → silent
.nd_store:
    move.b  D0,(m_noise_level).l
    bra     write_psg_noise_vol
.nd_done:
    rts

;==============================================================================
; music_silence — Full silence: key-off all channels, clear song state
;==============================================================================
    xdef    music_silence
music_silence:
    clr.b   (m_song).l
    bsr     key_off_sq1
    bsr     key_off_sq0
    bsr     key_off_trg
    move.b  #$FF,(PSG_PORT).l              ; PSG noise mute
    rts

;==============================================================================
; Music data tables
;==============================================================================
    even

; MusicNotePeriodTable — 114 bytes, big-endian 16-bit periods indexed by
; even note IDs (byte offsets).  Pulled from reference Z_00.asm.
; (Local copy to avoid label collision with the transpiled MusicNotePeriodTable
; in zelda_translated/z_00.asm, whose in-memory layout may have `even`
; padding that breaks offset math.)
MusicNotePeriodTable:
    dc.b    $00,$23,$00,$6A,$03,$27,$00,$97
    dc.b    $00,$00,$02,$F9,$02,$CF,$02,$A6
    dc.b    $02,$80,$02,$5C,$02,$3A,$02,$1A
    dc.b    $01,$FC,$01,$DF,$01,$C4,$01,$AB
    dc.b    $01,$93,$01,$7C,$01,$67,$01,$53
    dc.b    $01,$40,$01,$2E,$01,$1D,$01,$0D
    dc.b    $00,$FE,$00,$EF,$00,$E2,$00,$D5
    dc.b    $00,$C9,$00,$BE,$00,$B3,$00,$A9
    dc.b    $00,$A0,$00,$8E,$00,$86,$00,$77
    dc.b    $00,$7E,$00,$71,$00,$54,$00,$64
    dc.b    $00,$5F,$00,$59,$00,$50,$00,$47
    dc.b    $00,$43,$00,$3F,$00,$38,$00,$32
    dc.b    $00,$21,$05,$4D,$05,$01,$04,$B9
    dc.b    $04,$35,$03,$F8,$03,$BF,$03,$89
    dc.b    $03,$57
    even

; NoteLengthTables — 5x 8-byte tables concatenated (40 bytes).
NoteLengthTables:
    dc.b    $03,$0A,$01,$14,$05,$28,$3C,$70  ; table 0
    dc.b    $07,$1B,$35,$14,$0D,$28,$3C,$50  ; table 1
    dc.b    $06,$0C,$08,$18,$24,$30,$48,$10  ; table 2
    dc.b    $07,$0D,$09,$1B,$24,$36,$48,$10  ; table 3
    dc.b    $3C,$50,$0A,$05,$14,$0D,$28,$0E  ; table 4
    even

NoiseVolumesTbl:
    dc.b    $10,$1C,$1C,$1C
NoisePeriodsTbl:
    dc.b    $00,$03,$0A,$03            ; original NES $400E values (kept for ref)
NoiseLengthsTbl:
    dc.b    $00,$18,$18,$58

;----------------------------------------------------------------------
; PsgDrumTable — 4 drum slots × 4 bytes each (Kroc Sonic 1 SMS pattern).
; Layout per slot:
;   +0 noise control byte ($Ex): bit2=FB (1=white), bits 1-0=NF shift rate
;   +1 attack level ($00-$FF): starting software-envelope level
;   +2 decay rate   (per tick): envelope subtracted each music_tick
;   +3 reserved
;
; Index 0 is the "rest" slot but set_psg_noise short-circuits it, so its
; values are placeholders.
;----------------------------------------------------------------------
PsgDrumTable:
    dc.b    $E7, $00, $00, $00         ; 0: rest (handled by early-out)
    dc.b    $E4, $E0, $30, $00         ; 1: hi-hat  - white NF=00, ~5 frames (48 dec)
    dc.b    $E5, $FF, $0E, $00         ; 2: snare   - white NF=01, ~18 frames (14 dec)
    dc.b    $E4, $E0, $30, $00         ; 3: hi-hat alt (48 dec)
    even

;==============================================================================
; MusicBlob — SongTable + 9 song headers + 9 song scripts (2757 bytes)
; Pulled verbatim from PRG bank 0 at NES $8D60.
;==============================================================================
MusicBlob:
    incbin  "src/data/music_blob.dat"
    even

; DMC samples now ship as 14 kHz PCM C arrays in data/audio/sfx_pcm.c
; and play through SGDK's XGM Z80 driver. The legacy dmc_samples.inc
; include is gone; dmc_trigger trampolines through audio_sfx_play.
