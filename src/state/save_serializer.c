/* save_serializer.c — NES save file A codec. See save_serializer.h. */

#include "save_serializer.h"
#include "platform_abi.h"

#define NES_HEART_VALUES    0x066Fu
#define NES_HEART_PARTIAL   0x0670u
#define NES_SWORD_BLOCKED   0x052Eu
#define NES_OBJ_STATE_LINK  0x00ACu
#define NES_INV_CLOCK       0x066Cu
#define NES_CUR_LEVEL       0x0010u
#define NES_SELECTED_ITEM   0x0656u
#define NES_TILE_SPACE      0x24u

unsigned short save_file_a_checksum(unsigned char slot)
{
    unsigned short sum = 0u;
    unsigned short i;
    for (i = 0u; i < SAVE_NAME_BYTES; ++i)
        sum = (unsigned short)(sum + nes_ram[NES_FILEA_NAME(slot) + i]);
    for (i = 0u; i < SAVE_ITEMS_BYTES; ++i)
        sum = (unsigned short)(sum + nes_ram[NES_FILEA_ITEMS(slot) + i]);
    for (i = 0u; i < SAVE_WORLD_FLAGS_BYTES; ++i)
        sum = (unsigned short)(sum + nes_ram[NES_FILEA_FLAGS(slot) + i]);
    sum = (unsigned short)(sum + nes_ram[NES_FILEA_ACTIVE(slot)]);
    sum = (unsigned short)(sum + nes_ram[NES_FILEA_UNKNOWN(slot)]);
    sum = (unsigned short)(sum + nes_ram[NES_FILEA_DEATHS(slot)]);
    sum = (unsigned short)(sum + nes_ram[NES_FILEA_QUEST(slot)]);
    return sum;
}

void save_file_a_commit(unsigned char slot)
{
    unsigned short sum = save_file_a_checksum(slot);
    nes_ram[NES_FILEB_COMMITTED(slot)] = 0xFFu;
    nes_ram[NES_FILEA_OPEN_MARKER(slot)] = SAVE_MAGIC_OPEN;
    nes_ram[NES_FILEA_CLOSE_MARKER(slot)] = SAVE_MAGIC_CLOSE;
    nes_ram[NES_FILEA_CHECKSUM(slot)] = (unsigned char)(sum >> 8);       /* [$0E] */
    nes_ram[NES_FILEA_CHECKSUM(slot) + 1u] = (unsigned char)(sum & 0xFFu); /* [$0F] */
}

unsigned char save_file_a_valid(unsigned char slot)
{
    unsigned short sum;
    if (slot >= SAVE_SLOT_COUNT) return 0u;
    if (nes_ram[NES_FILEA_OPEN_MARKER(slot)] != SAVE_MAGIC_OPEN) return 0u;
    if (nes_ram[NES_FILEA_CLOSE_MARKER(slot)] != SAVE_MAGIC_CLOSE) return 0u;
    sum = save_file_a_checksum(slot);
    return (nes_ram[NES_FILEA_CHECKSUM(slot)] == (unsigned char)(sum >> 8) &&
            nes_ram[NES_FILEA_CHECKSUM(slot) + 1u] == (unsigned char)(sum & 0xFFu))
        ? 1u : 0u;
}

void save_file_a_format(unsigned char slot)
{
    unsigned short i;
    if (slot >= SAVE_SLOT_COUNT) return;
    for (i = 0u; i < SAVE_NAME_BYTES; ++i)
        nes_ram[NES_FILEA_NAME(slot) + i] = NES_TILE_SPACE;
    for (i = 0u; i < SAVE_ITEMS_BYTES; ++i)
        nes_ram[NES_FILEA_ITEMS(slot) + i] = 0u;
    for (i = 0u; i < SAVE_WORLD_FLAGS_BYTES; ++i)
        nes_ram[NES_FILEA_FLAGS(slot) + i] = 0u;
    nes_ram[NES_FILEA_ACTIVE(slot)] = 0u;
    nes_ram[NES_FILEA_UNKNOWN(slot)] = 0u;
    nes_ram[NES_FILEA_DEATHS(slot)] = 0u;
    nes_ram[NES_FILEA_QUEST(slot)] = 0u;
    RAM(NES_SLOTINFO_ACTIVE + slot) = 0u;
    RAM(NES_SLOTINFO_QUEST + slot) = 0u;
    RAM(NES_SLOTINFO_DEATHS + slot) = 0u;
    save_file_a_commit(slot);
}

