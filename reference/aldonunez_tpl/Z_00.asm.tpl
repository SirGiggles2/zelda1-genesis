.INCLUDE "Variables.inc"

.SEGMENT "BANK_00_00"


.EXPORT DriveAudio

SongTable:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@

; Description:
; Each song phrase is described by a header in this format:
;
; 0: Note length table base
; 1: Song script address low
; 2: Song script address high
; 3: Triangle note offset in script
; 4: Square 0 note offset in script
; 5: Noise note offset in script
; 6: Envelope selector
; 7: TODO:
;
; Note that the last byte of the "Item taken" header overlaps
; "End level", and "End level" overlaps "Overworld".
;
SongHeaderDemo0:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@

; Unknown block
    .BYTE @@, @@, @@

SongHeaderItemTaken0:
    .BYTE @@, @@, @@, @@, @@, @@, @@

SongHeaderEndLevel0:
    .BYTE @@, @@, @@, @@, @@, @@, @@

SongHeaderOverworld0:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@

SongHeaderUnderworld0:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@

SongHeaderLastLevel0:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@

SongHeaderGanon0:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@

SongHeaderEnding0:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@

SongHeaderZelda:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@

SongScriptItemTaken0:
.INCBIN "dat/SongScriptItemTaken0.dat"

SongScriptOverworld0:
.INCBIN "dat/SongScriptOverworld0.dat"

SongScriptUnderworld0:
.INCBIN "dat/SongScriptUnderworld0.dat"

SongScriptEndLevel0:
.INCBIN "dat/SongScriptEndLevel0.dat"

SongScriptLastLevel0:
.INCBIN "dat/SongScriptLastLevel0.dat"

SongScriptGanon0:
.INCBIN "dat/SongScriptGanon0.dat"

SongScriptEnding0:
.INCBIN "dat/SongScriptEnding0.dat"

SongScriptDemo0:
.INCBIN "dat/SongScriptDemo0.dat"

SongScriptZelda0:
.INCBIN "dat/SongScriptZelda0.dat"

DriveAudio:
    ; If the game is paused, then silence all channels
    ; by first disabling them, then enabling them.
    ;
    ; Then go drive tune channel 0 only.
    ;
    LDA Paused
    BEQ @Play
    LDA #@@
    STA ApuStatus_4015
    LDA #@@
    STA ApuStatus_4015
    BNE @Paused
@Play:
    ; Else the game is not paused.
    ;
    ; Synchronize the APU frame counter once a video frame.
    ;
    LDA #@@
    STA Ctrl2_FrameCtr_4017
    ; Drive each game sound channel.
    ;
    JSR DriveTune1
    JSR DriveEffect
    JSR DriveSample
    JSR DriveSong
@Paused:
    JSR DriveTune0
    ; Reset all requests for sound.
    ;
    LDA #@@
    STA Tune0Request
    STA EffectRequest
    STA Tune1Request
    STA SampleRequest
    STA SongRequest
    RTS

TuneScripts0:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@

L18C9_SilenceSong:
    JMP SilenceSong

DriveTune0:
    LDA Tune0Request
    ; Tune $80 is only a signal to silence the song.
    ;
    BMI L18C9_SilenceSong
    BEQ @CheckCurrentTune
    ; If the requested tune is not "heart warning", then go play it.
    ;
    CMP #@@
    BNE @ChangeTune
    ; Else only play "heart warning" if nothing else is playing.
    ;
    LDX Tune0
    BEQ @ChangeTune
@CheckCurrentTune:
    LDA Tune0
    BNE @KeepPlaying
    RTS

@ChangeTune:
    STA Tune0
    LDY #@@
:
    ; Get the index for the song bit: 1 to 8.
    ;
    INY
    LSR
    BCC :-
    LDA TuneScripts0-1, Y
    STA TunePtr0
