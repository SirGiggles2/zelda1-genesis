.INCLUDE "Variables.inc"

.SEGMENT "BANK_06_00"


; Imports from program bank 07

.IMPORT TableJump

.EXPORT CopyCommonDataToRam
.EXPORT InitMode2_Submodes
.EXPORT UpdateMode2Load_Full

LevelBlockAddrsQ1:
    .ADDR LevelBlockOW
    .ADDR LevelBlockUW1Q1
    .ADDR LevelBlockUW1Q1
    .ADDR LevelBlockUW1Q1
    .ADDR LevelBlockUW1Q1
    .ADDR LevelBlockUW1Q1
    .ADDR LevelBlockUW1Q1
    .ADDR LevelBlockUW2Q1
    .ADDR LevelBlockUW2Q1
    .ADDR LevelBlockUW2Q1

LevelInfoAddrs:
    .ADDR LevelInfoOW
    .ADDR LevelInfoUW1
    .ADDR LevelInfoUW2
    .ADDR LevelInfoUW3
    .ADDR LevelInfoUW4
    .ADDR LevelInfoUW5
    .ADDR LevelInfoUW6
    .ADDR LevelInfoUW7
    .ADDR LevelInfoUW8
    .ADDR LevelInfoUW9

CommonDataBlockAddr_Bank6:
    .ADDR CommonDataBlock_Bank6

LevelBlockAddrsQ2:
    .ADDR LevelBlockOW
    .ADDR LevelBlockUW1Q2
    .ADDR LevelBlockUW1Q2
    .ADDR LevelBlockUW1Q2
    .ADDR LevelBlockUW1Q2
    .ADDR LevelBlockUW1Q2
    .ADDR LevelBlockUW1Q2
    .ADDR LevelBlockUW2Q2
    .ADDR LevelBlockUW2Q2
    .ADDR LevelBlockUW2Q2

InitMode2_Submodes:
    LDA GameSubmode
    JSR TableJump
InitMode2_Submodes_JumpTable:
    .ADDR InitMode2_Sub0
    .ADDR InitMode2_Sub1

InitMode2_Sub0:
    ; Copy level block for level.
    ;
    LDA CurLevel
    ASL
    TAX
    LDY CurSaveSlot
    LDA QuestNumbers, Y
    BNE @SecondQuest
    ; First quest.
    ;
    LDA LevelBlockAddrsQ1, X
    STA @@
    INX
    LDA LevelBlockAddrsQ1, X
    JMP @Copy

@SecondQuest:
    ; Second quest.
    ;
    LDA LevelBlockAddrsQ2, X
    STA @@
    INX
    LDA LevelBlockAddrsQ2, X
@Copy:
    STA @@
    JSR FetchLevelBlockDestInfo
    JSR CopyBlock
    RTS

InitMode2_Sub1:
    ; Copy level info.
    ;
    LDA CurLevel
    ASL
    TAX
    LDA LevelInfoAddrs, X
    STA @@
    INX
    LDA LevelInfoAddrs, X
    STA @@
    JSR FetchLevelInfoDestInfo
    JSR CopyBlock
    LDA #@@
    STA GameSubmode
    INC IsUpdatingMode
    RTS

CopyCommonDataToRam:
    LDX #@@                    ; Get the source address of common data block in ROM.
    LDA CommonDataBlockAddr_Bank6, X
    STA @@
    INX
    LDA CommonDataBlockAddr_Bank6, X
    STA @@
    JSR FetchDestAddrForCommonDataBlock
    JSR CopyBlock
    LDA #@@
    STA GameSubmode
    RTS

; Returns:
; [$02:03]: destination address
; [$04:05]: end address
;
; Destination address $687E.
FetchLevelBlockDestInfo:
    LDA #@@
    STA @@
    LDA #@@
    STA @@
    LDA #@@                    ; End address $6B7D.
    STA @@
    LDA #@@
    STA @@
    RTS

