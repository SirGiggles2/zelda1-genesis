.INCLUDE "Variables.inc"
.INCLUDE "BeginEndVars.inc"

.SEGMENT "BANK_02_00"


; Imports from RAM code bank 01

.IMPORT Anim_SetSpriteDescriptorAttributes
.IMPORT Anim_SetSpriteDescriptorRedPaletteRow
.IMPORT Anim_WriteItemSprites
.IMPORT Anim_WriteSpecificItemSprites
.IMPORT Anim_WriteSpritePairNotFlashing
.IMPORT BeginUpdateMode
.IMPORT DrawObjectMirrored
.IMPORT DrawObjectNotMirrored
.IMPORT FetchFileAAddressSet
.IMPORT FileBChecksums
.IMPORT FormatDecimalByte
.IMPORT FormatHeartsInTextBuf
.IMPORT Person_Draw
.IMPORT ResetRoomTileObjInfo
.IMPORT SilenceAllSound
.IMPORT UpdateWorldCurtainEffect_Bank2

; Imports from RAM code bank 06

.IMPORT MenuPalettesTransferBuf

; Imports from program bank 07

.IMPORT Anim_FetchObjPosForSpriteDescriptor
.IMPORT AnimateItemObject
.IMPORT EndGameMode
.IMPORT GoToNextMode
.IMPORT HideAllSprites
.IMPORT Link_EndMoveAndDraw
.IMPORT TableJump
.IMPORT TurnOffAllVideo
.IMPORT TurnOffVideoAndClearArtifacts

.EXPORT InitDemo_RunTasks
.EXPORT InitMode1_Full
.EXPORT InitMode13_Full
.EXPORT InitModeEandF_Full
.EXPORT TransferCommonPatterns
.EXPORT UpdateMode0Demo
.EXPORT UpdateMode13WinGame
.EXPORT UpdateMode1Menu
.EXPORT UpdateModeDSave
.EXPORT UpdateModeERegister
.EXPORT UpdateModeFElimination

CommonPatternBlockAddrs:
    .ADDR CommonSpritePatterns
    .ADDR CommonBackgroundPatterns
    .ADDR CommonMiscPatterns

CommonPatternBlockSizes:
    .DBYT @@
    .DBYT @@
    .DBYT @@

CommonPatternVramAddrs:
    .DBYT @@
    .DBYT @@
    .DBYT @@

TransferCommonPatterns:
    JSR TurnOffAllVideo
    LDA PpuStatus_2002          ; Clear address latch and scroll.
@LoopBlock:
    LDA PatternBlockIndex
    ASL
    TAX
    ; Put block address in [00:01] and size in [03:02].
    ; Load destination VRAM address and set it.
    ; The size and VRAM address have the high byte first.
    ;
    LDA CommonPatternBlockAddrs, X
    STA @@
    LDA CommonPatternBlockSizes, X
    STA @@
    LDA CommonPatternVramAddrs, X
    STA PpuAddr_2006
    INX
    LDA CommonPatternBlockAddrs, X
    STA @@
    LDA CommonPatternBlockSizes, X
    STA @@
    LDA CommonPatternVramAddrs, X
    JSR TransferPatternBlock_Bank2
    ; Loop until pattern block index = 3.
    ;
    LDA PatternBlockIndex
    CMP #@@
    BNE @LoopBlock
    LDA #@@                    ; Mark this block copied.
    STA TransferredCommonPatterns
    LDA #@@                    ; Reset pattern block index.
    STA PatternBlockIndex
    RTS

; Params:
; [00:01]: source address
; [03:02]: size
; A: low byte of desination VRAM address
;
TransferPatternBlock_Bank2:
    STA PpuAddr_2006
    LDY #@@
@Loop:
    ; Load and transfer 1 byte to VRAM.
    ;
    LDA (@@), Y
    STA PpuData_2007
    ; Increment the 16-bit source address at [00:01].
    ;
    LDA @@
    CLC
    ADC #@@
    STA @@
    LDA @@
    ADC #@@
    STA @@
    ; Decrement the 16-bit amount remaining in [03:02].
    ;
    LDA @@
    SEC
    SBC #@@
    STA @@
    LDA @@
    SBC #@@
    STA @@
    ; Loop until the amount remaining = 0.
    ;
    LDA @@
    BNE @Loop
    LDA @@
    BNE @Loop
    ; The block is done. Increment the block index.
    ;
    INC PatternBlockIndex
    RTS

CommonSpritePatterns:
.INCBIN "dat/CommonSpritePatterns.dat"

CommonBackgroundPatterns:
.INCBIN "dat/CommonBackgroundPatterns.dat"

CommonMiscPatterns:
.INCBIN "dat/CommonMiscPatterns.dat"

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
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@

InitDemo_RunTasks:
    JSR TurnOffAllVideo
    LDA DemoPhase
    BNE InitDemo_Phase1
    LDA DemoSubphase
    JSR TableJump
InitDemo_RunTasks_Phase0_JumpTable:
    .ADDR InitDemoSubphaseClearArtifacts
    .ADDR InitDemoSubphaseTransferTitlePalette
    .ADDR InitDemoSubphasePlayTitleSong

InitDemo_Phase1:
    LDA DemoSubphase
    JSR TableJump
InitDemo_RunTasks_Phase1_JumpTable:
    .ADDR InitDemoSubphaseClearArtifacts
    .ADDR InitDemoSubphaseTransferStoryPalette
    .ADDR InitDemoSubphaseTransferStoryTiles

UpdateMode0Demo:
    LDA GameSubmode
    BNE @HandleSubmodes         ; Only animate if submode = 0 and SkippedDemo = 0
    LDA SkippedDemo
    BNE @HandleSubmodes
    JSR AnimateDemo
    LDA IsUpdatingMode          ; If no longer updating,
    BEQ Exit                    ; then return.
@HandleSubmodes:
    LDA GameSubmode
    JSR TableJump
UpdateMode0Demo_JumpTable:
    .ADDR UpdateMode0Demo_Sub0
    .ADDR UpdateMode0Demo_Sub1
    .ADDR UpdateMode0Demo_Sub2

UpdateMode0Demo_Sub0:
    LDA ButtonsPressed          ; If Start is not pressed,
    AND #@@
    BEQ Exit                    ; then return.
    STA TransferredDemoPatterns ; Else, store $10, because it's convenient and not $A5.
    LDA #@@
    STA SongRequest
    JSR SilenceAllSound
    LDA #@@
    STA SkippedDemo
    INC GameSubmode             ; Go to next submode.
    JSR TurnOffAllVideo
    JSR HideAllSprites
    LDA #@@                    ; Cue transfer record for menu palettes.
    STA TileBufSelector
Exit:
    RTS

UpdateMode0Demo_Sub2:
    ; Copy data from each save file A to save slot info.
    ; Most of the game will deal with save slot info.
    ; Format each inactive file A, to make sure it's clear.
    ;
    JSR TurnOffAllVideo
    LDA #@@
    STA CurSaveSlot
    JSR FetchFileAAddressSet
    LDY #@@
@LoopFormatSlot:
    LDA (@@), Y
    STA IsSaveSlotActive, Y
    BNE @NextFormatSlot
    TYA                         ; Save the slot.
    PHA
    STY CurSaveSlot             ; Switch to this slot's addresses.
    JSR FetchFileAAddressSet
    JSR FormatFileA
    LDA #@@
    STA CurSaveSlot
    ; Fetch the address set for slot 0 again,
    ; so that we can keep referring to its
    ; IsSaveSlotActive address as a table base.
    ;
    ; After this, the slot is still not active, but it will
    ; definitely be clear.
    JSR FetchFileAAddressSet
    PLA
    TAY                         ; Restore the slot.
@NextFormatSlot:
    LDA (@@), Y                ; Copy death count from file A to save slot info.
    STA DeathCounts, Y
    LDA (@@), Y                ; Copy quest number from file A to save slot info.
    STA QuestNumbers, Y
    DEY
    BPL @LoopFormatSlot
    LDY #@@                    ; Point to hearts value
    LDX #@@                    ; 0: process hearts value; 1: process heart partial.
@LoopHeart:
    LDA (@@), Y                ; Load the hearts value or heart partial from Items block in file A.
    PHA                         ; Push either value.
    TXA
    LSR
    ; If X is even, then the value is a hearts value.
    ; So, make the hearts equal the heart containers.
    BCS @StoreValue
    ; Pop what we pushed, because we're going to push
    ; a modification.
    PLA
    AND #@@
    STA @@
    LSR
    LSR
    LSR
    LSR
    ORA @@
    PHA                         ; Push the full hearts value.
@StoreValue:
    PLA                         ; Pop whatever we pushed: heart partial or full hearts value
    STA SaveSlotHearts, X       ; Copy to hearts in save slot info.
    ; Point to the next byte in Items block.
    ; hearts value -> hearts partial
    INY
    INX                         ; Increment the index of the value we check.
    ; There are 6 values total:
    ; (hearts value, hearts partial) * 3 slots.
    CPX #@@
    BEQ @CopyNames              ; Once we finish the last value, quit the loop.
    TXA                         ; If X is odd, then go process heart partial instead of hearts value.
    LSR
    BCS @LoopHeart
    ; The 3 files are consecutive in the set.
    ; Point to the hearts value in the next slot.
    TYA
    ADC #@@
    TAY
    JMP @LoopHeart              ; Go process hearts in the next slot.

@CopyNames:
    LDY #@@                    ; Copy the name from file A to save slot info.
@LoopNameByte:
    LDA (@@), Y
    STA Names, Y
    DEY
    BPL @LoopNameByte
    INC GameMode                ; Go to the next game mode (Menu).
    LDA #@@                    ; We're set to initialize the mode.
    STA IsUpdatingMode
    STA GameSubmode
    RTS

AnimateDemo:
    LDA DemoPhase
    BNE AnimateDemo_Phase1
    LDA DemoSubphase
    JSR TableJump
AnimateDemo_Phase0_JumpTable:
    .ADDR AnimateDemoPhase0Subphase0
    .ADDR AnimateDemoPhase0Subphase1

AnimateDemo_Phase1:
    LDA DemoSubphase
    JSR TableJump
AnimateDemo_Phase1_JumpTable:
    .ADDR AnimateDemoPhase1Subphase0
    .ADDR AnimateDemoPhase1Subphase1
    .ADDR AnimateDemoPhase1Subphase2
    .ADDR AnimateDemoPhase1Subphase3
    .ADDR AnimateDemoPhase1Subphase4

InitialTitleSprites:
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

DemoLineAttrs:
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
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@

; Unknown block
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@

DemoLeftItemIds:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@

DemoRightItemIds:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@

DemoItemColumnX1:
    .BYTE @@

DemoItemColumnX2:
    .BYTE @@

DemoStoryFinalSpriteTiles:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@

DemoStoryFinalSpriteAttrs:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@

DemoTextFields:
.INCBIN "dat/DemoTextFields.dat"

DemoLineTextAddrs:
.INCLUDE "dat/DemoLineTextAddrs.inc"

InitDemoSubphaseClearArtifacts:
    JSR TurnOffVideoAndClearArtifacts
IncSubphase:
    INC DemoSubphase
    RTS

TitlePaletteTransferRecord:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@

InitDemoSubphaseTransferTitlePalette:
    LDX #@@                    ; Write a tile buf record for the title palette.
    STX @@
    STX DynTileBufLen
@CopyTitlePalette:
    LDA TitlePaletteTransferRecord, X
    STA DynTileBuf, X
    DEX
    BPL @CopyTitlePalette
    LDX #@@                    ; Reset variables used in this phase.
    LDA #@@
    STA DemoLineTextIndex
    STA DemoItemRow
@ClearVars:
    STA TriforceGlowTimer, X
    STA InitializedWaterfallAnimation, X
    STA DemoPhase0Subphase1Cycle, X
    DEX
    BPL @ClearVars
    LDX #@@                    ; Mark objects 1 to 10 disabled.
@DisableObjects:
    LDA #@@
    STA ObjState, X
    DEX
    BNE @DisableObjects
    JMP IncSubphase             ; Go advance the DemoSubphase and return.

InitDemoSubphasePlayTitleSong:
    LDA #@@                    ; Request the title song.
    STA SongRequest
    ; Select transfer buffer 8 (offset $10 in table):
    ; title nametables and attributes.
    LDA #@@
    JMP EndInitDemo

StoryPaletteTransferRecord:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@

InitDemoSubphaseTransferStoryPalette:
    LDX #@@                    ; Copy the transfer record for the story palette.
    STX @@
    STX DynTileBufLen
@CopyStoryPalette:
    LDA StoryPaletteTransferRecord, X
    STA DynTileBuf, X
    DEX
    BPL @CopyStoryPalette
    LDX #@@                    ; Reset variables used in this phase.
    LDA #@@
@ClearVars:
    STA @@, X
    STA @@, X
    STA @@, X
    STA @@, X
    DEX
    BPL @ClearVars
    JMP IncSubphase             ; Go to the next subphase and return.

InitDemoSubphaseTransferStoryTiles:
    INC SwitchNameTablesReq
    LDA #@@
    STA CurVScroll
    ; Select transfer buffer 1 (offset 2 in table):
    ; Story nametables and attributes.
    LDA #@@
EndInitDemo:
    STA TileBufSelector
    LDA #@@
    STA DemoSubphase
    INC IsUpdatingMode
    RTS

AnimateDemoPhase0Subphase0:
    ; Animate while we wait about 512 frames.
    ;
    LDA FrameCounter
    AND #@@                    ; The timer is incremented every other frame.
    BEQ @SkipTimer
    INC DemoTimer
    LDA DemoTimer
    BNE @SkipTimer
    JMP IncSubphase             ; Go advance the demo subphase and return.

@SkipTimer:
    JSR AnimateDemoPhase0Subphase0Artifacts
    RTS

AnimateDemoPhase1Subphase0:
    LDA FrameCounter            ; Increase CurVScroll every odd frame.
    AND #@@
    BEQ @CheckVScroll
    INC CurVScroll
    LDA CurVScroll              ; Have we scrolled to the bottom of nametable 2?
    CMP #@@
    BNE @CheckVScroll
    INC ScrolledScreenCount
    ; The bottom of NT 2 is also the top of NT 0.
    ; So, reset CurVScroll and the base NT.
    LDA #@@
    STA CurVScroll
    INC SwitchNameTablesReq
