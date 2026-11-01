/* save_serializer_probe.c — in-ROM tests for the NES save file A codec
 * (T-100). Runs only when armed (ROOMROM_DEBUG_PROBE_SELFTEST). Backs up
 * and restores every cell it touches: the NES save block $6000..$652F,
 * the profile Items/World Flags, the slot info $062D..$0651 and the
 * individual cells save/load write. It never touches cart SRAM.
 *
 * Result block at SAVE_SERIALIZER_PROBE_BASE:
 *   [0..1] 'S','V'  [2] version 2  [3] total  [4] passes  [5] pass mask
 */

#include "save_serializer_probe.h"
#include "../save_serializer.h"
#include "platform_abi.h"

#define PROBE  ((volatile unsigned char *)SAVE_SERIALIZER_PROBE_BASE)

#define SLOTINFO_FIRST 0x062Du
#define SLOTINFO_BYTES (0x0652u - 0x062Du)

static const unsigned short k_single_cells[] = {
    0x0016u, 0x0010u, 0x0656u, 0x052Eu, 0x00ACu, 0x066Cu
};
#define SINGLE_COUNT (sizeof(k_single_cells) / sizeof(k_single_cells[0]))

static unsigned char s_block[NES_SAVE_BLOCK_BYTES];
static unsigned char s_items[SAVE_ITEMS_BYTES];
static unsigned char s_flags[SAVE_WORLD_FLAGS_BYTES];
static unsigned char s_slotinfo[SLOTINFO_BYTES];
static unsigned char s_singles[SINGLE_COUNT];

static void backup(void)
{
    unsigned short i;
    for (i = 0u; i < NES_SAVE_BLOCK_BYTES; ++i) s_block[i] = F_ram_6000_X(i);
    for (i = 0u; i < SAVE_ITEMS_BYTES; ++i) s_items[i] = F_ram_0657_R6(i);
    for (i = 0u; i < SAVE_WORLD_FLAGS_BYTES; ++i) s_flags[i] = F_ram_067F_R2(i);
    for (i = 0u; i < SLOTINFO_BYTES; ++i) s_slotinfo[i] = F_ram_062D_R4(i);
    for (i = 0u; i < SINGLE_COUNT; ++i) s_singles[i] = RAM(k_single_cells[i]); /* native_ram bulk:dyn_addr */
}

static void restore(void)
{
    unsigned short i;
    for (i = 0u; i < NES_SAVE_BLOCK_BYTES; ++i) F_ram_6000_X(i) = s_block[i];
    for (i = 0u; i < SAVE_ITEMS_BYTES; ++i) F_ram_0657_R6(i) = s_items[i];
    for (i = 0u; i < SAVE_WORLD_FLAGS_BYTES; ++i) F_ram_067F_R2(i) = s_flags[i];
    for (i = 0u; i < SLOTINFO_BYTES; ++i) F_ram_062D_R4(i) = s_slotinfo[i];
    for (i = 0u; i < SINGLE_COUNT; ++i) RAM(k_single_cells[i]) = s_singles[i]; /* native_ram bulk:dyn_addr */
}

static void seed_profile(unsigned char first)
{
    unsigned short i;
    for (i = 0u; i < SAVE_ITEMS_BYTES; ++i) F_ram_0657_R6(i) = (unsigned char)(first + i);
    for (i = 0u; i < SAVE_WORLD_FLAGS_BYTES; ++i)
        F_ram_067F_R2(i) = (unsigned char)(first ^ i);
}

/* 0: a formatted file validates, is blank and inactive. */
static unsigned char test_format_valid(void)
{
    save_file_a_format(2u);
    return (save_file_a_valid(2u) &&
            F_NES_FILEA_NAME_6012_N == 0x24u &&
            F_NES_FILEA_ACTIVE_6514_N == 0u &&
            F_NES_SLOTINFO_ACTIVE_0635_R == 0u) ? 1u : 0u;
}

/* 1: known vector — blank file sums to 8 x $24 = $0120, stored [hi, lo]. */
static unsigned char test_checksum_vector(void)
{
    save_file_a_format(0u);
    return (save_file_a_checksum(0u) == 0x0120u &&
            F_NES_FILEA_CHECKSUM_6524_N == 0x01u &&
            F_NES_FILEA_CHECKSUM_6525_N == 0x20u) ? 1u : 0u;
}