; Returns:
; [$02:03]: destination address
; [$04:05]: end address
;
; Destination address $6B7E.
FetchLevelInfoDestInfo:
    LDA #@@
    STA @@
    LDA #@@
    STA @@
    LDA #@@                    ; End address $6C7D.
    STA @@
    LDA #@@
    STA @@
    RTS

FetchDestAddrForCommonDataBlock:
    LDA #@@                    ; 67F0 to 687D (inclusive)
    STA @@
    LDA #@@
    STA @@
    LDA #@@
    STA @@
    LDA #@@
    STA @@
    RTS

; Params:
; [$00:01]: source address
; [$02:03]: destination address
; [$04:05]: end destination address
;
; Also increments submode.
;
CopyBlock:
    LDY #@@
@Loop:
    LDA (@@), Y
    STA (@@), Y
    LDA @@
    CMP @@
    BNE @Next
    LDA @@
    CMP @@
    BNE @Next
    INC GameSubmode
    RTS

@Next:
    LDA @@
    CLC
    ADC #@@
    STA @@
    LDA @@
    ADC #@@
    STA @@
    LDA @@
    CLC
    ADC #@@
    STA @@
    LDA @@
    ADC #@@
    STA @@
    JMP @Loop

UpdateMode2Load_Full:
    ; Make replacements for the second quest.
    ;
    LDY CurSaveSlot
    LDA QuestNumbers, Y
    BEQ @Exit                   ; If not second quest, then return.
    LDA CurLevel
    BEQ @PatchQ2Rooms           ; If OW, then go patch rooms.
    TAX
    ASL
    TAY
    ; Get an address for the current level that points
    ; to an array of replacement bytes for Q2 UW level info.
    ;
    ; This address array doesn't access the OW element (0).
    ; So, it overlaps the last two bytes of LevelInfoUWQ2Replacements9.
    ;
    LDA LevelInfoUWQ2ReplacementAddrs-2, Y
    STA @@
    LDA LevelInfoUWQ2ReplacementAddrs-1, Y
    STA @@
    ; Get the number of replacement bytes for Q2 UW level info.
    ; This address array doesn't access the OW element (0).
    ;
    LDY LevelInfoUWQ2ReplacementSizes-1, X
@ReplaceInfoBytes:
    ; Copy bytes from Q2 replacement array to level info
    ; starting at offset $29 (shortcut position array).
    LDA (@@), Y
    STA LevelInfo_ShortcutOrItemPosArray, Y
    DEY
    BPL @ReplaceInfoBytes
@Exit:
    RTS

@PatchQ2Rooms:
    ; Replace attributes of several rooms in OW in second quest.
    ;
    LDY #@@
@ReplaceRoomBytes:
    LDX LevelBlockAttrsBQ2ReplacementOffsets, Y
    LDA LevelBlockAttrsBQ2ReplacementValues, Y
    STA LevelBlockAttrsB, X
    DEY
    BPL @ReplaceRoomBytes
    LDA #@@
    STA LevelBlockAttrsD+11
    LDA #@@
    STA LevelBlockAttrsD+60
    LDA #@@
    STA LevelBlockAttrsD+116
    LDA #@@
    STA LevelBlockAttrsA+60
    LDA #@@
    STA LevelBlockAttrsA+116
    LDA #@@
    STA LevelBlockAttrsF+60
    LDA #@@
    STA LevelBlockAttrsF+116
    RTS

LevelBlockAttrsBQ2ReplacementOffsets:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@

LevelBlockAttrsBQ2ReplacementValues:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@

LevelInfoUWQ2Replacements1:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@

LevelInfoUWQ2Replacements2:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@

LevelInfoUWQ2Replacements3:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@

LevelInfoUWQ2Replacements4:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@

LevelInfoUWQ2Replacements5:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@

LevelInfoUWQ2Replacements6:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@

LevelInfoUWQ2Replacements7:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@

