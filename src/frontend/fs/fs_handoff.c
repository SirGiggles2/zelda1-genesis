/* src/fs_handoff.c -- VDP teardown + jump to ASM trampoline.
 * S1.F4: vdp_* calls replaced with render_* API.
 */
#include "fs_handoff.h"
#include "render_abi.h"
#include "platform_abi.h"

extern void fs_to_transpiled_trampoline(void);

/* Set when the player commits to a slot. Polled by the host loop; the
 * File Select itself never jumps into gameplay. */
unsigned char g_fs_handoff_requested = 0u;
unsigned char g_fs_handoff_slot      = 0u;

#ifdef OW_DEBUG_ENTRY
extern void ow_debug_entry(unsigned char room_id);
#endif

static void clear_plane(unsigned short plane_base) {
    unsigned short zero_row[32];
    unsigned short i;
    for (i = 0; i < 32; i++) zero_row[i] = 0;
    for (i = 0; i < 32; i++) {
        render_plane_write_row(plane_base, i, zero_row, 32u);
    }
}

void fs_handoff_to_transpiled(uint8_t slot) {
    DBG_SENTINEL(0x12u) = 0xCC;   /* probe: fs handoff begun */

    render_display_enable(0);
    clear_plane(0xC000);
    clear_plane(0xE000);
    render_mode_set_v64();
    render_vscroll_set(0);

    /* Seed CurSaveSlot ($0016) directly here -- m68k SysV byte-arg ABI is
     * unreliable, so the trampoline takes no args and reads from RAM. */
    nes_ram[0x0016] = slot;

    /* T-100: the real NES save block is validated at title Start
     * (save_game_boot). The former fake markers / IsSaveSlotActive[0]=1
     * seeding here existed for the retired transpiled File Select and
     * would now mark empty slots active; removed. */

    /* Hand control back to the host instead of jumping away.
     *
     * The two original exits do not work in Zelda.md:
     *   fs_to_transpiled_trampoline lives in genesis_shell.asm, which is
     *   not linked (that whole transpiled path is retired), and
     *   ow_debug_entry only renders a static room and spins forever —
     *   it is not a gameplay entry and is also unlinked.
     *
     * So this records the request and returns. src/platform/game_main.c
     * polls it and performs the actual entry into the gameplay runtime.
     * Keeping the jump out of here means src/frontend/ stays free of any
     * dependency on src/game/ or engine/, which is the direction WT-3
     * cares about.
     *
     * CurSaveSlot ($0016) is already seeded above, so whatever the host
     * starts sees the slot the player chose — and Mode $0D (Save) reads
     * that same cell when committing. */
    g_fs_handoff_slot      = slot;
    g_fs_handoff_requested = 1u;
}