@KeepPlaying:
    LDY TunePtr0
    INC TunePtr0
    LDA TuneScripts0, Y
    BMI @PrepNote
    BNE @PlayNote
    ; We've reached the end of the tune.
    ;
    LDX #@@
    STX Sq0Duty_4000
    LDX #@@
    STX Sq0Length_4003
    LDX #@@
    STX Sq0Timer_4002
    STX Tune0
    RTS

@PrepNote:
    STA Sq0Duty_4000
    LDY TunePtr0
    INC TunePtr0
    LDA TuneScripts0, Y
@PlayNote:
    JSR EmitSquareNote0
    LDA #@@
    STA Sq0Sweep_4001
    RTS

SilenceSample:
    LDA #@@
    STA ApuStatus_4015
    LDA #@@
    STA Sample
    STA Tune1
    STA @@
    STA BackgroundSample
    RTS

PlayArrowSfx:
    STY Effect
    LDA #@@
    STA EffectCounter
    ; If "heart taken" is requested in tune channel 0, then cancel it.
    ;
    LDA Tune0Request
    AND #@@
    BNE ContinueArrowSfx
    STA Tune0Request
ContinueArrowSfx:
    LDY EffectCounter
    LDA ArrowSfxNotes-1, Y
    BNE PlaySfxNote
PlayStairsSfx:
    STY Effect
    LDA #@@
    STA EffectCounter
:
    LDA #@@
    STA @@                     ; The sound of one step lasts $C frames.
ContinueStairsSfx:
    DEC @@
    LDY @@
    BEQ :-
    CPY #@@
    BCC :+
    LDA #@@
    BNE SetSfxVolumeLength
:
    LDA StairsSfxNotes-1, Y
PlaySfxNote:
    TAX
    AND #@@
    STA NoisePeriod_400E
    TXA
    LSR
    LSR
    LSR
    LSR
    ORA #@@
SetSfxVolumeLength:
    STA NoiseVolume_400C
    LDA #@@
    STA NoiseLength_400F
    DEC EffectCounter
SilenceSfxIfEnded:
    BNE :+
    LDA #@@
    STA NoiseVolume_400C
    LDA #@@
    STA Effect
:
    RTS

PlaySwordSfx:
    STY Effect
    LDA #@@
    STA EffectCounter
ContinueSwordSfx:
    LDY EffectCounter
    LDA SwordSfxNotes-1, Y
    BNE PlaySfxNote
DriveEffect:
    LDY EffectRequest
    ; $80 is a signal to silence samples and tune 1.
    ;
    BMI SilenceSample
    LDA Effect
    LSR EffectRequest
    BCS PlaySwordSfx
    LSR
    BCS ContinueSwordSfx
    LSR EffectRequest
    BCS PlayArrowSfx
    LSR
    BCS ContinueArrowSfx
    LSR EffectRequest
    BCS @PlayFlameSfx
    LSR
    BCS @ContinueFlameSfx
    LSR EffectRequest
    BCS PlayStairsSfx
    LSR
    BCS ContinueStairsSfx
    LSR EffectRequest
    BCS @PlayBombSfx
    LSR
    BCS @ContinueBombSfx
    LSR
    BCS @ContinueSeaSfx
    LSR EffectRequest
    BCS @PlaySeaSfx
    RTS

@PlayBombSfx:
    STY Effect
    LDA #@@
    STA EffectCounter
@ContinueBombSfx:
    LDY EffectCounter
    LDA BombSfxNotes-1, Y
    BNE PlaySfxNote
@PlayFlameSfx:
    STY Effect
    LDA #@@
    STA EffectCounter
@ContinueFlameSfx:
    LDA EffectCounter
    LSR
    TAY
    LDX #@@
    STX NoisePeriod_400E
    LDA FlameSfxNotes-1, Y
    JMP SetSfxVolumeLength

@PlaySeaSfx:
    STY Effect
    LDA #@@
    STA SeaSfxCounter
    LDA #@@
    STA @@                     ; The volume begins and ends at $10.
