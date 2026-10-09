    .text
    .globl main
    .globl debug_set_a4
    .globl debug_get_a4
    .globl debug_main_after_a4

main:
    lea 0x00FF8000,%a4
    jmp debug_main_after_a4

debug_set_a4:
    lea 0x00FF8000,%a4
    rts

debug_get_a4:
    move.l %a4,%d0
    rts
