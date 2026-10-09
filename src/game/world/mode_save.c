/* mode_save.c — GameMode $0D (Save), native body.
 *
 * NES source: reference/aldonunez/Z_02.asm:2779 UpdateModeDSave, reached
 * from UpdateMode_JumpTable entry 13 (Z_07.asm:1627); Z_05.asm:1388
 * InitModeD.
 *
 * NES structure: InitModeD (TurnOffVideoAndClearArtifacts, InitSaveRam,
 * BeginUpdateMode); Sub0 formats file B and copies the profile into it
 * (items, deaths, active, quest, name, world flags; hearts refilled on the
 * profile), Sub1 validates B and copies it to file A, Sub2 goes to
 * GameMode 0 submode 1.
 *
 * T-100: save_game_save_current() runs Sub0 + CopyFileBToFileA collapsed
 * onto file A (save_serializer.c) and commits the whole NES save block to
 * cart SRAM. File A bytes and profile side effects match the NES; the
 * A/B power-loss protocol is replaced by one atomic commit.
 *
 * T-149: Sub2 goes to GameMode 0 submode 1 as on the NES; the Genesis
 * main loop then returns to its File Select.
 */

#include "mode_save.h"
#include "save_game.h"
#include "platform_abi.h"
#include "transfer_buf_drain.h"

/* NES Variables.inc. */
#define MODE_SAVE_IS_UPDATING  RAM(0x0011u)
#define MODE_SAVE_GAME_MODE    RAM(0x0012u)
#define MODE_SAVE_GAME_SUBMODE RAM(0x0013u)

/* Last save result, readable by probes and by any UI that wants to report
 * failure. 0 = no attempt yet, 1 = committed, 2 = refused (bad slot). */
unsigned char g_mode_save_last_result = 0u;

/* NES timing, counted in update ticks k from the first update tick (the
 * NES save routines run across frames):
 *   save_roundtrip (stage sets $0D while IsUpdatingMode is 1, T-149):
 *     rows after k = 0, 1, 2: $0D/1, $0D/1, $00/1 (Sub1 shows at k = 0).
 *   t013_save (from mode 8, InitModeD first, T-013 P2.6, NES t253-t257):
 *     rows after InitModeD, k = 0, 1, 2: $0D/0, $0D/0, $0D/1, $00/1
 *     (Sub1 shows at k = 1).
 * The save ends at k = 2 either way. */
/* NES SaveFileBAddressSets (Z_02.asm:1281): per-slot [$C0..$CD] pointer
 * sets of the save routines (Items, World Flags, Unknown, Name, ...). */
static const unsigned char k_file_b_addr_sets[3][14] = {
    { 0x98u,0x68u,0x10u,0x69u,0x80u,0x68u,0x90u,0x6Du,0x93u,0x6Du,0x96u,0x6Du,0x99u,0x6Du },
    { 0xC0u,0x68u,0x90u,0x6Au,0x88u,0x68u,0x91u,0x6Du,0x94u,0x6Du,0x97u,0x6Du,0x9Au,0x6Du },
    { 0xE8u,0x68u,0x10u,0x6Cu,0x90u,0x68u,0x92u,0x6Du,0x95u,0x6Du,0x98u,0x6Du,0x9Bu,0x6Du },
};

/* The NES save keeps its file pointers in $C0-$CF (ObjShoveDir cells):
 * after UpdateModeDSave they hold the slot's address set with the World
 * Flags pointer [$C2:C3] advanced $180, and the checksum in [$CE:CF].
 * InitMode_EnterRoom clears $C0-$CB later; $CC-$CF stay into play
 * (T-171: t171_save_resume_nes t257/t378). */
static void save_pointer_scratch(void)
{
    const unsigned char slot = (unsigned char)RAM(0x0016u);
    unsigned char i;
    unsigned short wf;
    if (slot >= 3u) return;
    for (i = 0u; i < 14u; ++i) RAM(0x00C0u + i) = k_file_b_addr_sets[slot][i];
    wf = (unsigned short)(((unsigned short)RAM(0x00C3u) << 8) | RAM(0x00C2u));
    wf = (unsigned short)(wf + 0x180u);
    RAM(0x00C2u) = (unsigned char)wf;
    RAM(0x00C3u) = (unsigned char)(wf >> 8);
    RAM(0x00CEu) = nes_ram[0x6524u + 2u * slot];
    RAM(0x00CFu) = nes_ram[0x6525u + 2u * slot];
}

static unsigned char s_running;
static unsigned char s_k;
static unsigned char s_after_init;

extern void roomrom_mode8_blank(void);   /* TurnOffVideoAndClearArtifacts */

void mode13_save_update(void)
{
    if (MODE_SAVE_IS_UPDATING == 0u) {
        /* InitModeD. InitSaveRam only resets the console when the save
         * files fail validation; the Genesis save block is validated by
         * save_game_boot / save_game_save_current. */
        roomrom_mode8_blank();
        transfer_buf_nt_cleared();
        MODE_SAVE_GAME_SUBMODE = 0u;
        MODE_SAVE_IS_UPDATING = 1u;
        s_after_init = 1u;
        s_running = 0u;
        return;
    }
    if (!s_running) {
        /* Sub0 + CopyFileBToFileA collapsed onto file A (T-100). */
        s_running = 1u;
        s_k = 0u;
        g_mode_save_last_result = save_game_save_current() ? 1u : 2u;
        save_pointer_scratch();
    } else {
        ++s_k;
    }
    if (s_k == (s_after_init ? 1u : 0u)) MODE_SAVE_GAME_SUBMODE = 1u;
    if (s_k < 2u) return;
    s_running = 0u;
    s_after_init = 0u;
    /* UpdateModeDSave_Sub2: GameMode 0 submode 1 (the title's save
     * validation, then the menu); the Genesis front end takes over there
     * (a4_probe_main.c). */
    MODE_SAVE_GAME_MODE = 0u;
    MODE_SAVE_GAME_SUBMODE = 1u;
}