/* 2: save then load restores items (pre-refill) and world flags. */
/* Failure detail for the round trip: [6] step, [7..8] index, [9] value. */
static unsigned char fail_at(unsigned char step, unsigned short idx, unsigned char val)
{
    PROBE[6] = step;
    PROBE[7] = (unsigned char)(idx >> 8);
    PROBE[8] = (unsigned char)idx;
    PROBE[9] = val;
    return 0u;
}

static unsigned char test_save_load_round_trip(void)
{
    unsigned short i;
    seed_profile(0x10u);
    F_NES_SLOTINFO_QUEST_R = 1u;
    F_NES_SLOTINFO_DEATHS_R = 5u;
    if (!save_file_a_save(1u)) return fail_at(1u, 0u, 0u);
    if (!save_file_a_valid(1u)) return fail_at(2u, 0u, 0u);
    if (F_NES_FILEA_ACTIVE_6513_N != 1u) return fail_at(3u, 0u, F_NES_FILEA_ACTIVE_6513_N);
    if (F_NES_FILEA_QUEST_N != 1u) return fail_at(4u, 0u, F_NES_FILEA_QUEST_N);
    if (F_NES_FILEA_DEATHS_N != 5u) return fail_at(5u, 0u, F_NES_FILEA_DEATHS_N);
    seed_profile(0xA0u);
    save_file_a_load(1u);
    for (i = 0u; i < SAVE_ITEMS_BYTES; ++i) {
        /* Items+$15 = InvClock $066C: @ChoseSlot clears it after the copy. */
        unsigned char want = (i == 0x15u) ? 0u : (unsigned char)(0x10u + i);
        if (F_ram_0657_R6(i) != want)
            return fail_at(6u, i, F_ram_0657_R6(i));
    }
    for (i = 0u; i < SAVE_WORLD_FLAGS_BYTES; ++i)
        if (F_ram_067F_R2(i) != (unsigned char)(0x10u ^ i))
            return fail_at(7u, i, F_ram_067F_R2(i));
    if (F_ram_0016_R3 != 1u) return fail_at(8u, 0u, F_ram_0016_R3);
    return 1u;
}

/* 3: one flipped world-flag byte fails validation; boot formats it. */
static unsigned char test_corrupt_rejected(void)
{
    seed_profile(0x33u);
    if (!save_file_a_save(1u)) return 0u;
    F_NES_FILEA_FLAGS_N ^= 0x01u;
    if (save_file_a_valid(1u)) return 0u;
    save_files_boot_validate();
    return (save_file_a_valid(1u) && F_NES_FILEA_ACTIVE_6513_N == 0u &&
            F_NES_SLOTINFO_ACTIVE_0634_R == 0u) ? 1u : 0u;
}

/* 4: saving slot 0 leaves slot 2's file A bytes unchanged. */
static unsigned char test_cross_slot_isolation(void)
{
    unsigned short i;
    unsigned char before[SAVE_ITEMS_BYTES];
    seed_profile(0x44u);
    (void)save_file_a_save(2u);
    for (i = 0u; i < SAVE_ITEMS_BYTES; ++i) before[i] = F_NES_FILEA_ITEMS_X(i);
    seed_profile(0x55u);
    (void)save_file_a_save(0u);
    for (i = 0u; i < SAVE_ITEMS_BYTES; ++i)
        if (F_NES_FILEA_ITEMS_X(i) != before[i]) return 0u;
    return save_file_a_valid(2u);
}

void save_serializer_probe_run(void)
{
    unsigned char passes = 0u;
    unsigned char bits = 0u;
    unsigned char k;
    unsigned char (*const tests[5])(void) = {
        test_format_valid, test_checksum_vector, test_save_load_round_trip,
        test_corrupt_rejected, test_cross_slot_isolation
    };

    PROBE[0] = 'S';
    PROBE[1] = 'V';
    PROBE[2] = 0x02u;
    PROBE[6] = 0u; PROBE[7] = 0u; PROBE[8] = 0u; PROBE[9] = 0u;
    backup();
    for (k = 0u; k < 5u; ++k) {
        if (tests[k]()) { ++passes; bits = (unsigned char)(bits | (1u << k)); }
        restore();
    }
    PROBE[3] = 5u;
    PROBE[4] = passes;
    PROBE[5] = bits;
}