void save_files_boot_validate(void)
{
    unsigned char s;
    unsigned char i;

    /* Sub1, file A half. File B is never left uncommitted by this port
     * (save_file_a_save commits in one step), so the NES "copy valid
     * uncommitted B to A" branch has no input to act on. */
    for (s = 0u; s < SAVE_SLOT_COUNT; ++s) {
        if (!save_file_a_valid(s)) save_file_a_format(s);
    }

    /* Sub2: slot info. Inactive files are re-formatted (NES does too). */
    for (s = 0u; s < SAVE_SLOT_COUNT; ++s) {
        unsigned char active = nes_ram[NES_FILEA_ACTIVE(s)];
        RAM(NES_SLOTINFO_ACTIVE + s) = active;
        if (active == 0u) save_file_a_format(s);
        RAM(NES_SLOTINFO_DEATHS + s) = nes_ram[NES_FILEA_DEATHS(s)];
        RAM(NES_SLOTINFO_QUEST + s) = nes_ram[NES_FILEA_QUEST(s)];
    }
    for (s = 0u; s < SAVE_SLOT_COUNT; ++s) {
        unsigned char hv = nes_ram[NES_FILEA_ITEMS(s) + 0x18u];   /* HeartValues */
        unsigned char hi = (unsigned char)(hv & 0xF0u);
        RAM(NES_SLOTINFO_HEARTS + 2u * s) = (unsigned char)(hi | (hi >> 4));
        RAM(NES_SLOTINFO_HEARTS + 2u * s + 1u) =
            nes_ram[NES_FILEA_ITEMS(s) + 0x19u];                   /* HeartPartial */
    }
    for (i = 0u; i < 3u * SAVE_NAME_BYTES; ++i)
        RAM(NES_SLOTINFO_NAMES + i) = nes_ram[NES_FILEA_NAME(0) + i];
}

void save_file_a_load(unsigned char slot)
{
    unsigned short i;
    if (slot >= SAVE_SLOT_COUNT) return;
    RAM(NES_CUR_SAVE_SLOT) = slot;
    RAM(NES_CUR_LEVEL) = 0u;
    RAM(NES_SELECTED_ITEM) = 0u;
    for (i = 0u; i < SAVE_ITEMS_BYTES; ++i)
        RAM(NES_PROFILE_ITEMS + i) = nes_ram[NES_FILEA_ITEMS(slot) + i];
    RAM(NES_SWORD_BLOCKED) = 0u;
    RAM(NES_OBJ_STATE_LINK) = 0u;
    RAM(NES_INV_CLOCK) = 0u;
    for (i = 0u; i < SAVE_WORLD_FLAGS_BYTES; ++i)
        RAM(NES_PROFILE_WORLD_FLAGS + i) = nes_ram[NES_FILEA_FLAGS(slot) + i];
}

unsigned char save_file_a_save(unsigned char slot)
{
    unsigned short i;
    unsigned char hi;
    if (slot >= SAVE_SLOT_COUNT) return 0u;

    /* Sub0: Items copied BEFORE the heart refill below (NES order). */
    for (i = 0u; i < SAVE_ITEMS_BYTES; ++i)
        nes_ram[NES_FILEA_ITEMS(slot) + i] = RAM(NES_PROFILE_ITEMS + i);
    nes_ram[NES_FILEA_DEATHS(slot)] = RAM(NES_SLOTINFO_DEATHS + slot);
    nes_ram[NES_FILEA_ACTIVE(slot)] = 1u;
    RAM(NES_SLOTINFO_ACTIVE + slot) = 1u;
    nes_ram[NES_FILEA_QUEST(slot)] = RAM(NES_SLOTINFO_QUEST + slot);
    nes_ram[NES_FILEA_UNKNOWN(slot)] = 0u;   /* file B [C8] stays formatted 0 */
    for (i = 0u; i < SAVE_NAME_BYTES; ++i)
        nes_ram[NES_FILEA_NAME(slot) + i] = RAM(NES_SLOTINFO_NAMES + 8u * slot + i);

    /* Profile side effect: full hearts, then StoreSaveSlotHearts. */
    hi = (unsigned char)(RAM(NES_HEART_VALUES) & 0xF0u);
    RAM(NES_HEART_VALUES) = (unsigned char)(hi | (hi >> 4));
    RAM(NES_HEART_PARTIAL) = 0xFFu;
    RAM(NES_SLOTINFO_HEARTS + 2u * slot) = RAM(NES_HEART_VALUES);
    RAM(NES_SLOTINFO_HEARTS + 2u * slot + 1u) = RAM(NES_HEART_PARTIAL);

    for (i = 0u; i < SAVE_WORLD_FLAGS_BYTES; ++i)
        nes_ram[NES_FILEA_FLAGS(slot) + i] = RAM(NES_PROFILE_WORLD_FLAGS + i);

    /* CopyFileBToFileA tail: slot info from file A, markers, checksum,
     * file B committed. */
    RAM(NES_SLOTINFO_QUEST + slot) = nes_ram[NES_FILEA_QUEST(slot)];
    RAM(NES_SLOTINFO_DEATHS + slot) = nes_ram[NES_FILEA_DEATHS(slot)];
    save_file_a_commit(slot);
    return 1u;
}