@ContinueSeaSfx:
    LDA SeaSfxCounter
    CMP #@@
    BCC :+
    ; SFX counter >= $BF. Increase volume fast.
    ;
    INC @@
    BNE @SetSeaSfxParams
:
    ; SFX counter < $BF. Decrease volume slowly down to $10 --
    ; when (counter MOD 8) = 7.
    ;
    LDA SeaSfxCounter
    LSR
    BCC @SetSeaSfxParams
    LSR
    BCC @SetSeaSfxParams
    LSR
    BCC @SetSeaSfxParams
    LDA @@
    CMP #@@
    BEQ @SetSeaSfxParams
    DEC @@
@SetSeaSfxParams:
    LDA @@
    STA NoiseVolume_400C
    LDX #@@
    STX NoisePeriod_400E
    LDA #@@
    STA NoiseLength_400F
    DEC SeaSfxCounter
    JMP SilenceSfxIfEnded

BombSfxNotes:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@

TuneScripts1:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@

DriveTune1:
    LDA Tune1Request
    BMI @SilenceThenPlay
    BNE @ChangeTune
    LDA Tune1
    BNE @KeepPlaying
    RTS

@SilenceThenPlay:
    JSR SilenceSong
    LDA #@@
@ChangeTune:
    STA Tune1
    LDY #@@
:
    ; Get the index for the song bit: 1 to 8.
    ;
    INY
    LSR
    BCC :-
    LDA TuneScripts1-1, Y
    STA TunePtr1
    LDA #@@
    STA NoteCounterTune1
@KeepPlaying:
    DEC NoteCounterTune1
    BNE @CheckVibrate
    LDY TunePtr1
    INC TunePtr1
    LDA TuneScripts1, Y
    BMI @PrepNote
    BNE @PlayNote
    ; We've reached the end of the tune.
    ;
    ; If tune is "Game Over", then go play it again.
    ;
    LDA Tune1
    CMP #@@
    BEQ @ChangeTune
    LDX #@@
    STX Sq1Duty_4004
    LDX #@@
    STX Sq1Length_4007
    LDX #@@
    STX Tune1
    STX Sq1Timer_4006
    RTS

@PrepNote:
    AND #@@
    STA NoteLengthTune1
    LDY TunePtr1
    INC TunePtr1
    LDA TuneScripts1, Y
@PlayNote:
    JSR EmitSquareNote1
    LDA #@@
    STA Sq1Sweep_4005
    LDA #@@
    STA Sq1Duty_4004
    LDA NoteLengthTune1
    STA NoteCounterTune1
    LDA #@@
    STA CustomEnvelopeOffsetTune1
@CheckVibrate:
    ; If the tune is not one of "flute" or "Link dying", then return.
    ;
    LDA Tune1
    AND #@@
    BEQ @Exit
    LDY CustomEnvelopeOffsetTune1
    BEQ :+
    DEC CustomEnvelopeOffsetTune1
:
    LDA CustomEnvelopeTune1, Y
    STA Sq1Duty_4004
    LDA NoteCounterTune1
    LDX CurNoteLowPeriodSq1
    JSR VibratePitch
    STX Sq1Timer_4006
@Exit:
    RTS

CustomEnvelopeTune1:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@

DriveSample:
    LDA SampleRequest
    BMI @ChangeSampleMid
    BNE @ChangeSampleLow
    LDA Sample
    BEQ @CheckBgSample
    DEC SampleCounter
    BNE @Exit
    LDA Sample
    BMI @ChangeSampleMid
    AND #@@
    BNE @ChangeSampleLow
    LDA #@@
    STA Sample
    LDA #@@
    STA ApuStatus_4015
@CheckBgSample:
    LDA BackgroundSample
    BNE :+
@Exit:
    RTS

@ChangeSampleLow:
    LDX #@@
    BEQ :+
@ChangeSampleMid:
    LDX #@@
    AND #@@