LevelInfoUWQ2Replacements8:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@

LevelInfoUWQ2Replacements9:
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

LevelInfoUWQ2ReplacementAddrs:
    .ADDR LevelInfoUWQ2Replacements1
    .ADDR LevelInfoUWQ2Replacements2
    .ADDR LevelInfoUWQ2Replacements3
    .ADDR LevelInfoUWQ2Replacements4
    .ADDR LevelInfoUWQ2Replacements5
    .ADDR LevelInfoUWQ2Replacements6
    .ADDR LevelInfoUWQ2Replacements7
    .ADDR LevelInfoUWQ2Replacements8
    .ADDR LevelInfoUWQ2Replacements9

LevelInfoUWQ2ReplacementSizes:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@

; Unknown block
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@

LevelBlockOW:
.INCBIN "dat/LevelBlockOW.dat"

LevelBlockUW1Q1:
.INCBIN "dat/LevelBlockUW1Q1.dat"

LevelBlockUW2Q1:
.INCBIN "dat/LevelBlockUW2Q1.dat"

LevelBlockUW1Q2:
.INCBIN "dat/LevelBlockUW1Q2.dat"

LevelBlockUW2Q2:
.INCBIN "dat/LevelBlockUW2Q2.dat"

LevelInfoOW:
.INCBIN "dat/LevelInfoOW.dat"

LevelInfoUW1:
.INCBIN "dat/LevelInfoUW1.dat"

LevelInfoUW2:
.INCBIN "dat/LevelInfoUW2.dat"

LevelInfoUW3:
.INCBIN "dat/LevelInfoUW3.dat"

LevelInfoUW4:
.INCBIN "dat/LevelInfoUW4.dat"

LevelInfoUW5:
.INCBIN "dat/LevelInfoUW5.dat"

LevelInfoUW6:
.INCBIN "dat/LevelInfoUW6.dat"

LevelInfoUW7:
.INCBIN "dat/LevelInfoUW7.dat"

LevelInfoUW8:
.INCBIN "dat/LevelInfoUW8.dat"

LevelInfoUW9:
.INCBIN "dat/LevelInfoUW9.dat"

CommonDataBlock_Bank6:

.SEGMENT "BANK_06_DATA"


.EXPORT ColumnDirectoryOW
.EXPORT LevelNumberTransferBuf
.EXPORT LevelPaletteRow7TransferBuf
.EXPORT MenuPalettesTransferBuf
.EXPORT TriforceRow0TransferBuf

MenuPalettesTransferBuf:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@

LevelPaletteRow7TransferBuf:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@

LevelNumberTransferBuf:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@

ColumnDirectoryOW:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@

TriforceRow0TransferBuf:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@

TriforceRow1TransferBuf:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@

TriforceRow2TransferBuf:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@

TriforceRow3TransferBuf:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@

TriforceTextTransferBuf:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@


.SEGMENT "BANK_06_DLIST"


.EXPORT TransferCurTileBuf