@CheckVScroll:
    ; Scrolling hasn't ended until we've scrolled to the bottom,
    ; wrapped around, and scrolled 8 more lines.
    LDA CurVScroll
    CMP #@@
    BNE @Exit
    LDA ScrolledScreenCount
    BEQ @Exit
    LDA #@@
    STA ScrolledScreenCount
    INC DemoSubphase
@Exit:
    RTS

AnimateDemoPhase1Subphase1:
    INC DemoTimer
    LDA DemoTimer
    BNE :+
    INC DemoSubphase            ; Go to the next subphase.
:
    LDA #@@
    STA DemoLineTileVramAddrHi
    LDA #@@
    STA DemoLineTileVramAddrLo
    LDA #@@
    STA DemoLineAttrVramAddrHi
    LDA #@@
    STA DemoLineAttrVramAddrLo
    RTS

AnimateDemoPhase1Subphase2:
    JSR HideAllSprites
    JSR DisableFallenObjects
    JSR Demo_AnimateObjects
    LDA FrameCounter
    AND #@@
    BEQ @Exit                   ; Every even frame, just return.
    LDX #@@                    ; Decrement the Y coordinate of every object.
@DecObjYs:
    DEC ObjY, X
    DEX
    BNE @DecObjYs
    INC ScrolledLineCount       ; We scrolled one more line.
    LDA ScrolledLineCount
    BNE :+                      ; Once we've scrolled a whole screen,
    INC ScrolledScreenCount     ; Increment the screen count.
:
    LDA ScrolledScreenCount     ; Have we scrolled 5 screens?
    CMP #@@
    BNE @Scroll                 ; Scroll the nametable.
    LDA ScrolledLineCount       ; Scroll half a screen more.
    CMP #@@
    BNE @Scroll                 ; Scroll the nametable.
    INC DemoSubphase            ; Go to the next subphase.
@Exit:
    RTS

@Scroll:
    INC CurVScroll              ; Update vertical scrolling.
    LDA CurVScroll
    CMP #@@
    BNE @CheckLine              ; If we reached the bottom of the screen,
    INC SwitchNameTablesReq     ; Switch the base nametable and reset scroll.
    LDA #@@
    STA CurVScroll
@CheckLine:
    LDA ScrolledLineCount       ; 7/8 of the lines,
    AND #@@
    BNE @Exit                   ; just return.
    JSR ProcessDemoLineItems    ; But every 8 lines, we have to check for new text and objects.
    ; Append a request to transfer a line.
    ; It's blank by default. Change it later.
    LDX #@@
    LDA #@@
    STA DynTileBuf+3, X
    DEX
@ClearLine:
    LDA #@@
    STA DynTileBuf+3, X
    DEX
    BPL @ClearLine
    LDA #@@
    STA DynTileBuf+2
    LDA DemoLineTileVramAddrHi
    STA DynTileBuf
    LDA DemoLineTileVramAddrLo
    STA DynTileBuf+1
    CLC                         ; The next line is 32 bytes farther.
    ADC #@@
    STA DemoLineTileVramAddrLo
    BNE @CheckNTEnd             ; If crossed a page,
    INC DemoLineTileVramAddrHi  ; then increment high address byte,
    JMP @CheckText              ; and do the next task.

@CheckNTEnd:
    ; Check if we reached the end of a nametable.
    ; If we did, then set the address to the top of the other one.
    ; $2BC0 -> $2000
    ; $23C0 -> $2800
    CMP #@@
    BNE @CheckText
    LDA DemoLineTileVramAddrHi
    CMP #@@
    BNE @CheckNT0
    LDA #@@
    STA DemoLineTileVramAddrHi
    JMP @ResetVramLo

@CheckNT0:
    CMP #@@
    BNE @CheckText
    LDA #@@
    STA DemoLineTileVramAddrHi
@ResetVramLo:
    LDA #@@
    STA DemoLineTileVramAddrLo
@CheckText:
    ; Check text and NT attributes.
    ;
    LDX DemoLineIndex
    LDA DemoLineAttrs, X
    AND #@@
    BEQ @ProcessAttrs           ; If attribute $80 isn't set, then leave the line blank.
    LDA DemoLineTextIndex
    ASL
    TAX
    LDY #@@
    LDA DemoLineTextAddrs, X    ; Get the address of the current text field.
    STA @@
    LDA DemoLineTextAddrs+1, X
    STA @@
    LDA (@@), Y                ; The first byte of text field is the offset into the line.
    TAX
@CopyLine:
    ; X is an offset into the destination line.
    ; Y is an offset into the source tiles of the current record.
    ;
    ; Get the next source tile.
    INY
    LDA (@@), Y
    CMP #@@
    BEQ @EndLine                ; When you reach the end marker, quit.
    STA DynTileBuf+3, X         ; Copy to the tile buf.
    INX
    JMP @CopyLine

@EndLine:
    INC DemoLineTextIndex
@ProcessAttrs:
    ; Finished processing attribute $80.
    ;
    JSR ProcessDemoLineAttrs
    INC DemoLineIndex           ; Advance to the next line.
    RTS

ProcessDemoLineAttrs:
    LDX DemoLineIndex
    LDA DemoLineAttrs, X
    AND #@@
    BEQ @Exit                   ; If attribute $40 is missing, then return.
    ; Append a second transfer record.
    ; This one is for nametable attributes.
    LDA DemoLineAttrVramAddrHi
    STA DynTileBuf+35
    LDA DemoLineAttrVramAddrLo
    STA DynTileBuf+36
    LDA #@@                    ; 8 zeroes in VRAM take up 1 byte in record.
    STA DynTileBuf+37
    LDA #@@
    STA DynTileBuf+38
    LDA #@@
    STA DynTileBuf+39
    INC @@                   ; UNKNOWN: It doesn't seem to be used.
    LDA DemoLineAttrVramAddrLo  ; The next attributes go 8 bytes farther.
    CLC
    ADC #@@
    STA DemoLineAttrVramAddrLo
    ; Check if we reached the end of nametable attributes.
    ; If we did, then set the address to the top of the other one.
    ; $2C00 -> $23C0
    ; $2400 -> $2BC0
    BNE @Exit
    LDA DemoLineAttrVramAddrHi
    CMP #@@
    BNE @SetNT0
    LDA #@@
    STA DemoLineAttrVramAddrHi
    JMP @SetVramLo

@SetNT0:
    LDA #@@
    STA DemoLineAttrVramAddrHi
@SetVramLo:
    LDA #@@
    STA DemoLineAttrVramAddrLo
@Exit:
    RTS

DisableFallenObjects:
    ; For objects 1..10, indexed by X:
    ;
    LDX #@@
@Loop:
    ; Once an object has fallen off the top of the screen,
    ; disable it.
    LDA ObjY, X
    ; The Y coordinate where we consider an object
    ; completely off the screen is $F0.
    CMP #@@
    BNE @Next
    LDA #@@                    ; Set the corresponding state to disabled.
    STA ObjState, X
@Next:
    DEX
    BNE @Loop
    RTS

ProcessDemoLineItems:
    LDY DemoLineIndex
    LDA DemoLineAttrs, Y
    AND #@@                    ; If attribute $20 is present, then instantiate a new object.
    BNE @MakeObject
    RTS

@MakeObject:
    LDX #@@                    ; Look for the first disabled object.
@FindDisabled:
    LDA ObjState, X
    BNE @SetUpObject
    DEX
    JMP @FindDisabled

@SetUpObject:
    LDY DemoItemRow
    LDA DemoLeftItemIds, Y      ; Allocate an object for the item on the left.
    STA DemoItemIds, X
    LDA #@@                    ; Start at the bottom of the screen.
    STA ObjY, X
    LDA DemoItemColumnX1
    STA ObjX, X
    LDA #@@
    STA ObjState, X
    LDA DemoItemIds, X
    CMP #@@                    ; Link gets a special item ID.
    BCS @CenterLink             ; Go center the object horizontally, if it is Link.
    DEX                         ; Allocate another object for the item on the right.
    LDA DemoRightItemIds, Y
    STA DemoItemIds, X
    LDA #@@
    STA ObjY, X
    LDA DemoItemColumnX2
    STA ObjX, X
    LDA #@@
    STA ObjState, X
    LDA DemoLeftItemIds, Y
    CMP #@@
    BNE @IncRow
    ; Special case: The triforce must be centered.
    ; But it shows up in both column lists.
    ; Two objects were instantiated, but they'll overlap.
    LDA #@@
    STA ObjX, X
    STA ObjX+1, X
    LDA #@@
    STA @@                   ; UNKNOWN: It doesn't seem to be used.
@IncRow:
    INC DemoItemRow
    RTS

@CenterLink:
    LDA #@@
    STA ObjX, X
    JMP @IncRow

Demo_AnimateObjects:
    ; For each object 1..10, indexed by X:
    ;
    LDX #@@
@LoopObject:
    LDA ObjState, X
    BNE @NextObject             ; If the object is disabled, then skip it.
    TXA                         ; Save the index.
    PHA
    ; Animate the fairy specially.
    ;
    LDA DemoItemIds, X
    CMP #@@
    BNE @AnimateNormal
    JSR AnimateStationaryFairy
    JMP @PopAndNextObject

@AnimateNormal:
    ; Animate normal items (type < $30).
    ;
    CMP #@@
    BCS @AnimateFinal
    JSR AnimateItemObject
    JMP @PopAndNextObject

@AnimateFinal:
    ; At the end of the list of items are special items for Link
    ; and the sheet of paper.
    ;
    JSR AnimateDemoStoryFinalItems
@PopAndNextObject:
    PLA                         ; Restore the index.
    TAX
@NextObject:
    DEX
    BNE @LoopObject
    RTS

; Unknown block
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@

AnimateStationaryFairy:
    JSR Anim_FetchObjPosForSpriteDescriptor
    JSR Anim_SetSpriteDescriptorRedPaletteRow
    ; Rely on the fact that 2 represents normal sprite
    ; attributes with palette 6.
    ; Shift the value to make it 4.
    ASL
    AND FrameCounter            ; Every 4 frames, switch between 2 animation frames.
    LSR
    LSR
    STA @@                     ; Put frame in [$0C].
    LDY #@@
    JMP Anim_WriteItemSprites

AnimateDemoStoryFinalItems:
    ; The demo story ending objects use special item types.
    ; The bottom nibble of the item type is an index into two
    ; tables. Each row has 6 bytes, one for each sprite of the
    ; item type.
    ;
    LDA DemoItemIds, X
    AND #@@
    ASL
    STA @@
    ASL
    CLC
    ADC @@
    TAY                         ; Y gets offset of each row: (low_nibble & $0F) * 6
    LDA ObjY, X                 ; Put ObjY,ObjX in [$00, $01].
    STA @@
    LDA ObjX, X
    STA @@
    LDA #@@
    STA @@                     ; For 6 sprites, counted by [$02]:
    TYA
    ASL
    ASL
    TAX                         ; X gets the offset to the sprite (index * 4).
@LoopSprite:
    LDA DemoStoryFinalSpriteTiles, Y    ; From this table, get the tile.
    BEQ @SkipSprite             ; If it's zero, skip the sprite.
    STA Sprites+1, X
    LDA @@                     ; Write sprite Y.
    STA Sprites, X              ; From this table, get the attributes.
    LDA DemoStoryFinalSpriteAttrs, Y
    STA Sprites+2, X
    LDA @@                     ; Write sprite X.
    STA Sprites+3, X
    INX                         ; Point to the next sprite.
    INX
    INX
    INX
@SkipSprite:
    LDA @@                     ; Add 8 to X coordinate.
    CLC
    ADC #@@
    STA @@
    INY                         ; Increment the sprite index.
    DEC @@                     ; Decrement count.
    BPL @LoopSprite
    RTS

AnimateDemoPhase1Subphase3:
    INC DemoTimer               ; Delay about 256 frames.
    LDA DemoTimer
    BNE AnimateDemoPhase1End_AnimateObjects
    INC DemoSubphase
    RTS

AnimateDemoPhase1Subphase4:
    INC DemoTimer               ; Delay 56 frames.
    LDA DemoTimer
    CMP #@@
    BNE AnimateDemoPhase1End_AnimateObjects
    LDA #@@                    ; Go to phase 0 again, and initialize it.
    STA IsUpdatingMode
    STA DemoTimer
    STA DemoPhase
    STA DemoSubphase
    RTS

AnimateDemoPhase1End_AnimateObjects:
    JSR HideAllSprites
    JSR Demo_AnimateObjects
    RTS

TriforcePaletteTransferRecord:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@

TriforceGlowingColors:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@

AnimateDemoPhase0Subphase0Artifacts:
    ; Set up sprites for the title.
    ; Copy initial sprite data to Sprites area.
    LDY #@@
@CopySprites:
    LDA InitialTitleSprites-1, Y
    STA Sprites-1, Y
    DEY
    BNE @CopySprites
    JSR UpdateWaterfallAnimation
    ; When you reach the end of the glow cycle,
    ; Append a transfer record for the triforce palette.
    ;
    LDA TriforceGlowTimer
    BNE @DecGlowTimer
    LDY #@@
@CopyTriforcePalette:
    LDA TriforcePaletteTransferRecord, Y
    STA DynTileBuf, Y
    DEY
    BPL @CopyTriforcePalette
    ; Patch the palette record with the color
    ; for the current point in the cycle.
    LDY TriforceGlowCycle
    LDA TriforceGlowingColors, Y
    STA DynTileBuf+5
    LDA #@@                    ; Restart the glow timer.
    STA TriforceGlowTimer
    INC TriforceGlowCycle       ; Advance the glow cycle.
    LDA TriforceGlowCycle
    CMP #@@                    ; When the glow cycle finishes,
    BNE @DecGlowTimer
    LDA #@@                    ; delay twice as long for one step of the cycle.
    STA TriforceGlowTimer
    LDA #@@                    ; Restart the glow cycle.
    STA TriforceGlowCycle