:
    STX DmcCounter_4011
    STA Sample
    TAX
    AND #@@
    BEQ :+
    STA BackgroundSample
:
    TXA
    LDY #@@
:
    INY
    LSR
    BCC :-
    LDA SampleRates-1, Y
    STA DmcFreq_4010
    LDA SampleAddrs-1, Y
    STA DmcAddress_4012
    LDA SampleLengths-1, Y
    STA DmcLength_4013
    LDA #@@
    STA SampleCounter
    LDA #@@
    STA ApuStatus_4015
    LDA #@@
    STA ApuStatus_4015
    RTS

SampleAddrs:
    .BYTE @@, @@, @@, @@, @@, @@, @@

SampleLengths:
    .BYTE @@, @@, @@, @@, @@, @@, @@

SampleRates:
    .BYTE @@, @@, @@, @@, @@, @@, @@

; Params:
; X: duty byte
; Y: sweep byte
;
SetSq0DutyAndSweep:
    STY Sq0Sweep_4001
    STX Sq0Duty_4000
    RTS

EmitSquareNoteWithDutyAndSweep0:
    JSR SetSq0DutyAndSweep
; Params:
; A: offset of a note in period table
;
EmitSquareNote0:
    TAY
    LDA NotePeriodTable+1, Y
    BEQ Exit
    STA CurNoteLowPeriodSq0
    STA Sq0Timer_4002
    LDA NotePeriodTable, Y
    ORA #@@
    STA Sq0Length_4003
Exit:
    RTS

; Params:
; X: duty byte
; Y: sweep byte
;
SetSq1DutyAndSweep:
    STX Sq1Duty_4004
    STY Sq1Sweep_4005
    RTS

EmitSquareNoteWithDutyAndSweep1:
    JSR SetSq1DutyAndSweep
; Params:
; A: note ID (offset of note in period table)
;
; Returns:
; Y: note ID
; A: 0 if the note is a a rest
;
EmitSquareNote1:
    TAY
    LDA NotePeriodTable+1, Y
    BEQ Exit
    STA CurNoteLowPeriodSq1
    STA Sq1Timer_4006
    LDA NotePeriodTable, Y
    ORA #@@
    STA Sq1Length_4007
    RTS

; Params:
; A: offset of a note in period table
;
EmitTriangleNote:
    TAY
    LDA NotePeriodTable+1, Y
    BEQ Exit
    STA CurNoteLowPeriodTrg
    STA TrgTimer_400A
    LDA NotePeriodTable, Y
    ORA #@@
    STA TrgLength_400B
    RTS

; Params:
; A: note counter
; X: period value
;
; Returns:
; X: (period - 1) to (period + 1)
;
; If note counter < $10, then return.
;
VibratePitch:
    CMP #@@
    BCC @Exit
    ; Is bit 2 set?
    ;
    LSR
    LSR
    LSR
    BCS @GoDown
    ; If not set, then add 1 to X.
    ;
    TXA
    ADC #@@
    BNE :+
@GoDown:
    ; If set, then subtract 1 from X.
    ;
    TXA
    CLC
    ADC #@@
:
    TAX
@Exit:
    RTS

KeepPlaying:
    JMP KeepPlayingSong

DriveSong:
    LDA SongRequest
    BNE @ChangeSong
    LDA Song
    BNE KeepPlaying
    RTS

@ChangeSong:
    STA Song
    BMI @PlayFirstDemoPhrase
    CMP #@@
    BNE :+
    LDY #@@
    BNE PrepPhrase
:
    CMP #@@
    BEQ @PlayFirstOverworldPhrase
    CMP #@@
    BEQ @PlayFirstUnderworldPhrase
    CMP #@@
    BNE PlayNextPhrase
    LDY #@@
    BNE SetPrevPhraseIndex
@PlayFirstDemoPhrase:
    LDY #@@
    BNE SetPrevPhraseIndex
@PlayFirstUnderworldPhrase:
    LDY #@@
    BNE SetPrevPhraseIndex