TransferBufAddrs:
    .ADDR DynTileBuf
    .ADDR StoryTileAttrTransferBuf
    .ADDR Mode8TextTileBuffer
    .ADDR LevelPaletteRow7TransferBuf
    .ADDR AquamentusPaletteRow7TransferBuf
    .ADDR OrangeBossPaletteRow7TransferBuf
    .ADDR LevelNumberTransferBuf
    .ADDR StatusBarStaticsTransferBuf
    .ADDR GameTitleTransferBuf
    .ADDR MenuPalettesTransferBuf
    .ADDR Mode1TileTransferBuf
    .ADDR ModeFCharsTransferBuf
    .ADDR LevelInfo_PalettesTransferBuf
    .ADDR DynTileBuf
    .ADDR DynTileBuf
    .ADDR BlankTextBoxLines
    .ADDR GhostPaletteRow7TransferBuf
    .ADDR GreenBgPaletteRow7TransferBuf
    .ADDR BrownBgPaletteRow7TransferBuf
    .ADDR CellarAttrsTransferBuf
    .ADDR DynTileBuf
    .ADDR BlankPersonWares
    .ADDR Mode11DeadLinkPalette
    .ADDR LevelNumberTransferBuf
    .ADDR InventoryTextTransferBuf
    .ADDR SubmenuBoxesTopsTransferBuf
    .ADDR SubmenuBoxesSidesTransferBuf
    .ADDR GanonPaletteRow7TransferBuf
    .ADDR SelectedItemBoxBottomTransferBuf
    .ADDR UseBButtonTextTransferBuf
    .ADDR InventoryBoxBottomTransferBuf
    .ADDR CaveBgPaletteRowsTransferBuf
    .ADDR SubmenuMapRemainderTransferBuf
    .ADDR SheetMapBottomEdgeTransferBuf
    .ADDR LevelInfo_StatusBarMapTransferBuf
    .ADDR GameOverTransferBuf
    .ADDR SubmenuAttrs1TransferBuf
    .ADDR SubmenuAttrs2TransferBuf
    .ADDR BlankBottomRowNT2TransferBuf
    .ADDR BlankRowTransferBuf
    .ADDR SubmenuTriforceApexTransferBuf
    .ADDR TriforceRow0TransferBuf
    .ADDR TriforceRow1TransferBuf
    .ADDR TriforceRow2TransferBuf
    .ADDR TriforceRow3TransferBuf
    .ADDR SubmenuTriforceBottomTransferBuf
    .ADDR TriforceTextTransferBuf
    .ADDR Mode11BackgroundPaletteBottomHalfTransferBuf
    .ADDR Mode11PlayAreaAttrsTopHalfTransferBuf
    .ADDR Mode11PlayAreaAttrsBottomHalfTransferBuf
    .ADDR DynTileBuf
    .ADDR DynTileBuf
    .ADDR DynTileBuf
    .ADDR EndingPaletteTransferBuf
    .ADDR BombCapacityPriceTextTransferBuf
    .ADDR DynTileBuf
    .ADDR DynTileBuf
    .ADDR DynTileBuf
    .ADDR DynTileBuf
    .ADDR LifeOrMoneyCostTextTransferBuf
    .ADDR WhitePaletteBottomHalfTransferBuf
    .ADDR RedArmosPaletteRow7TransferBuf
    .ADDR GleeokPaletteRow7TransferBuf
    .ADDR DynTileBuf

TransferCurTileBuf:
    LDX TileBufSelector
    LDA TransferBufAddrs, X
    STA @@
    LDA TransferBufAddrs+1, X
    STA @@
    JSR TransferTileBuf
    LDA #@@                    ; TODO: Why put $3F in [$0300]? Is it the maximum size of dynamic transfer buf?
    STA @@
    LDX #@@
    STX TileBufSelector
    STX SwitchNameTablesReq
    STX DynTileBufLen
    DEX
    STX DynTileBuf              ; Empty the tile buffer.
    RTS

ContinueTransferTileBuf:
    ;
    ;
    ; Save VRAM address high byte.
    PHA
    STA PpuAddr_2006
    INY
    LDA (@@), Y                ; Read low byte of VRAM address.
    STA PpuAddr_2006
    INY
    LDA (@@), Y                ; Read count and attribute byte.
    ASL
    PHA
    LDA CurPpuControl_2000
    ORA #@@
    BCS :+                      ; If high bit is set, then auto-increment VRAM address by 32.
    AND #@@
:
    STA PpuControl_2000
    STA CurPpuControl_2000
    PLA
    ASL
    PHP
    BCC :+                      ; If bit 6 is set, then repeat one tile.
    ORA #@@
    INY                         ; increment Y index to point to first byte of text.
:
    PLP
    ; If the count was 0 (bottom 6 bits),
    ; then make it 64.
    CLC
    BNE :+
    SEC
