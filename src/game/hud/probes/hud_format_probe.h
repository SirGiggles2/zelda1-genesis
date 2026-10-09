/* Phase 9 Task 9.5 — HUD format probe (status-bar formatter contract).
 *
 * Probe RAM contract ($FF7EC0..$FF7ECF):
 *   [0]   = 'H' (0x48) magic
 *   [1]   = 'F' (0x46) magic
 *   [2]   = version (4 — adds rupee inventory-edge coverage)
 *   [3]   = total tests run (22)
 *   [4]   = passes
 *   [5]   = bits  0..7  (1 = pass)
 *   [6]   = bits  8..15
 *   [7]   = bits  16..21
 *   [8..15] reserved
 *
 * Tests verify the drained `hud_format_status_bar_text` (src/game/hud/
 * hud_dispatch.c) against hand-traced expected byte sequences over the
 * 41-byte transfer buffer at RAM($0302..$032A). This locks the status
 * bar format contract that any native Genesis-side HUD renderer (Task
 * 9.5 bullets 1-4) must consume unchanged. Group C exercises the
 * animated-tick state machine in `hud_world_change_rupees`.
 *
 * Bit map (Group A — heart row, 8 tests):
 *   bit0: template_loaded (header words + terminator)
 *   bit1: hearts_3_full   (hearts=$33, partial=0)
 *   bit2: hearts_3_max_1_cur (hearts=$31, partial=0)
 *   bit3: hearts_8_max_3_cur_high_partial (hearts=$83, partial=$80)
 *   bit4: hearts_8_max_3_cur_low_partial  (hearts=$83, partial=$40)
 *   bit5: hearts_15_full  (hearts=$FF, partial=0)
 *   bit6: hearts_zero     (hearts=$00, partial=0)
 *   bit7: hearts_7_full   (hearts=$77, partial=0)
 *
 * Bit map (Group B — decimal counters, 7 tests):
 *   bit8:  rupees_42      buf[25..27] = [$21, 4, 2]
 *   bit9:  rupees_0       buf[25..27] = [$21, 0, $24]
 *   bit10: rupees_255     buf[25..27] = [2, 5, 5]
 *   bit11: bombs_8        buf[37..39] = [$21, 8, $24]
 *   bit12: bombs_99       buf[37..39] = [$21, 9, 9]
 *   bit13: keys_5_no_mkey buf[31..33] = [$21, 5, $24], master_key=0
 *   bit14: master_key_dash buf[31..33] = [$21, 10, $24], master_key!=0
 *
 * Bit map (Group C — animated rupee tick, 5 tests):
 *   bit15: anim_buf_select_skip    ROOM_TRANSFER_BUF_SELECT!=0 → no state change
 *   bit16: anim_high_bit_clear_skip TRANSFER_BUF_BYTE(0)&$80==0 → no state change
 *   bit17: anim_odd_frame_skip     FRAME_COUNTER&1 → no state change
 *   bit18: anim_credit_tick        $067D=5,RUPEES=10,FC=0 → $067D=4,RUPEES=11,sfx=16
 *   bit19: anim_debit_tick         $067D=0,$067E=3,RUPEES=10 → $067E=2,RUPEES=9,sfx=16
 *
 * Bit map (Group D — rupee inventory-edge writes, 2 tests):
 *   bit20: anim_zero_clears_delta  RUPEES=0 → INVENTORY_VALUE(39)=0 (clears $067E)
 *   bit21: anim_max_clears_rta     RUPEES=$FF → INVENTORY_VALUE(38)=0 (clears $067D)
 */

#ifndef SRC_GAME_HUD_PROBES_HUD_FORMAT_PROBE_H
#define SRC_GAME_HUD_PROBES_HUD_FORMAT_PROBE_H

#define HUD_FORMAT_PROBE_BASE  0x00FF7EC0UL

#ifdef __cplusplus
extern "C" {
#endif

void hud_format_probe_run(void);

#ifdef __cplusplus
}
#endif

#endif /* SRC_GAME_HUD_PROBES_HUD_FORMAT_PROBE_H */