@PlayFirstOverworldPhrase:
    LDY #@@
SetPrevPhraseIndex:
    STY SongPhraseIndex
PlayNextPhrase:
    TAX
    BMI @PlayNextDemoPhrase
    CMP #@@
    BEQ @PlayNextOverworldPhrase
    CMP #@@
    BEQ @PlayNextUnderworldPhrase
    CMP #@@
    BNE @PlaySinglePhraseSong
    ; Play the next phrase of the ending song.
    ;
    INC SongPhraseIndex
    LDY SongPhraseIndex
    CPY #@@
    BNE PrepPhrase
    LDY #@@
    BNE SetPrevPhraseIndex
@PlayNextUnderworldPhrase:
    INC SongPhraseIndex
    LDY SongPhraseIndex
    CPY #@@
    BNE PrepPhrase
    LDY #@@
    BNE SetPrevPhraseIndex
@PlayNextOverworldPhrase:
    INC SongPhraseIndex
    LDY SongPhraseIndex
    CPY #@@
    BNE PrepPhrase
    LDY #@@
    BNE SetPrevPhraseIndex
@PlayNextDemoPhrase:
    INC SongPhraseIndex
    LDY SongPhraseIndex
    CPY #@@
    BNE PrepPhrase
    LDY #@@
    BNE SetPrevPhraseIndex
@PlaySinglePhraseSong:
    ; Get the index for the song bit: 2 to 6.
    ;
    TXA
    LDY #@@
:
    INY
    LSR
    BCC :-
PrepPhrase:
    LDA SongTable-1, Y
    TAY
    LDA SongTable, Y
    STA NoteLengthTableBase
    LDA SongTable+1, Y
    STA SongScriptPtrLo
    LDA SongTable+2, Y
    STA SongScriptPtrHi
    LDA SongTable+3, Y
    STA NoteOffsetSongTrg
    LDA SongTable+4, Y
    STA NoteOffsetSongSq0
    LDA SongTable+5, Y
    STA NoteOffsetSongNoise
    STA FirstNoteIndexSongNoise
    LDA SongTable+6, Y
    STA SongEnvelopeSelector
    LDA SongTable+7, Y
    STA @@                   ; TODO: [05F1]
    LDA #@@
    STA NoteCounterSongSq1
    STA NoteCounterSongSq0
    STA NoteCounterSongTrg
    STA NoteCounterSongNoise
    LSR
    STA NoteOffsetSongSq1
KeepPlayingSong:
    DEC NoteCounterSongSq1
    BNE ApplySq1Effects
    LDY NoteOffsetSongSq1
    INC NoteOffsetSongSq1
    LDA (SongScriptPtrLo), Y
    BEQ @SongEnded
    BPL PlayNote
    BNE PrepNote
@SongEnded:
    ; If this is a song that repeats, then go play again.
    ;
    LDA Song
    AND #@@
    BNE PlayAgain
SilenceSong:
    LDA #@@
    STA Song
    STA ApuStatus_4015
    LDA #@@
    STA ApuStatus_4015
    RTS

PlayAgain:
    JMP PlayNextPhrase

PrepNote:
    JSR GetSongNoteLength
    STA NoteLengthSongSq1
    LDY NoteOffsetSongSq1
    INC NoteOffsetSongSq1
    LDA (SongScriptPtrLo), Y
PlayNote:
    ; If something is playing in tune channel 1, then
    ; don't play a square note here.
    ;
    LDX Tune1
    BNE @SkipSq1
    JSR EmitSquareNote1
    BEQ :+
    JSR PrepareCustomSongEnvelope
:
    STA CustomEnvelopeOffsetSongSq1
    JSR SetSq1DutyAndSweep
    LDA #@@
    STA SongVibrationCounterSq1
@SkipSq1:
    LDA NoteLengthSongSq1
    STA NoteCounterSongSq1