@DecGlowTimer:
    DEC TriforceGlowTimer
    RTS

WaterfallWaveTiles:
    .BYTE @@, @@, @@, @@

WaterfallCrestTiles:
    .BYTE @@, @@, @@, @@

WaterfallSpriteXs:
    .BYTE @@, @@, @@, @@

WaterfallWaveSpriteOffsets:
    .BYTE @@, @@, @@

; Unknown block
    .BYTE @@, @@, @@, @@, @@

UpdateWaterfallAnimation:
    LDA InitializedWaterfallAnimation
    BNE @UpdateSprites
    LDA #@@                    ; Initialize animation values.
    STA TitleWaveYs+0
    LDA #@@
    STA TitleWaveYs+1
    LDA #@@
    STA TitleWaveYs+2
    ; UNKNOWN:
    ; These don't seem to be used in the demo.
    ; Maybe the waterfall used to be bigger?
    ;
    LDA #@@
    STA TitleWaveYs+3
    LDA #@@
    STA TitleWaveYs+4
    LDA #@@
    STA TitleWaveYs+5
    INC InitializedWaterfallAnimation
@UpdateSprites:
    LDX #@@
:
    JSR UpdateSpritesForWaterfallWave
    DEX
    BPL :-
    JSR UpdateSpritesForWaterfallCrest
    RTS

UpdateSpritesForWaterfallWave:
    ; Add 2 to current waterfall wave Y.
    ; But keep it in the range $B2..$E3.
    ;
    INC TitleWaveYs, X
    INC TitleWaveYs, X
    LDA TitleWaveYs, X
    CMP #@@
    BCC :+
    LDA #@@
    STA TitleWaveYs, X
:
    STA @@                     ; Keep a copy of the new value in [$05].
    ; Depending on the Y coordinate of the wave,
    ; modify the animation state.
    ;
    ; < $B9, use tile offset 0
    ; < $C2, use tile offset 8
    ; else, use tile offset $10
    TAY
    LDA #@@
    CPY #@@
    BCS @SetOffset
    LSR
    CPY #@@
    BCS @SetOffset
    LDA #@@
@SetOffset:
    STA @@
    STX @@                     ; Keep a copy of the wave index in [$02].
    LDY WaterfallWaveSpriteOffsets, X    ; Y gets offset of first sprite in current wave.
    LDX #@@                    ; For each sprite (4) in current wave, indexed by X:
@LoopSprite:
    LDA WaterfallWaveTiles, X   ; Get base tile for current sprite.
    CLC
    ADC @@                     ; Modify the tile according to the current state of the wave.
    STA Sprites+1, Y
    LDA WaterfallSpriteXs, X    ; Set sprite X to each part of wave in a row.
    STA Sprites+3, Y
    LDA @@                     ; Set sprite Y to rolling value.
    STA Sprites, Y
    LDA #@@                    ; Set sprite attributes: normal with palette 4.
    STA Sprites+2, Y
    INY                         ; Advance to the next sprite.
    INY
    INY
    INY
    DEX
    BPL @LoopSprite
    LDX @@                     ; Put the wave index in X again.
    RTS

UpdateSpritesForWaterfallCrest:
    LDX #@@                    ; For each sprite (4) in crest, indexed by X:
    LDY #@@
@LoopSprite:
    ; Every 16 frames, switch between
    ; the two frames of animation.
    LDA FrameCounter
    AND #@@
    CLC
    ADC WaterfallCrestTiles, X
    STA Sprites+1, Y
    LDA #@@                    ; The Y coordinate is fixed in place.
    STA Sprites, Y
    LDA WaterfallSpriteXs, X    ; Set sprite X to each part of crest in a row.
    STA Sprites+3, Y
    LDA #@@                    ; Set sprite attributes: normal with palette 4.
    STA Sprites+2, Y
    INY                         ; Advance to the next sprite.
    INY
    INY
    INY
    DEX
    BPL @LoopSprite
    RTS

DemoPhase0Subphase1Palettes:
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
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@

; Unknown block
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@

DemoPhase0Subphase1Delays:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@

; Unknown block
    .BYTE @@, @@

AnimateDemoPhase0Subphase1:
    LDA DemoPhase0Subphase1Timer    ; When subphase timer expires,
    BNE @UpdateAnimation        ; go update animation only.
    ; Calculate the address to the palette to transfer.
    ; Addr = DemoPhase0Subphase1Palettes + (DemoPhase0Subphase1Palettes * $20)
    LDA #@@
    STA @@
    LDA DemoPhase0Subphase1Cycle
    ASL
    ASL
    ASL
    ASL
    ROL @@
    ASL
    ROL @@
    ADC #<DemoPhase0Subphase1Palettes
    STA @@
    LDA @@
    ADC #>DemoPhase0Subphase1Palettes
    STA @@
    LDA #@@                    ; Set up the header of the transfer record.
    STA DynTileBuf
    LDA #@@
    STA DynTileBuf+1
    LDA #@@
    STA DynTileBuf+2
    LDY #@@                    ; Put an end marker at the end.
    LDA #@@
    STA DynTileBuf+4, Y
@CopyPalette:
    LDA (@@), Y                ; Copy the chosen palette into the transfer record.
    STA DynTileBuf+3, Y
    DEY
    BPL @CopyPalette
    ; Advance the subphase cycle.
    ;
    INC DemoPhase0Subphase1Cycle
    ; Set the timer to the delay for the current point in the cycle.
    ;
    LDY DemoPhase0Subphase1Cycle
    LDA DemoPhase0Subphase1Delays-1, Y
    STA DemoPhase0Subphase1Timer
    CPY #@@
    BCC @UpdateAnimation        ; If we reached the end of the cycle,
    ; Advance to the next demo phase.
    ;
    INC DemoPhase
    LDA #@@                    ; Reset the demo subphase.
    STA DemoSubphase
    STA IsUpdatingMode          ; We have to initialize the new demo phase.
@UpdateAnimation:
    DEC DemoPhase0Subphase1Timer
    JSR UpdateWaterfallAnimation
    RTS

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
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@

SaveFileBAddressSets:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@

SaveFileBAddressSet1:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@

SaveFileBAddressSet2:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@

; TODO: [C8:C9]
;
; Returns:
; [C0:C1]: items pointer
; [C2:C3]: world flags pointer
; [C4:C5]: name pointer
; [C6:C7]: IsSaveSlotActive pointer
; [C8:C9]:
; [CA:CB]: death count pointer
; [CC:CD]: quest pointer
;
FetchFileBAddressSet:
    LDA #@@                    ; Calculate the end of the address set for current file.
    LDY CurSaveSlot
@AddE:
    CLC
    ADC #@@
    DEY
    BPL @AddE
    TAY
    ; Copy the file address set (14 bytes) for the current
    ; save file to [$C0] to make it easier to work with.
    LDX #@@
@CopyAddresses:
    LDA SaveFileBAddressSets, Y
    STA @@, X
    DEY
    DEX
    BPL @CopyAddresses
    RTS

ModeFTitleTransferBuf:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@

ModeFTitlePatchRegister:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@

ModeFSaveSlotTemplatePatchRegister:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@

ModeFSaveSlotTemplateTransferBuf:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@

ModeEandFSlotCursorYs:
    .BYTE @@, @@, @@, @@

ModeE_CharMap:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@

SlotToInitialNameCharTransferHeaders:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@

DeletedSlotBlankNameTransferBuf:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@

ModeEandFCursorSprites:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@

ModeE_CharBoardYOffsetsAndBounds:
    .BYTE @@, @@, @@, @@, @@, @@

SlotToBlankNameTransferBufEndOffset:
    .BYTE @@, @@, @@

SlotToNameOffset:
    .BYTE @@, @@, @@

SlotToInitialNameCharTransferHeaderEndOffsets:
    .BYTE @@, @@, @@

InitModeEandF_Full:
    LDA #@@
    STA CurSaveSlot
    JSR ModeE_ResetVariables
    JSR TurnOffAllVideo
    LDA GameSubmode
    BNE @CheckSub1              ; Go handle submodes 1 and up.
    ; Submode 0:
    ;
    JSR TurnOffVideoAndClearArtifacts
@FormatSlotsB:
    ; Format all file B's.
    ;
    JSR FetchFileBAddressSet
    JSR FormatFileB
    INC CurSaveSlot
    LDA CurSaveSlot
    CMP #@@
    BNE @FormatSlotsB
    LDA #@@                    ; Reset CurSaveSlot.
    STA CurSaveSlot
    LDX #@@                    ; Copy the title tiles to the dynamic transfer buf.
@CopyTiles:
    LDA ModeFTitleTransferBuf, X
    STA DynTileBuf, X
    DEX
    BPL @CopyTiles
    LDA GameMode
    CMP #@@
    BNE @SetBufLen              ; If in mode E, overwrite "ELIMINATION MODE" with "REGISTER YOUR NAME".
    LDY #@@
@OverwriteTitle:
    LDA ModeFTitlePatchRegister, Y
    STA DynTileBuf+7, Y
    INY
    CPY #@@
    BNE @OverwriteTitle
@SetBufLen:
    LDA #@@                    ; Record the length of the transfer buf.
@SetTransferBufLenAndIncSubmode:
    STA DynTileBufLen
    INC GameSubmode
    RTS

@CheckSub1:
    CMP #@@
    BNE @CheckSub2
    ; Submode 1:
    ;
    ; Copy ModeFSaveSlotTemplateTransferBuf to dynamic transfer buf.
    LDX #@@
@CopySlotTemplates:
    LDA ModeFSaveSlotTemplateTransferBuf, X
    STA DynTileBuf, X
    DEX
    BPL @CopySlotTemplates
    LDX #@@                    ; Overwrite payload of a dynamic transfer record with a name.
    LDY #@@
@OverwriteName:
    LDA Names, Y                ; The names are all arranged one after another.
    STA DynTileBuf+3, X
    INX
    INY
    TYA
    AND #@@
    BNE @OverwriteName          ; If copied the whole name,
    INX                         ; Skip the header for the next transfer record.
    INX
    INX
    CPX #@@
    BNE @OverwriteName          ; If there are more names to write, go write the next one.
    LDA GameMode
    CMP #@@
    BNE @SkipOverwriteEndOption ; If in mode E, overwrite "ELIMINATION" with "REGISTER".
    LDY #@@
@OverwriteEndOption:
    LDA ModeFSaveSlotTemplatePatchRegister, Y
    STA DynTileBuf+3, X
    INX
    INY
    CPY #@@
    BNE @OverwriteEndOption
@SkipOverwriteEndOption:
    LDA #@@
    BNE @SetTransferBufLenAndIncSubmode    ; Go record the length of the transfer buf, and advance the submode.
@CheckSub2:
    CMP #@@
    BNE @CheckSub3              ; Go handle submode 3 and 4.
    ; Submode 2:
    ;
    ; Cue a transfer of ModeFCharBoardTransferBuf.
    LDA #@@
@SelectMenuBuf:
    STA TileBufSelector
    INC GameSubmode
    RTS

@CheckSub3:
    ; Submode 3:
    ;
    CMP #@@
    BNE @Sub4                   ; Go handle submode 4.
    LDA #@@                    ; In mode $E, use $15 for cursor color.
    LDY GameMode
    CPY #@@
    BNE :+
    LDA #@@                    ; In mode $F, use $30 for cursor color.
:
    ; Replace byte 1 of row 3 of sprite palette in transfer buf
    ; TileBufSelector=$12.
    STA MenuPalettesTransferBuf+32
    LDA #@@
    BNE @SelectMenuBuf          ; Go cue transfer of menu palettes and advance submode.
@Sub4:
    ; Submode 4:
    ;
    LDA GameMode
    CMP #@@
    BEQ @FoundInactiveSlot      ; If in mode E,
    LDX #@@                    ; Then look for the first slot that's inactive.
    LDY #@@
    STY CurSaveSlot
@FindInactiveSlot:
    INY
    INC CurSaveSlot
    LDA IsSaveSlotActive, Y
    BEQ @FoundInactiveSlot      ; Found one. Quit the loop.
    DEX
    BPL @FindInactiveSlot       ; Loop again. If not found, then CurSaveSlot will be 3 (out of bounds).
@FoundInactiveSlot:
    JSR ModeEandF_SetUpCursorSprites
    LDA CurSaveSlot
    CMP #@@
    BNE :+                      ; If we're at the "end" option, then hide the char-board cursor.
    LDA #@@
    STA Sprites+8
:
    LDA #@@                    ; The X of Link objects is $50.
    STA @@
    LDA #@@                    ; The base Y of Link objects is $30.
    STA @@
    INC IsUpdatingMode
    JMP Mode1_WriteLinkSprites

ZeldaString:
    .BYTE @@, @@, @@, @@, @@

UpdateModeERegister:
    LDA ButtonsPressed          ; If didn't press Start,
    AND #@@
    BEQ @GoIdle
    LDA CurSaveSlot             ; or didn't select "End" option,
    CMP #@@
    BEQ @ChoseEnd
@GoIdle:
    JMP @Idle                   ; then go handle idle time.

@ChoseEnd:
    ; Pressed Start over "End" option.
    ;
    ; Silence tune channel 1.
    LDA #@@
    STA Tune1
    STA @@                   ; Reset SaveFileNameIndex.
    STA @@                   ; Reset SaveSlotNameIndex.
    STA CurSaveSlot             ; Reset CurSaveSlot.
    TAX                         ; X holds the current slot number
@LoopSaveSlot:
    LDY CurSaveSlot
    LDA #@@                    ; Mark save file B committed.
    STA IsSaveFileBCommitted, Y
    TYA
    ASL
    TAY
    LDA #@@                    ; Reset FileBReadyToSave [$0426].
    STA @@
    STA FileBChecksums, Y       ; Reset checksum for current save file B.
    INY
    STA FileBChecksums, Y
    TXA                         ; Save current save slot number.
    PHA
    JSR FetchFileBAddressSet
    PLA                         ; Restore save slot number.
    TAX