:
    ROR
    LSR
    ; We pulled the flags out, and we're left with a count in A.
    ; Move it to X.
    TAX
@Loop:
    BCS :+                      ; If the original bit 6 is clear,
    INY                         ; then increment Y index (not repeating).
:
    LDA (@@), Y
    STA PpuData_2007
    DEX
    BNE @Loop
    PLA                         ; Restore VRAM address high byte.
    ; If we wrote to $3Fxx, then set PPUADDR to $3F00, then $0000.
    ;
    CMP #@@
    BNE @AdvanceSource
    STA PpuAddr_2006
    STX PpuAddr_2006
    STX PpuAddr_2006
    STX PpuAddr_2006
@AdvanceSource:
    ; Advance the source address to one after the last byte read.
    ;
    SEC
    TYA
    ADC @@
    STA @@
    LDA #@@
    ADC @@
    STA @@
TransferTileBuf:
    LDX PpuStatus_2002
    LDY #@@
    LDA (@@), Y                ; Read high byte of VRAM address.
    BPL ContinueTransferTileBuf ; End when we read a negative VRAM address.
    RTS

Mode1TileTransferBuf:
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
    .BYTE @@, @@, @@

ModeFCharsTransferBuf:
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

GanonPaletteRow7TransferBuf:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@

EndingPaletteTransferBuf:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@

BlankTextBoxLines:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@

BlankPersonWares:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@

SubmenuTriforceApexTransferBuf:
    .BYTE @@, @@, @@, @@, @@, @@

SubmenuTriforceBottomTransferBuf:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@

GhostPaletteRow7TransferBuf:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@

GreenBgPaletteRow7TransferBuf:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@

BrownBgPaletteRow7TransferBuf:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@

CaveBgPaletteRowsTransferBuf:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@

CellarAttrsTransferBuf:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@

WhitePaletteBottomHalfTransferBuf:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@

RedArmosPaletteRow7TransferBuf:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@

GleeokPaletteRow7TransferBuf:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@

AquamentusPaletteRow7TransferBuf:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@

OrangeBossPaletteRow7TransferBuf:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@

BombCapacityPriceTextTransferBuf:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@

LifeOrMoneyCostTextTransferBuf:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@

Mode8TextTileBuffer:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@

StatusBarStaticsTransferBuf:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@

InventoryTextTransferBuf:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@

SubmenuBoxesTopsTransferBuf:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@

SubmenuBoxesSidesTransferBuf:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@

SelectedItemBoxBottomTransferBuf:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@

UseBButtonTextTransferBuf:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@

InventoryBoxBottomTransferBuf:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@

SubmenuMapRemainderTransferBuf:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@

SheetMapBottomEdgeTransferBuf:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@

SubmenuAttrs1TransferBuf:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@

SubmenuAttrs2TransferBuf:
    .BYTE @@, @@, @@, @@, @@

BlankBottomRowNT2TransferBuf:
    .BYTE @@, @@, @@, @@, @@

BlankRowTransferBuf:
    .BYTE @@, @@, @@, @@, @@

Mode11DeadLinkPalette:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@

GameOverTransferBuf:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@

Mode11BackgroundPaletteBottomHalfTransferBuf:
    .BYTE @@, @@, @@, @@, @@, @@, @@, @@
    .BYTE @@, @@, @@, @@

Mode11PlayAreaAttrsTopHalfTransferBuf:
    .BYTE @@, @@, @@, @@, @@

Mode11PlayAreaAttrsBottomHalfTransferBuf:
    .BYTE @@, @@, @@, @@, @@

StoryTileAttrTransferBuf:
.INCBIN "dat/StoryTileAttrTransferBuf.dat"

GameTitleTransferBuf:
.INCBIN "dat/GameTitleTransferBuf.dat"


.SEGMENT "BANK_06_ISR"



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


.SEGMENT "BANK_06_VEC"



; Unknown block
    .BYTE @@, @@, @@, @@, @@, @@