ApplySq1Effects:
    ; If something is playing in tune channel 1, then
    ; skip effects for square channel 1.
    ;
    LDY Tune1
    BNE @HandleSq0
    INC SongVibrationCounterSq1
    LDY CustomEnvelopeOffsetSongSq1
    BEQ :+
    DEC CustomEnvelopeOffsetSongSq1
:
    JSR ShapeSongVolume
    STA Sq1Duty_4004
    LDX #@@
    STX Sq1Sweep_4005
    LDA Song
    BPL @HandleSq0
    ; The demo/title song ($80) vibrates the pitch.
    ;
    LDA SongVibrationCounterSq1
    LDX CurNoteLowPeriodSq1
    JSR VibratePitch
    STX Sq1Timer_4006
@HandleSq0:
    LDY NoteOffsetSongSq0
    BEQ @HandleTrg
    DEC NoteCounterSongSq0
    BNE @ApplySq0Effects
    LDY NoteOffsetSongSq0
    INC NoteOffsetSongSq0
    LDA (SongScriptPtrLo), Y
    BPL @PlaySq0
    JSR GetSongNoteLength
    STA NoteLengthSongSq0
    LDY NoteOffsetSongSq0
    INC NoteOffsetSongSq0
    LDA (SongScriptPtrLo), Y
@PlaySq0:
    LDX Tune0
    BNE @SkipSq0
    JSR EmitSquareNote0
    BEQ :+
    JSR PrepareCustomSongEnvelope
:
    STA CustomEnvelopeOffsetSongSq0
    JSR SetSq0DutyAndSweep
    LDA #@@
    STA SongVibrationCounterSq0
@SkipSq0:
    LDA NoteLengthSongSq0
    STA NoteCounterSongSq0
@ApplySq0Effects:
    LDX Tune0
    BNE @HandleTrg
    INC SongVibrationCounterSq0
    LDY CustomEnvelopeOffsetSongSq0
    BEQ :+
    DEC CustomEnvelopeOffsetSongSq0
:
    JSR ShapeSongVolume
    STA Sq0Duty_4000
    LDA Song
    BPL :+
    ; The demo/title song ($80) vibrates the pitch.
    ;
    LDA SongVibrationCounterSq0
    LDX CurNoteLowPeriodSq0
    JSR VibratePitch
    STX Sq0Timer_4002
:
    LDA #@@
    STA Sq0Sweep_4001
@HandleTrg:
    LDA NoteOffsetSongTrg
    BNE :+
    JMP @HandleNoise

:
    DEC NoteCounterSongTrg
    BNE @ApplyTrgEffects
@PrepNoteOrPassage:
    LDY NoteOffsetSongTrg
    INC NoteOffsetSongTrg
    LDA (SongScriptPtrLo), Y
    BEQ @SetTrgLinear
    BPL @PlayNoteTrg
    CMP #@@
    BEQ @EndOfPassage
    BCC @PrepNoteTrg
    ; Control note >= $F1. The low nibble defines the number of
    ; repetititions of the passage starting at the next offset.
    ;
    SEC
    SBC #@@
    STA SongRepetitionsTrg
    LDA NoteOffsetSongTrg
    STA SongRepeatStartOffset
    JMP @PrepNoteOrPassage

@EndOfPassage:
    DEC SongRepetitionsTrg
    BEQ :+
    LDA SongRepeatStartOffset
    STA NoteOffsetSongTrg
:
    JMP @PrepNoteOrPassage

@PrepNoteTrg:
    JSR GetSongNoteLength
    STA NoteLengthSongTrg
    LDA #@@
    STA TrgLinear_4008
    LDY NoteOffsetSongTrg
    INC NoteOffsetSongTrg
    LDA (SongScriptPtrLo), Y
    BEQ @SetTrgLinear
@PlayNoteTrg:
    JSR EmitTriangleNote
    LDA #@@
    STA SongVibrationCounterTrg
    LDX NoteLengthSongTrg
    STX NoteCounterSongTrg