@CopyName:
    LDY @@                   ; Copy next character from save slot info to file B.
    LDA Names, Y
    LDY @@                   ; SaveFileNameIndex
    STA (@@), Y
    CMP #@@
    BEQ @NextChar               ; If we copied a space, then go advance offsets and check things.
    LDA IsSaveSlotActive, X
    BNE @NextChar               ; If the save slot is active, then go advance offsets and check things.
    ; The save slot is not active.
    ;
    ; Initialize file B hearts value to 3 heart containers and 2 hearts.
    LDY #@@
    LDA #@@
    STA (@@), Y
    INY                         ; Initialize file B heart partial to full.
    LDA #@@
    STA (@@), Y
    LDY #@@                    ; Initialize file B's max bombs to 8.
    LDA #@@
    STA (@@), Y
    TXA                         ; Save the current save slot number.
    PHA
    ASL                         ; Multiply it by 8 to get offset to current name in save slot info.
    ASL
    ASL
    TAY
    LDX #@@                    ; Compare the name to "ZELDA".
@CompareToZelda:
    LDA Names, Y
    CMP ZeldaString, X
    BNE @FlagBReady             ; If there's any mismatch, then skip the rest.
    INY
    INX
    CPX #@@
    BCC @CompareToZelda         ; Go check the next character until the end of "ZELDA".
    PLA                         ; Pop and push the slot number, so we can get it into X.
    PHA
    TAX
    LDY #@@                    ; Set second quest in file B.
    LDA #@@
    STA (@@), Y
@FlagBReady:
    PLA                         ; Restore save slot number.
    TAX
    LDA #@@                    ; Set FileBReadyToSave [$0426].
    STA @@
    LDY #@@                    ; Set IsSaveSlotActive in file B.
    STA (@@), Y
@NextChar:
    INC @@                   ; Point to the next char in save slot info name.
    INC @@                   ; Point to the next char in save file B name.
    LDA @@
    CMP #@@
    BNE @CopyName               ; If we haven't copied 8 characters from save slot info name, then go copy the next one.
    INX                         ; Make X refer to the next slot.
    LDA #@@                    ; Reset the offset to the next save file B char.
    STA @@                   ; SaveFileNameIndex
    LDA @@
    BEQ :+                      ; If FileBReadyToSave [$0426] is set, then calculate and store the file B checksum, and mark file B uncommitted.
    JSR CalculateAndStoreFileBChecksumUncommitted
:
    INC CurSaveSlot             ; Advance the slot number.
    LDA CurSaveSlot
    CMP #@@
    BEQ :+                      ; If haven't processed 3 slots,
    JMP @LoopSaveSlot           ; then go process the next one.

:
    LDA #@@                    ; Reset FileBReadyToSave [$0426].
    STA @@
    STA CurSaveSlot             ; Reset CurSaveSlot.
    JSR ModeE_ResetVariables
    LDA #@@                    ; Make sure we stay updating.
    STA IsUpdatingMode
    JMP UpdateModeDSave_Sub2    ; Go to mode 0 submode 1.

@Idle:
    ; Handle idle time in mode $E.
    ;
    LDA CurSaveSlot
    CMP #@@
    BEQ :+                      ; If a slot is chosen,
    JSR ModeE_HandleDirections  ; then we can check the direction buttons.
:
    JSR UpdateModeEandF_Idle
    JSR ModeEandF_WriteNameCursorSpritePosition
    JSR ModeEandF_WriteCharBoardCursorSpritePosition
    JMP ModeE_HandleAOrB

UpdateModeFElimination:
    LDA ButtonsPressed
    CMP #@@
    BEQ :+                      ; If Start wasn't pressed,
    JMP UpdateModeEandF_Idle    ; Then go handle other buttons and idle time.

:
    ; Start was pressed.
    ;
    LDA CurSaveSlot
    CMP #@@
    BNE DeleteSlot              ; If a slot was chosen, then go delete it.
    LDA #@@                    ; "End" was chosen. So, go to mode $E.
    STA GameMode
    LDA #@@
    STA IsUpdatingMode
    STA GameSubmode
ModeE_ResetVariables:
    ; Assumes that zero is passed in A.
    ;
    STA CharBoardIndex
    STA InitializedNameField
    STA NameCharOffset
    RTS

DeleteSlot:
    LDA #@@                    ; "Hurt" sound effect
    STA SampleRequest
    LDY CurSaveSlot
    LDX SlotToBlankNameTransferBufEndOffset, Y
    ; Copy the appropriate transfer buf of a blank name for
    ; current slot to dynamic transfer buf.
    LDY #@@
@CopyBlankBuf:
    LDA DeletedSlotBlankNameTransferBuf, X
    STA DynTileBuf, Y
    DEX
    DEY
    BPL @CopyBlankBuf
    JSR FetchFileAAddressSet
    JSR FormatFileA
    JSR FetchProfileNameAddress
    LDY #@@                    ; Clear the name in save slot info.
@ClearName:
    LDA #@@
    STA (@@), Y
    DEY
    BPL @ClearName
    RTS

ModeE_HandleDirections:
    LDA ButtonsDown
    AND #@@                    ; Filter and keep direction buttons.
    BNE ModeE_HandleDirectionButton    ; If no button is down, then reset repeat state.
ResetButtonRepeatState:
    STA StillHoldingButton
    STA SubsequentButtonRepeat
    STA ButtonRepeatTimer
    RTS

ModeE_HandleDirectionButton:
    TAY
    LDA StillHoldingButton
    BNE @CheckSameButton        ; If we weren't holding a button last frame,
    STY HeldButton              ; Store current buttons down.
    INC StillHoldingButton      ; Now we definitely are holding a button.
@CheckSameButton:
    LDA ButtonsDown
    AND #@@
    CMP HeldButton
    BEQ @CheckRepeat            ; If it's not the same button as before,
    LDA #@@                    ; then reset repeat state.
    JSR ResetButtonRepeatState
@CheckRepeat:
    LDA ButtonRepeatTimer
    BEQ @ChooseRepeatDelay      ; Once the repeat timer reaches zero, handle the direction button again.
    DEC ButtonRepeatTimer       ; Otherwise, only count down the timer.
    RTS

@ChooseRepeatDelay:
    ; If this is the first button press, then wait $10 frames
    ; to repeat; otherwise wait 8 frames.
    LDY #@@
    LDA SubsequentButtonRepeat
    BNE :+
    LDY #@@
:
    STY ButtonRepeatTimer
    LDA ButtonsDown
    AND #@@
    CMP #@@
    BNE @Left
    ; Pressed Right.
    ;
    ; Increase CharBoardIndex [$041F] to put cursor at character to the right.
    ;
    INC CharBoardIndex
    LDA ObjX+1                  ; Move the char board cursor right one spot.
    CLC
    ADC #@@
    STA ObjX+1
    CMP #@@
    BNE :+                      ; If still on the board, then go finish.
    LDA #@@                    ; Otherwise, move the cursor to the left end.
    STA ObjX+1
    LDX #@@                    ; Cycle down.
    JSR CycleCharBoardCursorY
    LDA ModeE_WrappedAroundBoardY
    BEQ :+                      ; If wrapped around to the top,
    LDA #@@                    ; then reset CharBoardIndex.
    STA CharBoardIndex
:
    JMP @FinishInput            ; Go finish.

@Left:
    CMP #@@
    BNE @Down
    ; Pressed Left.
    ;
    ; Decrease CharBoardIndex [$041F] to put cursor at character to the left.
    ;
    DEC CharBoardIndex
    LDA ObjX+1                  ; Move the char board cursor left one spot.
    SEC
    SBC #@@
    STA ObjX+1
    CMP #@@
    BNE :+                      ; If still on the board, then go finish.
    LDA #@@                    ; Otherwise, move the cursor to the right end.
    STA ObjX+1
    LDX #@@                    ; Cycle up.
    JSR CycleCharBoardCursorY
    LDA ModeE_WrappedAroundBoardY
    BEQ :+                      ; If wrapped around to the bottom,
    LDA #@@                    ; then set CharBoardIndex to the last index.
    STA CharBoardIndex
:
    JMP @FinishInput            ; Go finish.

@Down:
    CMP #@@
    BNE @Up
    ; Pressed Down.
    ;
    ; Increase CharBoardIndex [$041F] by $B (one row down).
    ;
    LDA CharBoardIndex
    CLC
    ADC #@@
    STA CharBoardIndex
    LDX #@@                    ; Cycle down.
    JSR CycleCharBoardCursorY
    LDA ModeE_WrappedAroundBoardY
    BEQ @Finish                 ; If didn't wrap around, then go finish.
    LDA CharBoardIndex          ; Wrapped around. So, move to top row.
    SEC
    SBC #@@
    STA CharBoardIndex
@Finish:
    JMP @FinishInput            ; Go finish.

@Up:
    CMP #@@
    BNE @Exit                   ; If no single direction was pressed, then return.
    ; Pressed Up.
    ;
    ; Decrease CharBoardIndex [$041F] by $B (one row up).
    ;
    LDA CharBoardIndex
    SEC
    SBC #@@
    STA CharBoardIndex
    LDX #@@                    ; Cycle up.
    JSR CycleCharBoardCursorY
    LDA ModeE_WrappedAroundBoardY
    BEQ @FinishInput            ; If didn't wrap around, then go finish.
    LDA CharBoardIndex          ; Wrapped around. So, move to bottom row.
    CLC
    ADC #@@
    STA CharBoardIndex
@FinishInput:
    LDA #@@
    STA SubsequentButtonRepeat
    STA Tune1Request            ; Request "selection changed" tune (same as rupee taken).
@Exit:
    RTS

; Params:
; X: 0 for down, 3 for up.
;
; Returns:
; ModeE_WrappedAroundBoardY [$042A]=1 if wrapped arounnd.
;
; Assume we don't wrap around.
CycleCharBoardCursorY:
    LDY #@@
    LDA ObjY+1                  ; Move the char board cursor Y one spot in given direction.
    CLC
    ADC ModeE_CharBoardYOffsetsAndBounds, X    ; Add $10 or -$10 ($F0), depending on X passed in (0 or 3).
    STA ObjY+1
    INX                         ; Look at boundaries.
    CMP ModeE_CharBoardYOffsetsAndBounds, X
    BNE @ReturnValue            ; If we didn't reach the boundary, then return Y=0.
    INX                         ; Set cursor Y to wrapped around position.
    LDA ModeE_CharBoardYOffsetsAndBounds, X
    STA ObjY+1
    INY                         ; Return ModeE_WrappedAroundBoardY [$042A]=1.
@ReturnValue:
    STY ModeE_WrappedAroundBoardY
LA10A_Exit:
    RTS

ModeE_HandleAOrB:
    LDA InitializedNameField
    ; If InitializedNameField [$0420] is set, then skip initializing
    ; the name field.
    BNE @CheckAB
    LDY CurSaveSlot
    CPY #@@
    BEQ LA10A_Exit              ; If at the "End" option, then return.
    ; Set NameCharOffset [$0421] to the offset of first char
    ; in the current slot's name.
    LDA SlotToNameOffset, Y
    STA NameCharOffset
    ; Get the offset of the end of the initial name character
    ; transfer record header for the current slot.
    LDX SlotToInitialNameCharTransferHeaderEndOffsets, Y
    LDY #@@                    ; Each transfer record header is 3 bytes.
@CopyHeaderTemplate:
    LDA SlotToInitialNameCharTransferHeaders, X
    STA NameInputCharBuf, Y     ; Copy a byte of transfer header for current slot to [$0422][Y].
    DEX
    DEY
    BPL @CopyHeaderTemplate
    INC InitializedNameField    ; Set InitializedNameField [$0420] to mark the name field initialized.
@CheckAB:
    ; At this point:
    ; - NameCharOffset [$0421] holds the offset of the first character in the save slot info name for the current slot.
    ;   - This will be changed as the player inputs characters.
    ; - [$0422] to [$0424] hold a transfer record header. The VRAM address points to the beginning of the appropriate name field in the nametable.
    ;   - This will be changed as the player inputs characters.
    ; - InitializedNameField [$0420] is set to 1.
    ;
    LDA ButtonsPressed
    AND #@@
    BEQ @Exit                   ; If neither A nor B was pressed, then go finish.
    ; A or B was pressed.
    ;
    CMP #@@
    BNE @MoveCursor             ; If B was pressed, then go move the name cursor only.
    ; A was pressed.
    ;
    ; Request to play the character click tune (same as bomb set).
    LDY #@@
    STY Tune0Request
    LDY #@@                    ; Copy our char transfer record header (in [$0422-0424]) to dynamic transfer buf.
@CopyHeader:
    LDA NameInputCharBuf, Y
    STA DynTileBuf, Y
    DEY
    BPL @CopyHeader
    STY DynTileBuf+4            ; Write the end marker to dynamic transfer buf.
    LDX NameCharOffset
    LDY CharBoardIndex          ; CharBoardIndex in [$041F] will index into character map.
    LDA ModeE_CharMap, Y        ; Get the character that's highlighted.
    STA DynTileBuf+3            ; Write the chosen character to dynamic transfer buf.
    STA Names, X                ; Set the character in the name.
@MoveCursor:
    LDA ObjX                    ; Move name cursor right 8 pixels.
    CLC
    ADC #@@
    STA ObjX
    INC NameCharOffset          ; Increment NameCharOffset [$0421].
    INC NameInputCharBuf+1      ; Increment the low VRAM address where next char will go.
    ; If VRAM address still points inside the name field,
    ; then go finish.
    ;
    ; The idea is that each name field in nametable begins at
    ; an address ending in $E. For example, slot 0 has a name
    ; at VRAM addresses $20CE to $20D5.
    ;
    ; Once the $E becomes a 6, we've gone past the end of
    ; the name field.
    ;
    ; Keep in mind that [0423] is the second byte of the transfer
    ; record header.
    ;
    LDA NameInputCharBuf+1
    AND #@@
    CMP #@@
    BNE @Exit
    ; The VRAM address now points outside the name field.
    ; So, wrap around to the beginning of the name field.
    ;
    ; For example, $20D6 -> $20CE.
    ;
    LDA NameInputCharBuf+1
    SEC
    SBC #@@
    STA NameInputCharBuf+1
    ; It also means that we went past the end of the save slot
    ; info name. Set the offset to the beginning of the name.
    LDY CurSaveSlot
    LDA SlotToNameOffset, Y
    STA NameCharOffset
    ; If the name cursor has gone past the end of the field,
    ; then wrap around.
    LDA ObjX
    CMP #@@
    BNE @Exit
    LDA #@@
    STA ObjX
