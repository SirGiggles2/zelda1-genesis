.INCLUDE "Variables.inc"

.SEGMENT "BANK_03_00"


; Imports from program bank 07

.IMPORT TurnOffAllVideo

.EXPORT TransferLevelPatternBlocks

LevelPatternBlockSrcAddrs:
    .ADDR PatternBlockUWSP127
    .ADDR PatternBlockUWSP127
    .ADDR PatternBlockUWSP127
    .ADDR PatternBlockUWSP358
    .ADDR PatternBlockUWSP469
    .ADDR PatternBlockUWSP358
    .ADDR PatternBlockUWSP469
    .ADDR PatternBlockUWSP127
    .ADDR PatternBlockUWSP358
    .ADDR PatternBlockUWSP469

BossPatternBlockSrcAddrs:
    .ADDR PatternBlockUWSPBoss1257
    .ADDR PatternBlockUWSPBoss1257
    .ADDR PatternBlockUWSPBoss1257
    .ADDR PatternBlockUWSPBoss3468
    .ADDR PatternBlockUWSPBoss3468
    .ADDR PatternBlockUWSPBoss1257
    .ADDR PatternBlockUWSPBoss3468
    .ADDR PatternBlockUWSPBoss1257
    .ADDR PatternBlockUWSPBoss3468
    .ADDR PatternBlockUWSPBoss9

PatternBlockSrcAddrsUW:
    .ADDR PatternBlockUWBG
    .ADDR PatternBlockUWSP

PatternBlockSrcAddrsOW:
    .ADDR PatternBlockOWBG
    .ADDR PatternBlockOWSP

PatternBlockPpuAddrs:
    .DBYT @@
    .DBYT @@

PatternBlockPpuAddrsExtra:
    .DBYT @@
    .DBYT @@

PatternBlockSizesOW:
    .DBYT @@
    .DBYT @@

PatternBlockSizesUW:
    .DBYT @@
    .DBYT @@
    .DBYT @@
    .DBYT @@

TransferLevelPatternBlocks:
    JSR TurnOffAllVideo
    LDA PpuStatus_2002
    JSR ResetPatternBlockIndex
    LDA CurLevel
    BNE TransferLevelPatternBlocksUW    ; Go handle UW levels.
@LoopBlockOW:
    JSR FetchPatternBlockInfoOW
    JSR TransferPatternBlock_Bank3
    LDA PatternBlockIndex
    CMP #@@                    ; There are two blocks.
    BNE @LoopBlockOW            ; If we haven't transferred the second, then go do so.
ResetPatternBlockIndex:
    LDA #@@
    STA PatternBlockIndex
    RTS

TransferLevelPatternBlocksUW:
    JSR FetchPatternBlockAddrUW
    JSR FetchPatternBlockSizeUW
    LDA PatternBlockIndex
    CMP #@@
    BNE TransferLevelPatternBlocksUW    ; If at block index 1, then go transfer the second block.
    ; At this point, we've transferred two common blocks
    ; (BG and sprites). Now UW, transfer bosses and other
    ; specialized sprite patterns.
    ;
    JSR FetchPatternBlockAddrUWSpecial
    JSR FetchPatternBlockSizeUW
    JSR FetchPatternBlockUWBoss
    JSR FetchPatternBlockSizeUW
    JMP ResetPatternBlockIndex

FetchPatternBlockAddrUW:
    LDA PatternBlockIndex
    ASL
    TAX
    LDA PatternBlockSrcAddrsUW, X
    STA @@
    INX
    LDA PatternBlockSrcAddrsUW, X
    STA @@
    RTS

; Returns:
; [$00:01]: source address
; [$03:02]: size
;
FetchPatternBlockInfoOW:
    LDA PatternBlockIndex
    ASL
    TAX
    LDA PatternBlockSrcAddrsOW, X
    STA @@
    LDA PatternBlockSizesOW, X
    STA @@
    INX
    LDA PatternBlockSrcAddrsOW, X
    STA @@
    LDA PatternBlockSizesOW, X
    STA @@
    RTS

FetchPatternBlockAddrUWSpecial:
    LDA CurLevel
    ASL
    TAX
    LDA LevelPatternBlockSrcAddrs, X
    STA @@
    INX
    LDA LevelPatternBlockSrcAddrs, X
    STA @@
    RTS

FetchPatternBlockUWBoss:
    LDA CurLevel
    ASL
    TAX
    LDA BossPatternBlockSrcAddrs, X
    STA @@
    INX
    LDA BossPatternBlockSrcAddrs, X
    STA @@
    RTS

FetchPatternBlockSizeUW:
    LDA PatternBlockIndex
    ASL
    TAX
    LDA PatternBlockSizesUW, X
    STA @@
    INX
    LDA PatternBlockSizesUW, X
    STA @@
; Params:
; [$00:01]: source address
; [$03:02]: size
;
; Look up and transfer destination PPU address by PatternBlockIndex.
;
TransferPatternBlock_Bank3:
    LDA PatternBlockIndex
    ASL
    TAX
    LDA PatternBlockPpuAddrs, X
    STA PpuAddr_2006
    INX
    LDA PatternBlockPpuAddrs, X
    STA PpuAddr_2006
    LDY #@@                    ; Start copying.
@LoopCopy:
    LDA (@@), Y                ; Transfer 1 byte from source pattern block in ROM to PPU.
    STA PpuData_2007
    ; Increment source address.
    ;
    LDA @@
    CLC
    ADC #@@
    STA @@
    LDA @@
    ADC #@@
    STA @@
    ; Decrement count.
    ;
    LDA @@
    SEC
    SBC #@@
    STA @@
    LDA @@
    SBC #@@
    STA @@
    ; If count is not zero, go copy more.
    ;
    LDA @@
    BNE @LoopCopy
    LDA @@
    BNE @LoopCopy
    INC PatternBlockIndex       ; Mark this block finished, and we're ready for the next one.
    RTS

PatternBlockUWBG:
.INCBIN "dat/PatternBlockUWBG.dat"

PatternBlockOWBG:
.INCBIN "dat/PatternBlockOWBG.dat"

PatternBlockOWSP:
.INCBIN "dat/PatternBlockOWSP.dat"

PatternBlockUWSP358:
.INCBIN "dat/PatternBlockUWSP358.dat"

PatternBlockUWSP469:
.INCBIN "dat/PatternBlockUWSP469.dat"

PatternBlockUWSP:
.INCBIN "dat/PatternBlockUWSP.dat"

PatternBlockUWSP127:
.INCBIN "dat/PatternBlockUWSP127.dat"

PatternBlockUWSPBoss1257:
.INCBIN "dat/PatternBlockUWSPBoss1257.dat"

PatternBlockUWSPBoss3468:
.INCBIN "dat/PatternBlockUWSPBoss3468.dat"

PatternBlockUWSPBoss9:
.INCBIN "dat/PatternBlockUWSPBoss9.dat"


.SEGMENT "BANK_03_ISR"



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


.SEGMENT "BANK_03_VEC"



; Unknown block
    .BYTE @@, @@, @@, @@, @@, @@