@ApplyTrgEffects:
    INC SongVibrationCounterTrg
    LDA SongVibrationCounterTrg
    LDX CurNoteLowPeriodTrg
    JSR VibratePitch
    STX TrgTimer_400A
    ; TODO: [05F1] ?
    ;
    LDA @@
    BPL :+
    LDA #@@
    BNE @SetTrgLinear
:
    LDA #@@
@SetTrgLinear:
    STA TrgLinear_4008
@HandleNoise:
    ; If the song is not demo nor ending, then return.
    ; They don't use noise.
    ;
    LDA Song
    AND #@@
    BEQ @Exit
    DEC NoteCounterSongNoise
    BNE @Exit
:
    LDY NoteOffsetSongNoise
    INC NoteOffsetSongNoise
    LDA (SongScriptPtrLo), Y
    BNE :+
    ; We've reached the end of the track. Noise always repeats.
    ;
    LDA FirstNoteIndexSongNoise
    STA NoteOffsetSongNoise
    BNE :-
:
    JSR GetSongNoiseNoteLength
    STA NoteCounterSongNoise
    ; From the original control note, extract an index 0-3
    ; to look up noise parameters.
    ;
    TXA
    AND #@@
    LSR
    LSR
    LSR
    LSR
    TAY
    LDA NoiseVolumes, Y
    STA NoiseVolume_400C
    LDA NoisePeriods, Y
    STA NoisePeriod_400E
    LDA NoiseLengths, Y
    STA NoiseLength_400F
@Exit:
    RTS

NoiseVolumes:
    .BYTE @@, @@, @@, @@

NoisePeriods:
    .BYTE @@, @@, @@, @@

NoiseLengths:
    .BYTE @@, @@, @@, @@

; Params:
; A: control note
;
; Returns:
; A: note length
; X: original control note
;
; Rotate so that bits 0, 7, 6 move to bits 2, 1, 0.
;
GetSongNoiseNoteLength:
    TAX
    ROR
    TXA
    ROL
    ROL
    ROL
; Params:
; A: control note
;
; Returns:
; A: note length
;
GetSongNoteLength:
    AND #@@
    CLC
    ADC NoteLengthTableBase
    TAY
    LDA NoteLengthTable0, Y
    RTS

GetSongNoteLengthWithAbsIndex:
    AND #@@
    TAY
    LDA NoteLengthTable0, Y
    RTS

; Unknown block
    .BYTE @@

StairsSfxNotes:
    .BYTE @@, @@, @@, @@, @@, @@

; Unknown block
    .BYTE @@

; Big-endian 16-bit period values.
;
NotePeriodTable:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@

; Returns:
; A: starting custom envelope offset
; X: duty byte $82
; Y: sweep byte $7F
;
PrepareCustomSongEnvelope:
    LDA SongEnvelopeSelector
    LDA #@@
    LDX #@@
    LDY #@@
    RTS

; Params:
; Y: envelope offset
;
; Returns:
; A: duty/volume value
;
ShapeSongVolume:
    LDA SongEnvelopeSelector
    BPL :+
    LDA CustomEnvelopeSong, Y
    AND #@@
    BNE @ReturnValue
:
    LDA CustomEnvelopeSong, Y
    LSR
    LSR
    LSR
    LSR
@ReturnValue:
    ORA #@@
    RTS

CustomEnvelopeSong:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@

SwordSfxNotes:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@

ArrowSfxNotes:
    .BYTE @@, @@, @@, @@, @@

FlameSfxNotes:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@

NoteLengthTable0:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@

NoteLengthTable1:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@

NoteLengthTable2:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@

NoteLengthTable3:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@

NoteLengthTable4:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@


.SEGMENT "BANK_00_ISR"



; Unknown block
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@


.SEGMENT "BANK_00_VEC"



; Unknown block
    .BYTE @@, @@, @@, @@, @@, @@