@Exit:
    JMP ModeE_SetNameCursorSpriteX    ; Go set the name cursor sprite X.

ModeEandF_SetUpCursorSprites:
    ; Copy almost 3 sprite records ($B bytes) to byte 1 of
    ; Sprites block. Only sprite 0 byte 0 is missing.
    ; These are the cursor sprites.
    LDY #@@
@CopySprites:
    LDA ModeEandFCursorSprites, Y
    STA Sprites+1, Y
    DEY
    BPL @CopySprites
    LDY CurSaveSlot             ; Set the Y of the slot cursor sprite (#0) according to current save slot.
    LDA ModeEandFSlotCursorYs, Y
    STA ObjY
    STA Sprites
    LDA GameMode
    CMP #@@
    ; There's more work in mode $E.
    ; The name cursor position is held in ObjX/ObjY[0].
    ; The char-board cursor position is held in ObjX/ObjY[1].
    BEQ @Exit
    LDA #@@                    ; Use a heart tile for the slot cursor sprite (#0).
    STA Sprites+1
    ; Because the visible part of the cursor block sprite is
    ; in the bottom, move the name cursor's sprite 8 pixels
    ; above its ObjY.
    LDA ObjY
    SEC
    SBC #@@
    STA Sprites+4
    LDA #@@                    ; The base name cursor X is $70.
    STA ObjX
    LDA #@@                    ; The base char-board cursor Y is $87.
    STA ObjY+1
    LDA #@@                    ; The base char-board cursor X is $30.
    STA ObjX+1
@Exit:
    RTS

ModeEandF_WriteNameCursorSpritePosition:
    LDA ObjY
    CMP #@@                    ; If name cursor Y corresponds to a save slot,
    BNE @WriteCursorY           ; then go write the appropriate Y to name cursor sprite.
    LDA #@@                    ; else hide name cursor.
    STA Sprites+4
    RTS

@WriteCursorY:
    LDA ObjY
    JSR ModifyFlashingCursorY   ; This returns the adjusted coordinate or $F8 to hide it.
    STY Sprites+4               ; Set name cursor sprite Y.
ModeE_SetNameCursorSpriteX:
    LDA ObjX
    STA Sprites+7               ; Set name cursor sprite X.
    RTS

ModeEandF_WriteCharBoardCursorSpritePosition:
    LDA ObjY
    CMP #@@                    ; If name cursor Y corresponds to a save slot,
    BNE @WriteCursorCoords      ; then go write the appropriate Y to char-board sprite.
    LDA #@@                    ; else hide char-board sprite.
    STA Sprites+8
    RTS

@WriteCursorCoords:
    LDA ObjY+1
    JSR ModifyFlashingCursorY
    STY Sprites+8               ; Set char-board cursor sprite Y.
    LDA ObjX+1
    STA Sprites+11              ; Set char-board cursor sprite X.
    RTS

; Params:
; A: cursor Y
;
; Returns:
; Y: adjusted cursor Y, or $F8 to hide it
;
; Description:
; Adjust the Y coordinate to account for the visible part of
; block cursor being in the bottom half of sprite.
; Also, make the cursor flash.
;
ModifyFlashingCursorY:
    SEC
    SBC #@@
    TAY
    LDA FrameCounter            ; Every 8 frames, put the Y off screen.
    AND #@@
    BNE :+
    LDY #@@
:
    RTS

UpdateModeEandF_Idle:
    LDA ButtonsPressed
    AND #@@
    BEQ @Exit                   ; If Select was not pressed, then return.
@ChangeSelection:
    LDA #@@                    ; Request to play the selection tune (same as rupee taken).
    STA Tune1Request
    INC CurSaveSlot             ; Choose the next slot.
    LDY CurSaveSlot
    CPY #@@
    BNE :+                      ; If went out of bounds, then wrap around.
    LDY #@@
    STY CurSaveSlot
:
    LDA ModeEandFSlotCursorYs, Y    ; Set sprite Y for new selection.
    STA Sprites
    LDA GameMode
    CMP #@@
    BEQ @Exit                   ; If in mode $F, then return.
    LDA ObjY                    ; Move name cursor down $18 pixels.
    CLC
    ADC #@@
    STA ObjY
    CMP #@@                    ; If we're past the "End" option,
    BNE :+
    LDA #@@                    ; then wrap around.
    STA ObjY
:
    STA Sprites                 ; It seems that this should be Sprites+4.
    LDA #@@                    ; Name cursor X is $70.
    STA Sprites+7
    STA ObjX
    LDA #@@                    ; Reset InitializedNameField and NameCharOffset.
    STA InitializedNameField
    STA NameCharOffset
    LDY CurSaveSlot
    CPY #@@
    BEQ @Exit                   ; If selection is "End" option, then return.
    LDA IsSaveSlotActive, Y
    BNE @ChangeSelection        ; If the slot is not active, then cycle again.
@Exit:
    RTS

Mode1SlotLineTransferBuf:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@

Mode1DeathCountsTransferBuf:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@

LinkColors:
    .BYTE @@, @@, @@

InitMode1_Full:
    JSR TurnOffAllVideo
    LDA GameSubmode
    JSR TableJump
InitMode1_Full_JumpTable:
    .ADDR UpdateMode0Demo_Sub1
    .ADDR InitMode1_Sub1
    .ADDR InitMode1_Sub2
    .ADDR InitMode1_FillAndTransferSlotTiles
    .ADDR InitMode1_FillAndTransferSlotTiles
    .ADDR InitMode1_FillAndTransferSlotTiles
    .ADDR InitMode1_Sub6

UpdateMode0Demo_Sub1:
    ; For each save slot:
    ;   If file B is uncommitted and valid, then
    ;     Copy file B to file A (also marks file B committed)
    ;   If file A's static markers are wrong or file is invalid, then
    ;     Format file A
    ;
    ; TODO: This is also mode 1 submode 0 init.
    ;
    JSR TurnOffAllVideo
    LDA #@@                    ; Reset CurSaveSlot.
    STA CurSaveSlot             ; For every save file B (3):
@LoopSlot:
    LDY CurSaveSlot
    LDA IsSaveFileBCommitted, Y
    BNE @CheckFileA
    JSR FetchFileBAddressSet
    JSR CalculateFileBChecksum
    LDA CurSaveSlot
    ASL
    TAY
    LDA FileBChecksums, Y       ; Does the checksum match?
    CMP @@
    BNE @CheckFileA
    INY
    LDA FileBChecksums, Y
    CMP @@
    BNE @CheckFileA             ; If not, then go check save file A.
    JSR CopyFileBToFileA
    JMP @NextSlot               ; Go process the next slot.

@CheckFileA:
    ; Calculate the checksum of the save file.
    ; It's only a sum.
    JSR FetchFileAAddressSet
    JSR CalculateFileAChecksum
    LDY CurSaveSlot
    LDA SaveFileOpenMarkers, Y  ; Are the static markers intact?
    CMP #@@
    BNE @FormatA
    LDA SaveFileCloseMarkers, Y
    CMP #@@
    BNE @FormatA                ; If not, then go format the file.
    LDA CurSaveSlot
    ASL
    TAY
    LDA FileAChecksums, Y       ; Does the checksum match?
    CMP @@
    BNE @FormatA
    INY
    LDA FileAChecksums, Y
    CMP @@
    BEQ @NextSlot               ; If not, then format the file.
@FormatA:
    JSR FetchFileAAddressSet
    JSR FormatFileA
@NextSlot:
    INC CurSaveSlot             ; Advance to the next save slot.
    LDA CurSaveSlot
    CMP #@@
    BNE @LoopSlot
    INC GameSubmode             ; After checking every file, go to the next submode.
    RTS

; Returns:
; [0F:0E]: checksum
;
CalculateFileAChecksum:
    LDA #@@                    ; Reset the sum.
    STA @@
    STA @@
    LDY #@@                    ; Sum the name (8 bytes).
@SumName:
    LDA (@@), Y
    JSR AddATo0F0E
    DEY
    BPL @SumName
    LDY #@@                    ; Add the Items block ($28 bytes) to [$0F:0E].
@SumItems:
    LDA (@@), Y
    JSR AddATo0F0E
    DEY
    BPL @SumItems
    LDA #@@                    ; Will count $180 with [01:00].
    STA @@
    LDA #@@
    STA @@
    LDY #@@                    ; Add World Flags ($180 bytes) to [$0F:0E].
@SumWorldFlags:
    LDA (@@), Y
    JSR AddATo0F0E
    INC @@
    BNE :+
    INC @@
:
    DEC @@
    BNE @SumWorldFlags
    DEC @@
    LDA @@
    BPL @SumWorldFlags
    LDA (@@), Y                ; IsSaveSlotActive
    JSR AddATo0F0E
    LDA (@@), Y                ; TODO: Add byte at [[$08:09]] to [$0F:0E].
    JSR AddATo0F0E
    LDA (@@), Y                ; DeathCount
    JSR AddATo0F0E
    LDA (@@), Y                ; QuestNumber
AddATo0F0E:
    CLC
    ADC @@
    STA @@
    LDA @@
    ADC #@@
    STA @@
    RTS

FormatFileA:
    LDY #@@
    LDA #@@                    ; Make the name 8 spaces.
@ClearName:
    STA (@@), Y
    DEY
    BPL @ClearName
    LDY #@@                    ; Reset the file's Items block.
    LDA #@@
@ClearItems:
    STA (@@), Y
    DEY
    BPL @ClearItems
    LDA #@@                    ; Will count $180 with [01:00].
    STA @@
    LDA #@@
    STA @@
    LDY #@@                    ; Reset World Flags ($180 bytes) at [$02:03].
@ClearWorldFlags:
    LDA #@@
    STA (@@), Y
    INC @@
    BNE :+
    INC @@
:
    DEC @@
    BNE @ClearWorldFlags
    DEC @@
    LDA @@
    BPL @ClearWorldFlags
    LDA #@@
    STA (@@), Y                ; Reset the file's IsSaveSlotActive.
    STA (@@), Y                ; TODO: Reset byte at [[$08:09]].
    STA (@@), Y                ; Reset the file's DeathCount.
    STA (@@), Y                ; Reset the file's QuestNumber.
    JSR FetchFileAAddressSet
    JSR CalculateFileAChecksum
    LDY CurSaveSlot
    ; Be proactive and reset these values in save slot info.
    ;
    LDA #@@
    STA IsSaveSlotActive, Y
    STA QuestNumbers, Y
    STA DeathCounts, Y
    ; Since file A is in a good state, we don't care about file B.
    ; Treat it as committed.
    LDA #@@
    STA IsSaveFileBCommitted, Y
    LDA #@@                    ; Store the static markers in the save file.
    STA SaveFileOpenMarkers, Y
    LDA #@@
    STA SaveFileCloseMarkers, Y
    TYA                         ; Store the checksum in the save file.
    ASL
    TAY
    LDA @@
    STA FileAChecksums, Y
    INY
    LDA @@
    STA FileAChecksums, Y
    RTS

CalculateAndStoreFileBChecksumUncommitted:
    JSR CalculateFileBChecksum
    LDY CurSaveSlot
    LDA #@@                    ; Now file B is valid, but uncommitted.
    STA IsSaveFileBCommitted, Y
    TYA
    ASL
    TAY
    LDA @@
    STA FileBChecksums, Y
    INY
    LDA @@
    STA FileBChecksums, Y
    RTS

; Returns:
; [CF:CE]: checksum
;
CalculateFileBChecksum:
    LDA #@@                    ; Reset the sum.
    STA @@
    STA @@
    LDY #@@                    ; Sum the name (8 bytes).
@SumName:
    LDA (@@), Y
    JSR AddAToCFCE
    DEY
    BPL @SumName
    LDY #@@                    ; Sum the $28 bytes of the file's Items block with [$CF:CE].
@SumItems:
    LDA (@@), Y
    JSR AddAToCFCE
    DEY
    BPL @SumItems
    LDA #@@                    ; Will count $180 with [C1:C0].
    STA @@
    LDA #@@
    STA @@
    LDY #@@                    ; Add World Flags ($180 bytes) to [CF:CE].
@SumWorldFlags:
    LDA (@@), Y
    JSR AddAToCFCE
    INC @@
    BNE :+
    INC @@
:
    DEC @@
    BNE @SumWorldFlags
    DEC @@
    LDA @@
    BPL @SumWorldFlags
    LDA (@@), Y                ; Add IsSaveSlotActive byte to [$CF:CE].
    JSR AddAToCFCE
    LDA (@@), Y                ; Add DeathCount byte to [$CF:CE].
    JSR AddAToCFCE
    LDA (@@), Y                ; TODO: Add byte at [[$CA:CB]] to [$CF:CE].
    JSR AddAToCFCE
    LDA (@@), Y                ; Add byte QuestNumber to [$CF:CE].
AddAToCFCE:
    CLC
    ADC @@
    STA @@
    LDA @@
    ADC #@@
    STA @@
    RTS

FormatFileB:
    LDY #@@                    ; Clear the name (to all spaces).
    LDA #@@
@ClearName:
    STA (@@), Y
    DEY
    BPL @ClearName
    LDY #@@                    ; Clear $28 bytes of the Items block in file.
    LDA #@@
@ClearItems:
    STA (@@), Y
    DEY
    BPL @ClearItems
    LDA #@@                    ; Will count $180 with [C1:C0].
    STA @@
    LDA #@@
    STA @@
    LDY #@@                    ; Clear $180 bytes of World flags.
@ClearWorldFlags:
    LDA #@@
    STA (@@), Y
    INC @@
    BNE :+
    INC @@
:
    DEC @@
    BNE @ClearWorldFlags
    DEC @@
    LDA @@
    BPL @ClearWorldFlags
    LDA #@@                    ; Clear individual bytes.
    STA (@@), Y                ; IsSaveSlotActive
    STA (@@), Y                ; TODO: ?
    STA (@@), Y                ; DeathCount
    STA (@@), Y                ; QuestNumber
    JSR FetchFileBAddressSet
    JSR CalculateFileBChecksum  ; Leave the checksum at [$CF:CE].
    LDA #@@
    LDY CurSaveSlot
    STA IsSaveFileBCommitted, Y ; Mark this file B committed.
    RTS

InitMode1_Sub1:
    ; Reset CurSaveSlot. Doing this is useful for the
    ; work done here, and for the sequence of submodes
    ; that generate and transfer save slot graphics.
    LDA #@@
    STA CurSaveSlot
    JSR FetchFileAAddressSet
    LDY #@@                    ; The ring is at this offset in Items block.
    LDX #@@                    ; The offset to the byte we want to change in a palette.
@LoopSlot:
    TYA                         ; Save ring offset in Items block of current slot.
    PHA
    LDA (@@), Y                ; Get the ring inventory value.
    TAY
    LDA LinkColors, Y           ; Get the color for that ring level.
    ; Put the color in the byte 2 of row for current slot in
    ; sprite palette that will be transferred.
    STA MenuPalettesTransferBuf+20, X
    PLA                         ; Restore ring offset.
    CLC
    ADC #@@                    ; Point to the ring in the next save slot.
    TAY
    TXA
    CLC
    ADC #@@                    ; Point one row down in palette.
    TAX
    CPX #@@
    BCC @LoopSlot               ; 3 times.
    JSR ResetRoomTileObjInfo    ; TODO: ?
    LDA #@@                    ; Cue transfer of menu palettes.
    STA TileBufSelector
    INC GameSubmode
    JSR TurnOffVideoAndClearArtifacts
    LDY #@@
    LDA #@@
    STA @@                   ; TODO: $529?
:
    STA RoomHistory, Y
    DEY
    BPL :-
    RTS

InitMode1_Sub2:
    LDA #@@
    STA TileBufSelector
    INC GameSubmode
    RTS

InitMode1_FillAndTransferSlotTiles:
    ; Copy the mode 1 line transfer buf template to
    ; dynamic transfer buf.
    LDY #@@
@CopySlotLineTemplate:
    LDA Mode1SlotLineTransferBuf, Y
    STA DynTileBuf, Y
    DEY
    BPL @CopySlotLineTemplate
    LDY CurSaveSlot             ; The first time, this is reset in a previous submode.
@OffsetSlotLineAddr:
    LDA DynTileBuf+1            ; Add ($60 * CurSaveSlot) to the PPU address of each record in the buffer.
    CLC
    ADC #@@
    STA DynTileBuf+1
    LDA DynTileBuf+21
    CLC
    ADC #@@
    STA DynTileBuf+21
    LDA DynTileBuf
    ADC #@@
    STA DynTileBuf
    STA DynTileBuf+20
    DEY
    BPL @OffsetSlotLineAddr
    ; Copy name of current slot to beginning of payload
    ; of first record in dynamic transfer buf.
    LDA CurSaveSlot
    ASL
    ASL
    ASL
    TAX
    LDY #@@
@CopyName:
    LDA Names, X
    STA DynTileBuf, Y
    INX
    INY
    CPY #@@
    BNE @CopyName
    ; Copy heart values from current save slot info
    ; to [$0E:0F] for formatting.
    LDA CurSaveSlot
    ASL
    TAY
    LDA SaveSlotHearts, Y
    STA @@
    INY
    LDA SaveSlotHearts, Y
    STA @@
    LDY #@@
    JSR FormatHeartsInTextBuf
    INC CurSaveSlot             ; Next time, process the next slot.
    INC GameSubmode
    RTS

InitMode1_Sub6:
    ; Copy the mode 1 death counts transfer buf template to
    ; dynamic transfer buf.
    LDY #@@
@CopyTemplate:
    LDA Mode1DeathCountsTransferBuf, Y
    STA DynTileBuf, Y
    DEY
    BPL @CopyTemplate
    LDA #@@
    STA @@                     ; The save slot.
    LDA #@@
    STA @@                     ; The offset where the string will be written in dynamic transfer record.
@LoopSlot:
    LDY @@
    LDA DeathCounts, Y
    JSR FormatDecimalByte
    LDX @@
    LDA @@
    STA DynTileBuf, X           ; Emit the first character.
    LDA @@
    STA DynTileBuf+1, X         ; Emit the second character.
    LDA @@
    BNE @EmitChar               ; If the third character isn't '0', then go emit it.
    ; If the first or second characters weren't spaces,
    ; then go ahead and emit the '0'.
    LDA @@
    CMP #@@
    BNE @Emit0
    LDA @@
    CMP #@@
    BNE @Emit0
    LDY @@
    LDA IsSaveSlotActive, Y
    BNE @Emit0                  ; If this slot isn't active,
    LDA #@@
    BNE @EmitChar               ; Go emit a space.
@Emit0:
    LDA #@@                    ; Else, emit a '0'.
@EmitChar:
    STA DynTileBuf+2, X
    TXA                         ; Advance the offset by 6,
    CLC                         ; to the starting position in the next transfer record.
    ADC #@@
    STA @@
    INC @@                     ; Increment the save slot.
    LDA @@
    CMP #@@
    BNE @LoopSlot               ; Go process the next slot, if not done.
    LDY #@@                    ; Find the first save slot that's active.
    STY CurSaveSlot
    STY CaveSourceRoomId        ; Use room ID $FF, so that mode 3 "Unfurl" will put the player in the room at StartRoomId.
@FindActiveSlot:
    INY
    INC CurSaveSlot
    LDA IsSaveSlotActive, Y
    BEQ @FindActiveSlot
    LDA #@@
    STA GameSubmode
    INC IsUpdatingMode          ; Start updating.
    RTS

Mode1CursorSpriteTriplet:
    .BYTE @@, @@, @@

Mode1CursorSpriteYs:
    .BYTE @@, @@, @@, @@, @@

UpdateMode1Menu:
    LDA GameSubmode
    JSR TableJump
UpdateMode1Menu_JumpTable:
    .ADDR UpdateMode1Menu_Sub0
    .ADDR UpdateMode1Menu_Sub1

UpdateMode1Menu_Sub0:
    LDA ButtonsPressed
    AND #@@
    BNE @Exit                   ; If Start was pressed, the go to the next submode.
    LDA ButtonsPressed
    AND #@@
    BEQ :+                      ; If Select was pressed,
@ChangeSelection:
    LDA #@@                    ; Request to play the selection change SFX (same as rupee taken).
    STA Tune1Request
    INC CurSaveSlot             ; Select the next slot or option.
    LDA CurSaveSlot
    CMP #@@
    BNE :+                      ; If the index is out of bounds,
    LDA #@@                    ; then wrap around to zero.
    STA CurSaveSlot
:
    LDY CurSaveSlot
    ; Since CurSaveSlot is used to index menu choices, which
    ; includes register and eliminate in addition to save slots;
    ; the IsSaveSlotActive array includes elements at the end
    ; for these options.
    LDA IsSaveSlotActive, Y
    BEQ @ChangeSelection        ; If option or save slot isn't active, then go advance the index.
    LDY #@@                    ; Write the tile, attributes, and X for the sprite record.
@WriteCursorSprite:
    LDA Mode1CursorSpriteTriplet, Y
    STA Sprites+1, Y
    DEY
    BPL @WriteCursorSprite
    LDY CurSaveSlot
    LDA Mode1CursorSpriteYs, Y
    STA Sprites                 ; Set the sprite Y for current option.
    LDA #@@                    ; The base Y of Link sprites is $58.
    STA @@
    LDA #@@                    ; The X coordinate of Link sprites is $30.
    STA @@
    JMP Mode1_WriteLinkSprites

@Exit:
    INC GameSubmode
    RTS

UpdateMode1Menu_Sub1:
    LDA #@@
    STA Tune1
    LDA #@@
    STA CurLevel                ; Begin in OW.
    STA SelectedItemSlot        ; Reset item index.
    JSR TurnOffAllVideo
    LDA CurSaveSlot
    CMP #@@
    BCC @ChoseSlot              ; If not a save slot,
    LDA CurSaveSlot             ; Go to the mode for each option (register, eliminate).
    CLC
    ADC #@@
    STA GameMode
    JMP EndGameMode

@ChoseSlot:
    ; The player chose a save slot.
    ;
    JSR TurnOffAllVideo
    JSR FetchFileAAddressSet
    LDY #@@                    ; Copy Items block from file A to profile.
@CopyItems:
    LDA (@@), Y
    STA Items, Y
    DEY
    BPL @CopyItems
    LDA #@@
    STA SwordBlocked
    STA ObjState                ; Reset player state.
    STA InvClock                ; Reset clock item.
    ; Copy WorldFlags block from file A to profile.
    ;
    TAY
@CopyWorldFlags:
    LDA (@@), Y
    STA (@@), Y
    INC @@
    BNE :+
    INC @@
:
    INC @@
    BNE :+
    INC @@
:
    LDA @@
    CMP #@@
    BNE @CopyWorldFlags
    LDA @@
    CMP #@@
    BNE @CopyWorldFlags         ; If not done (destination address < $07FF), then copy more.
    JMP GoToNextMode

Mode1_WriteLinkSprites:
    LDA #@@                    ; Put the left tile of Link in [$02].
    STA @@
    LDA #@@                    ; Put the right tile of Link in [$03].
    STA @@
    ; [$04] is used as an index and sprite attributes.
    ; It takes on values 0 to 2.
    ; As an index, it represents a save slot.
    ; As attributes, these values represent palettes 4 to 6.
    LDA #@@
    JSR Anim_SetSpriteDescriptorAttributes
    ; We want to start with sprite 4 (offset $10).
    ; Begin with 8, so that the loop will add 8 and
    ; put us at the offset we want.
    LDA #@@
    STA LeftSpriteOffset
@LoopSlot:
    LDA LeftSpriteOffset
    CLC
    ADC #@@                    ; Each Link is two sprites (8 bytes).
    STA LeftSpriteOffset
    CLC
    ADC #@@                    ; The right side is the next sprite.
    STA RightSpriteOffset
    LDA #@@                    ; This object has two sides (sprites).
    STA @@
    LDA #@@                    ; The two sides are 8 pixels apart.
    STA @@
    LDA @@                     ; Save the X coordinate.
    PHA
    JSR Anim_WriteSpritePairNotFlashing    ; We didn't set [$08]. But we don't care if CurSpriteIndex is cycled.
    TAX
    PLA                         ; Restore the X coordinate.
    STA @@
    LDY @@                     ; Use [$04] as a save slot number.
    LDA QuestNumbers, Y
    BEQ @NextSlot               ; If in first quest, then skip the second quest marker.
    LDY LeftSpriteOffset
    LDA @@                     ; Put the sword 3 pixels below Link.
    SEC
    SBC #@@
    STA Sprites+128, Y          ; Use sprites $20 to $23 for the swords.
    LDA #@@                    ; Use sword tiles.
    STA Sprites+129, Y
    LDA #@@                    ; Use palette 7.
    STA Sprites+130, Y
    LDA @@                     ; Put the sword $C pixels to the right of Link.
    CLC
    ADC #@@
    STA Sprites+131, Y
@NextSlot:
    LDA @@                     ; Move Y down for the next slot.
    CLC
    ADC #@@
    STA @@
    INC @@                     ; Look at the next slot, and use the next palette.
    INC @@
    LDA @@
    CMP #@@
    BNE @LoopSlot               ; If we're not done, then look at the next slot.
    RTS

SaveSlotHeartsAddrsLo:
    .LOBYTES SaveSlotHearts+0
    .LOBYTES SaveSlotHearts+2
    .LOBYTES SaveSlotHearts+4

SaveSlotHeartsAddrsHi:
    .HIBYTES SaveSlotHearts+0
    .HIBYTES SaveSlotHearts+2
    .HIBYTES SaveSlotHearts+4

ProfileNameAddrsLo:
    .LOBYTES Names+0
    .LOBYTES Names+8
    .LOBYTES Names+16

ProfileNameAddrsHi:
    .HIBYTES Names+0
    .HIBYTES Names+8
    .HIBYTES Names+16

UpdateModeDSave:
    LDA GameSubmode
    JSR TableJump
UpdateModeDSave_JumpTable:
    .ADDR UpdateModeDSave_Sub0
    .ADDR UpdateModeDSave_Sub1
    .ADDR UpdateModeDSave_Sub2

UpdateModeDSave_Sub0:
    ; Initialize file B, and copy profile to it.
    ; Calculate and store file B checksum.
    ; Mark file B uncommitted.
    ;
    JSR FetchFileBAddressSet
    JSR FormatFileB
    JSR FetchFileBAddressSet
    JSR FetchFileAAddressSet    ; This seems to be called only for $067F put in [$0E:0F].
    LDY #@@                    ; Copy Items block ($28 bytes) from profile to file B.
@CopyItems:
    LDA Items, Y
    STA (@@), Y
    DEY
    BPL @CopyItems
    LDY CurSaveSlot
    LDA DeathCounts, Y          ; Copy death count from profile to file B.
    LDY #@@
    STA (@@), Y
    LDA #@@                    ; We're saving, so make sure the current slot is active.
    STA (@@), Y
    LDY CurSaveSlot
    STA IsSaveSlotActive, Y     ; Also set the slot active in the save slot info.
    LDA QuestNumbers, Y         ; Copy quest number from profile to file B.
    LDY #@@
    STA (@@), Y
    JSR FetchProfileNameAddress
    LDY #@@                    ; Copy name from save slot info to file B.
@CopyName:
    LDA (@@), Y
    STA (@@), Y
    DEY
    BPL @CopyName
    LDA HeartValues             ; Put in [$0A] a full hearts value for the profile's heart containers.
    AND #@@
    PHA
    LSR
    LSR
    LSR
    LSR
    STA @@
    PLA
    ORA @@
    STA HeartValues
    LDA #@@                    ; Completely fill the hearts.
    STA HeartPartial
    JSR StoreSaveSlotHearts
    LDY #@@                    ; Copy World Flags ($180 bytes) from profile to file B.
@CopyWorldFlags:
    LDA (@@), Y
    STA (@@), Y
    INC @@
    BNE :+
    INC @@
:
    INC @@
    BNE :+
    INC @@
:
    LDA @@
    CMP #@@
    BNE @CopyWorldFlags
    LDA @@
    CMP #@@
    BNE @CopyWorldFlags
    JSR FetchFileBAddressSet
    JSR CalculateAndStoreFileBChecksumUncommitted
    INC GameSubmode             ; Go to the next submode.
    RTS

UpdateModeDSave_Sub1:
    LDY CurSaveSlot
    LDA IsSaveFileBCommitted, Y
    BNE @IncSubmode
    JSR FetchFileBAddressSet
    JSR CalculateFileBChecksum
    LDA CurSaveSlot
    ASL
    TAY
    LDA FileBChecksums, Y
    CMP @@
    BNE @DiscardFileB
    INY
    LDA FileBChecksums, Y
    CMP @@
    BNE @DiscardFileB
    JSR CopyFileBToFileA        ; The checksum matches, so commit and copy file B to A.
@IncSubmode:
    INC GameSubmode
    RTS

@DiscardFileB:
    ; Discard file B, because it couldn't be validated.
    ;
    LDY CurSaveSlot
    LDA #@@
    STA IsSaveFileBCommitted, Y
    INC GameSubmode
    RTS

CopyFileBToFileA:
    LDY CurSaveSlot
    LDA #@@                    ; Reset the save file markers.
    STA SaveFileOpenMarkers, Y
    STA SaveFileCloseMarkers, Y
    TYA                         ; Reset the checksum.
    ASL
    TAY
    LDA #@@
    STA FileAChecksums, Y
    INY
    STA FileAChecksums, Y
    JSR FetchFileBAddressSet
    JSR FetchFileAAddressSet    ; This puts $067F in [$0E:0F].
    LDY #@@                    ; Copy Items block ($28 bytes) from file B to file A.
@CopyItems:
    LDA (@@), Y
    STA (@@), Y
    DEY
    BPL @CopyItems
    LDY #@@                    ; Copy these individual bytes from file B to file A.
    LDA (@@), Y                ; IsSaveSlotActive
    STA (@@), Y
    LDA (@@), Y                ; TODO: ?
    STA (@@), Y
    LDA (@@), Y                ; DeathCount
    STA (@@), Y
    LDA (@@), Y                ; QuestNumber
    STA (@@), Y
    LDA (@@), Y                ; Push IsSaveSlotActive from file A to help copy it to save slot info.
    PHA
    LDA (@@), Y                ; Push death count from file A to help copy it to save slot info.
    PHA
    LDA (@@), Y                ; Push quest number from file A to help copy it to save slot info.
    PHA
    LDY CurSaveSlot
    PLA                         ; Finish copying quest number from file A to save slot info.
    STA QuestNumbers, Y
    PLA                         ; Finish copying death count from file A to save slot info.
    STA DeathCounts, Y
    PLA                         ; Finish copying IsSaveSlotActive  from file A to save slot info.
    STA IsSaveSlotActive, Y
    LDY #@@                    ; Copy the name from file B to file A.
@CopyName:
    LDA (@@), Y
    STA (@@), Y
    DEY
    BPL @CopyName
    ; Copy World Flags ($180 bytes) file B to file A.
    ; It counts up from what's in [$0E:0F] ($067F) to $07FF.
    LDY #@@
@CopyWorldFlags:
    LDA (@@), Y
    STA (@@), Y
    INC @@
    BNE :+
    INC @@
:
    INC @@
    BNE :+
    INC @@
:
    INC @@
    BNE :+
    INC @@
:
    LDA @@
    CMP #@@
    BNE @CopyWorldFlags
    LDA @@
    CMP #@@
    BNE @CopyWorldFlags
    LDY CurSaveSlot
    LDA #@@                    ; Write the save file markers.
    STA SaveFileOpenMarkers, Y
    LDA #@@
    STA SaveFileCloseMarkers, Y
    TYA                         ; Copy the checksum from file B to file A.
    ASL
    TAY
    LDA FileBChecksums, Y
    STA FileAChecksums, Y
    INY
    LDA FileBChecksums, Y
    STA FileAChecksums, Y
    LDY CurSaveSlot
    LDA #@@                    ; File B has been committed.
    STA IsSaveFileBCommitted, Y
    RTS

UpdateModeDSave_Sub2:
    LDA #@@                    ; Go to mode 0 submode 1. Keep it updating.
    STA GameMode
    LDA #@@
    STA GameSubmode
    RTS

; Returns:
; [0C:0D]: save slot name pointer
;
FetchProfileNameAddress:
    LDY CurSaveSlot
    LDA ProfileNameAddrsLo, Y
    STA @@
    LDA ProfileNameAddrsHi, Y
    STA @@
    RTS

StoreSaveSlotHearts:
    LDY CurSaveSlot
    LDA SaveSlotHeartsAddrsLo, Y
    STA @@
    LDA SaveSlotHeartsAddrsHi, Y
    STA @@
    LDY #@@                    ; Copy HeartsValue and HeartsPartial to set B.
@CopyHearts:
    LDA HeartValues, Y
    STA (@@), Y
    DEY
    BPL @CopyHearts
    RTS

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

InitMode13_Full:
    LDA GameSubmode
    JSR TableJump
InitMode13_Full_JumpTable:
    .ADDR InitMode13_Sub0
    .ADDR InitMode13_Sub1
    .ADDR InitMode13_Sub2
    .ADDR InitMode13_Sub3
    .ADDR InitMode13_Sub4

InitMode13_Sub0:
    JSR UpdateEndGameCurtainEffect
    LDA GameSubmode
    BEQ LA958_Exit
    JSR HideAllSprites
    JSR Link_EndMoveAndDraw
    LDX #@@                    ; Zelda is in object slot 1.
    JMP Person_Draw

UpdateEndGameCurtainEffect:
    LDA ObjTimer
    BNE @Exit
    LDA Song
    BNE @Exit
    JSR UpdateWorldCurtainEffect_Bank2
    ; When the right curtain edge reaches the middle (< $11),
    ; go to the next submode.
    ;
    LDA ObjX+12
    CMP #@@
    BCS @Exit
    ; TODO: Why set the timer?
    ;
    LDA #@@
    STA ObjTimer
    INC GameSubmode
@Exit:
    RTS

PlayAreaAttr0TransferBuf:
    .BYTE @@, @@, @@, @@, @@

InitMode13_Sub1:
    LDY #@@
:
    LDA PlayAreaAttr0TransferBuf, Y
    STA DynTileBuf, Y
    DEY
    BPL :-
    ; Point to the front of the first textbox line,
    ; and reset the current character index.
    ;
    LDA #@@
    STA TextboxCharPtr
    LDA #@@
    STA TextboxCharIndex
    STA ObjState+1
    INC GameSubmode
LA958_Exit:
    RTS

ThanksText:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@

InitMode13_Sub2:
    JSR UpdateZeldaTextbox
    LDA ObjState+1
    BEQ :+
    ; Set a delay of $50 frames in the next submode.
    ;
    LDA #@@
    STA ObjTimer+1
    INC GameSubmode
:
    RTS

ThanksTextboxCharTransferRecTemplate:
    .BYTE @@, @@, @@, @@, @@

ThanksTextboxLineAddrsLo:
    .BYTE @@, @@, @@

; Returns:
; ObjState[1]: 0 if still updating
;
UpdateZeldaTextbox:
    JSR Link_EndMoveAndDraw
    ; If Zelda's timer has not expired, then return.
    ;
    LDA ObjTimer+1
    BNE LA9F4_Exit
    ; Set the timer to wait 6 frames after the next character about
    ; to be shown.
    ;
    LDA #@@
    STA ObjTimer+1
    ; Copy the 5 bytes of the textbox character transfer record template
    ; to the dynamic transfer buf.
    ;
    LDY #@@
:
    LDA ThanksTextboxCharTransferRecTemplate, Y
    STA DynTileBuf, Y
    DEY
    BPL :-
:
    ; Replace the low byte of the VRAM address with the one
    ; where the next character should be written.
    ;
    LDA TextboxCharPtr
    STA DynTileBuf+1
    ; Increment the low VRAM address for the next character.
    ;
    INC TextboxCharPtr
    LDA #<ThanksText
    STA @@
    LDA #>ThanksText
    STA @@
    ; Load the person text current character index.
    ;
    LDY TextboxCharIndex
    ; Increment the index variable to point to the next character
    ; for the next time.
    ;
    INC TextboxCharIndex
    ; Get the current character.
    ;
    LDA (@@), Y
    ; If the character is $25, then it's a special space. It will still
    ; take up space, but will not take time to show -- meaning that
    ; we'll go look up the next character to transfer.
    ;
    AND #@@
    CMP #@@
    BEQ :-
    ; We have a non-space character. Put it in the transfer record.
    ;
    STA DynTileBuf+3
    ; Play the "heart taken/character" tune.
    ;
    LDA #@@
    STA Tune0Request
    ; If the high 2 bits of character element = 0, then return.
    ;
    LDA (@@), Y
    AND #@@
    BEQ LA9F4_Exit
    ; Determine an index based on the high 2 bits of the character element:
    ;   $80: 0
    ;   $40: 1
    ;   $C0: 2
    ;
    LDY #@@
    CMP #@@
    BEQ :+
    DEY
    CMP #@@
    BEQ :+
    DEY
:
    ; The index chooses the low VRAM address of the start
    ; of another line:
    ;   0: $C4: front of the second line
    ;   1: $E4: front of the third line
    ;   2: $A4: front of the first line
    ;
    LDA ThanksTextboxLineAddrsLo, Y
    STA TextboxCharPtr
    ; If index = 2, then we've reached the end of the text,
    ; and low VRAM address is moved to the front of the first line.
    ; So, advance the state of the person object, and unhalt Link.
    ;
    CPY #@@
    BNE LA9F4_Exit
    INC ObjState+1
    LDA #@@
    STA ObjState
LA9F4_Exit:
    RTS

InitMode13_Sub3:
    LDA ObjTimer+1
    BNE LA9F4_Exit
    JSR SilenceAllSound
    INC GameSubmode
    RTS

InitMode13_Sub4:
    LDA #@@
    STA CreditsTileOffset
    JSR BeginUpdateMode
    STA PeaceCharDelayCounter
    STA PeaceCharIndex
    JMP HideAllSprites

UpdateMode13WinGame:
    LDA GameSubmode
    JSR TableJump
UpdateMode13WinGame_JumpTable:
    .ADDR UpdateMode13WinGame_Sub0_Flash
    .ADDR UpdateMode13WinGame_Sub1
    .ADDR UpdateMode13WinGame_Sub1
    .ADDR UpdateMode13WinGame_Sub3
    .ADDR UpdateMode13WinGame_Sub4

EndingFlashColors:
    .BYTE @@, @@, @@, @@

UpdateMode13WinGame_Sub0_Flash:
    JSR HideAllSprites
    INC ItemLiftTimer
    LDA ItemLiftTimer
    CMP #@@
    BEQ @AdvanceSubmode
    JSR DrawLinkZeldaTriforces
@ChangePalette:
    LDX ItemLiftTimer
    ; Don't flash until $40 frames have passed.
    ;
    CPX #@@
    BCC @Exit
    ; Copy the level palette transfer buf.
    ;
    LDY #@@
:
    LDA LevelInfo_PalettesTransferBuf, Y
    STA DynTileBuf, Y
    DEY
    BPL :-
    ; Use (timer MOD 4) as an index to look up a color.
    ;
    TXA
    AND #@@
    TAX
    ; Change element 0 of palette 4 in buffer to change
    ; the background color.
    ;
    LDA EndingFlashColors, X
    STA DynTileBuf+19
@Exit:
    RTS

@AdvanceSubmode:
    ; Play the ending song.
    ;
    LDA #@@
    STA SongRequest
    ; Wait $40 frames at the beginning of the next mode
    ; before showing text.
    ;
    LDA #@@
    STA ObjTimer
    ; Set a long timer of $40 ($280 frames) for the whole duration
    ; of the next submode.
    ;
    LDA #@@
    STA EndingFlashLongTimer
    INC GameSubmode
    JMP @ChangePalette

DrawLinkZeldaTriforces:
    ; The triforce over Link goes in the room object slot $13.
    ; Set its location $10 pixels above Link.
    ;
    LDA ObjX
    STA ObjX+19
    LDA ObjY
    SEC
    SBC #@@
    STA ObjY+19
    ; Draw Link.
    ;
    LDX #@@
    JSR Anim_FetchObjPosForSpriteDescriptor
    JSR Anim_SetSpriteDescriptorAttributes
    STA @@
    LDA #@@
    STA LeftSpriteOffset
    LDA #@@
    STA RightSpriteOffset
    LDY #@@
    JSR Anim_WriteSpecificItemSprites
    ; Draw triforce over Link.
    ;
    LDA #@@
    LDX #@@
    JSR AnimateItemObject
    LDX #@@
    JSR Anim_FetchObjPosForSpriteDescriptor
    ; Draw Zelda.
    ;
    TXA
    JSR DrawObjectMirrored
    ; The triforce over Zelda goes in slot 2.
    ; Set its location $10 pixels above her.
    ;
    LDA ObjX+1
    STA ObjX+2
    LDA ObjY+1
    SEC
    SBC #@@
    STA ObjY+2
    ; Draw triforce over Zelda.
    ;
    LDX #@@
    LDA #@@
    JSR AnimateItemObject
    RTS

UpdateMode13WinGame_Sub1:
    LDA EndingFlashLongTimer
    BEQ @AdvanceSubmode
    JSR HideAllSprites
    ; Characters stop emitting when the long timer is $10.
    ; So, keep showing Link and Zelda until it reaches 4.
    ; Then hide them and wait until it reaches 0.
    ;
    LDA EndingFlashLongTimer
    CMP #@@
    BCC @Exit
    JSR DrawLinkZeldaTriforces
    ; Once in submode 2, only delay, instead of emitting characters.
    ;
    LDA GameSubmode
    CMP #@@
    BNE @Exit
    ; There is a delay before showing text.
    ;
    LDA ObjTimer
    BNE @Exit
    JSR UpdatePeaceTextbox
@Exit:
    RTS

@AdvanceSubmode:
    ; Transfer the ending palette, and go to the next submode.
    ;
    LDA #@@
    STA TileBufSelector
    INC GameSubmode
    RTS

PeaceTextboxCharTransferRecTemplate:
    .BYTE @@, @@, @@, @@, @@

PeaceTextboxCharAddrsLo:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@

PeaceText:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@

UpdatePeaceTextbox:
    ; Only emit a character once every 8 frames --
    ; when (counter [0412] MOD 8) = 4.
    ;
    INC PeaceCharDelayCounter
    LDA PeaceCharDelayCounter
    AND #@@
    CMP #@@
    BNE @Exit
    ; Copy the 5 bytes of the textbox character transfer record template
    ; to the dynamic transfer buf.
    ;
    LDY #@@
:
    LDA PeaceTextboxCharTransferRecTemplate, Y
    STA DynTileBuf, Y
    DEY
    BPL :-
    ; Load a character from the string and copy it to
    ; the transfer record until we read character $FF.
    ;
    LDY PeaceCharIndex
    LDA PeaceText, Y
    CMP #@@
    BEQ @AdvanceSubmode
    STA DynTileBuf+3
    ; A regular space takes as much time to emit as any character.
    ; But it makes no sound.
    ;
    CMP #@@
    BEQ :+
    LDA #@@                    ; "Heart taken/character" tune
    STA Tune0Request
:
    ; Point to the next character in the string.
    ;
    INC PeaceCharIndex
    ; Replace the low byte of the VRAM address with the one
    ; where the next character should be written.
    ;
    LDA PeaceTextboxCharAddrsLo, Y
    STA DynTileBuf+1
    ; Once the low VRAM address rolls over,
    ; increment the high address.
    ;
    CMP #@@
    BCS @Exit
    LDA #@@
    STA DynTileBuf
@Exit:
    RTS

@AdvanceSubmode:
    INC GameSubmode
LAB7E_Exit:
    RTS

UpdateMode13WinGame_Sub4:
    JSR HideAllSprites
    ; Put the triforce in object slot 2 at location ($78, $88).
    ;
    LDX #@@
    LDA #@@
    STA ObjX, X
    LDA #@@
    STA ObjY, X
    LDA #@@                    ; Triforce item ID
    JSR AnimateItemObject
    ; Reuse object slot 2 for the ash pile.
    ;
    LDX #@@
    LDA #@@                    ; Ganon object type
    STA ObjType, X
    JSR DrawAshPile
    ; Don't let the player skip ahead for a little while.
    ;
    LDA ObjTimer
    BNE LAB7E_Exit
    ; If Start hasn't been pressed, then return.
    ;
    LDA ButtonsPressed
    AND #@@
    BEQ LAB7E_Exit
    ; Start was pressed. We'll transition to mode $D to save.
    ;
    JSR EndGameMode
    LDA #@@
    STA GameMode
    JSR TurnOffAllVideo
    JSR TurnOffVideoAndClearArtifacts
    JSR SilenceAllSound
    JMP SwitchProfileToSecondQuest

DrawAshPile:
    JSR Anim_FetchObjPosForSpriteDescriptor
    LDA #@@                    ; Ganon-ashes frame image
    JMP DrawObjectNotMirrored

CreditsLastScreenList:
    .BYTE @@, @@

CreditsLastVscrollList:
    .BYTE @@, @@

UpdateMode13WinGame_Sub3:
    ; After scrolling 8 pixels, draw another tile row.
    ;
    LDA CreditsTileOffset
    CMP #@@
    BMI :+
    LDA CreditsTileOffset
    SBC #@@
    STA CreditsTileOffset
    JSR DrawCredits
:
    ; Add $80 to the scroll speed fraction.
    ;
    LDA VScrollAddrHi
    CLC
    ADC #@@
    STA VScrollAddrHi
    ; Carry over to the tile offset and current V-scroll.
    ;
    BCC :+
    INC CreditsTileOffset
:
    LDA CurVScroll
    ADC #@@
    STA CurVScroll
    ; If we've reached the bottom of a nametable, then
    ; roll over current V-scroll to 0, and increase number of
    ; screens scrolled.
    ;
    CMP #@@
    LDA #@@
    BCC :+
    STA CurVScroll
    INC a:VScrollAddrLo
:
    ; Roll the carry from the comparison above into bit 0.
    ; So, if reached the bottom of a nametable, switch nametables.
    ;
    ROL
    STA SwitchNameTablesReq
    ; Put the quest number in Y register.
    ;
    LDY #@@
    LDX CurSaveSlot
    LDA QuestNumbers, X
    BEQ :+
    INY
:
    ; If we're not showing the last screen, then return.
    ;
    LDA a:VScrollAddrLo
    CMP CreditsLastScreenList, Y
    BCC @Exit
    ; If we haven't scrolled the last amount in the last screen,
    ; then return.
    ;
    LDA CurVScroll
    CMP CreditsLastVscrollList, Y
    BCC @Exit
    ; But if we have, then go to the next submode, and set a timer
    ; to wait $40 frames in the next submode.
    ;
    INC GameSubmode
    LDA #@@
    STA ObjTimer
@Exit:
    RTS

CreditLineVramAddrsHi:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@

CreditsPagesTextMasks:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@

CreditsTextAddrsLo:
.INCLUDE "dat/CreditsTextAddrsLo.inc"

CreditsTextAddrsHi:
.INCLUDE "dat/CreditsTextAddrsHi.inc"

CreditsTextLines:
.INCBIN "dat/CreditsTextLines.dat"

CreditsAttrs:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@

; Unknown block
    .BYTE @@

DrawCredits:
    ; Make a transfer record of a full row of blank tiles.
    ;
    LDY #@@
    LDA #@@
:
    STA DynTileBuf+3, Y
    DEY
    BPL :-
    ; No wall tiles go in row 0.
    ;
    LDA CreditsRow
    BEQ @TerminateRecords
    ; Rows 1 and $2E get horizontal wall tiles.
    ; Rows 2 to $2D get side walls.
    ;
    CMP #@@
    BEQ @WriteHorizontalWalls
    CMP #@@
    BCC @WriteSideWalls
    BNE @TerminateRecords
@WriteHorizontalWalls:
    LDY #@@
    LDA #@@                    ; Wall bricks tile
:
    STA DynTileBuf+6, Y
    DEY
    BPL :-
@WriteSideWalls:
    LDA #@@                    ; Wall bricks tile
    STA DynTileBuf+6
    STA DynTileBuf+31
@TerminateRecords:
    ; Put the buffer's end marker -- whether we transfer tiles and
    ; attributes or only tiles.
    ;
    LDA #@@
    STA DynTileBuf+35           ; The end of the first record: tiles
    STA DynTileBuf+46           ; The end of the second record: NT attributes
    ; Specify that we're transferring $20 bytes/tiles.
    ;
    LDA #@@
    STA DynTileBuf+2
    ; Write the high byte of the current VRAM page.
    ;
    LDX CreditsVramPage
    LDA CreditLineVramAddrsHi, X
    STA DynTileBuf
    ; The line number will be used to shift the task mask below.
    ;
    LDA CreditsVramLine
    TAY
    ; Multiply the VRAM line number by $20 to get the low VRAM address.
    ;
    ASL
    ASL
    ASL
    ASL
    ASL
    STA DynTileBuf+1
    ; Get the mask for the current VRAM page.
    ;
    LDA CreditsPagesTextMasks, X
:
    ; Shift left as many times as the current VRAM line number,
    ; to get the bit that indicates whether this line has text.
    ;
    ASL
    DEY
    BPL :-
    BCC @IncVramLine
    ; The mask inidcates that it should have text.
    ; But if line index >= $17, then it doesn't.
    ;
    LDY CreditsLineIndex
    CPY #@@
    BCS @IncVramLine
    ; In the first quest, don't consider line numbers >= $10.
    ;
    LDX CurSaveSlot
    LDA QuestNumbers, X
    BNE :+
    CPY #@@
    BCS @IncLine
:
    LDX CurSaveSlot
    LDA QuestNumbers, X
    ; In the second quest, skip lines $C to $F.
    ;
    BEQ :+
    CPY #@@
    BCC :+
    CPY #@@
    BCC @IncLine
:
    LDA CreditsTextAddrsLo, Y
    STA @@
    LDA CreditsTextAddrsHi, Y
    STA @@
    ; First read the length of the string.
    ;
    LDY #@@
    LDA (@@), Y
    STA @@                     ; The length of the string
    ; Second, read the offset where the first character goes
    ; in the line.
    ;
    INY
    LDA (@@), Y
    TAX
    ; Loop over each character in the rest of the credits source record.
    ;
    INY
:
    LDA (@@), Y
    STA DynTileBuf+3, X
    INY                         ; Increment the source pointer.
    INX                         ; Increment the destination pointer.
    DEC @@                     ; Decrement the count remaining.
    BNE :-
    ; If line index <> $11, go increment.
    ;
    LDY CreditsLineIndex
    CPY #@@
    BCC @IncLine
    CPY #@@
    BNE @IncLine
    ; Line index = $11, prepare to read the player's name.
    ;
    LDA CurSaveSlot
    ASL
    ASL
    ASL
    TAY
    ; Copy the player's name at offset 9 in string to transfer.
    ;
    LDX #@@
:
    LDA Names, Y
    STA DynTileBuf+12, X
    INY
    INX
    CPX #@@
    BCC :-
    ; Format the death count for the current save slot.
    ;
    LDY a:CurSaveSlot
    LDA DeathCounts, Y
    JSR FormatDecimalByte
    ; Copy the decimal death count at offset 19 in string to transfer.
    ;
    LDX #@@
:
    LDA @@, X
    STA DynTileBuf+22, X
    DEX
    BPL :-
    LDY CreditsLineIndex
@IncLine:
    INC CreditsLineIndex
@IncVramLine:
    INC CreditsVramLine
    ; In each VRAM page there are 8 lines, except in ones where
    ; (page MOD 4) = 3. This means $23xx and $2Bxx.
    ; These pages have 6 lines.
    ;
    LDA CreditsVramPage
    AND #@@
    CMP #@@
    LDA #@@
    BCC :+
    LDA #@@
:
    ; If the VRAM line number has not just been incremented to
    ; the reference line number count, then go write NT attributes.
    ;
    CMP CreditsVramLine
    BNE @WriteAttributes
    ; Else roll over the VRAM line to 0.
    ;
    LDA #@@
    STA CreditsVramLine
    ; Increment the VRAM page number.
    ;
    LDY CreditsVramPage
    INY
    ; If it has reached $C, then roll it over to 0.
    ;
    CPY #@@
    BCC :+
    TAY
:
    STY CreditsVramPage
@WriteAttributes:
    ; Every 4 rows, we have to transfer NT attributes.
    ; So, when (counter MOD 4) <> 0, skip filling an attribute record.
    ;
    LDA CreditsRow
    LSR
    BCS @IncRow
    LSR
    BCS @IncRow
    ; Attributes for the left and right blocks are 0.
    ;
    LDX #@@
    STX DynTileBuf+38
    STX DynTileBuf+45
    ; The row was already divided by 4. So now it refers to an
    ; NT attribute block row. Look up the attribute byte to use
    ; for almost every block in this NT attribute block row.
    ;
    TAY
    LDA CreditsAttrs, Y
    ; Fill a record with this byte, so the whole NT attribute block
    ; row is changed.
    ;
    LDY #@@
:
    STA DynTileBuf+39, Y
    DEY
    BPL :-
    ; If tiles are being written to NT 1 (>= $2800), then
    ; write the high byte of NT 1's attribute space (>= $2BC0).
    ; Else write the high byte of NT 0's attribute space (>= $23C0).
    ;
    LDY #@@
    LDA DynTileBuf
    AND #@@
    BEQ :+
    LDY #@@
:
    STY DynTileBuf+35
    ; Every attribute block row is at an offset (tile row * 2).
    ;
    LDA CreditsRow
    AND #@@
    ASL
    ; Add the offset to the low byte of attribute space base
    ; ($23C0 or $2BC0); and write it to the record.
    ;
    ADC #@@
    STA DynTileBuf+36
    LDA #@@                    ; 8 bytes in the attribute row record.
    STA DynTileBuf+37
@IncRow:
    ; Add 1 to the row.
    ;
    LDY CreditsRow
    INY
    ; Skip the last two rows in each cycle.
    ; When (row MOD $20) gets to $1E, add 2.
    ;
    ; Again, this is because there are
    ; 240 pixels vertically instead of 256.
    ;
    TYA
    AND #@@
    CMP #@@
    BCC :+
    INY
    INY
:
    STY CreditsRow
    RTS

WorldFlagBlockAddrs:
    .BYTE @@, @@, @@, @@, @@, @@

SwitchProfileToSecondQuest:
    ; Clear $80 bytes of each block of world flags.
    ;
    LDX #@@
@ClearBlock:
    LDA WorldFlagBlockAddrs, X
    STA @@
    LDA WorldFlagBlockAddrs+1, X
    STA @@
    LDY #@@
    LDA #@@
:
    STA (@@), Y
    DEY
    BPL :-
    DEX
    DEX
    BPL @ClearBlock
    ; Clear $28 byte block of items.
    ;
    LDY #@@
:
    STA Items, Y
    DEY
    BPL :-
    ; Set 3 full hearts and heart containers.
    ;
    LDA #@@
    STA HeartValues
    DEC HeartPartial
    LDA #@@
    STA MaxBombs
    LDY CurSaveSlot
    LDA #@@
    STA QuestNumbers, Y
    RTS


.SEGMENT "BANK_02_ISR"


.EXPORT SwitchBank_Local2

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
    .BYTE @@, @@, @@, @@

SwitchBank_Local2:
    STA @@
    LSR
    STA @@
    LSR
    STA @@
    LSR
    STA @@
    LSR
    STA @@
    RTS


.SEGMENT "BANK_02_VEC"



; Unknown block
    .BYTE @@, @@, @@, @@, @@, @@

