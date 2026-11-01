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
        sum = (unsigned short)(sum + F_NES_FILEA_NAME_6002_X(slot, i));
    for (i = 0u; i < SAVE_ITEMS_BYTES; ++i)
        sum = (unsigned short)(sum + F_ram_601A_X2(slot, i));
    for (i = 0u; i < SAVE_WORLD_FLAGS_BYTES; ++i)
        sum = (unsigned short)(sum + F_ram_6092_X2(slot, i));
    sum = (unsigned short)(sum + F_ram_6512_X(slot));
    sum = (unsigned short)(sum + F_ram_6515_X(slot));
    sum = (unsigned short)(sum + F_ram_6518_X(slot));
    sum = (unsigned short)(sum + F_ram_651B_X(slot));
    return sum;
}

void save_file_a_commit(unsigned char slot)
{
    unsigned short sum = save_file_a_checksum(slot);
    F_ram_652A_X(slot) = 0xFFu;
    F_ram_651E_X(slot) = SAVE_MAGIC_OPEN;
    F_ram_6521_X(slot) = SAVE_MAGIC_CLOSE;
    F_NES_FILEA_CHECKSUM_6524_X2(slot) = (unsigned char)(sum >> 8);       /* [$0E] */
    F_NES_FILEA_CHECKSUM_6525_X2(slot) = (unsigned char)(sum & 0xFFu); /* [$0F] */
}

unsigned char save_file_a_valid(unsigned char slot)
{
    unsigned short sum;
    if (slot >= SAVE_SLOT_COUNT) return 0u;
    if (F_ram_651E_X(slot) != SAVE_MAGIC_OPEN) return 0u;
    if (F_ram_6521_X(slot) != SAVE_MAGIC_CLOSE) return 0u;
    sum = save_file_a_checksum(slot);
    return (F_NES_FILEA_CHECKSUM_6524_X2(slot) == (unsigned char)(sum >> 8) &&
            F_NES_FILEA_CHECKSUM_6525_X2(slot) == (unsigned char)(sum & 0xFFu))
        ? 1u : 0u;
}

void save_file_a_format(unsigned char slot)
{
    unsigned short i;
    if (slot >= SAVE_SLOT_COUNT) return;
    for (i = 0u; i < SAVE_NAME_BYTES; ++i)
        F_NES_FILEA_NAME_6002_X(slot, i) = NES_TILE_SPACE;
    for (i = 0u; i < SAVE_ITEMS_BYTES; ++i)
        F_ram_601A_X2(slot, i) = 0u;
    for (i = 0u; i < SAVE_WORLD_FLAGS_BYTES; ++i)
        F_ram_6092_X2(slot, i) = 0u;
    F_ram_6512_X(slot) = 0u;
    F_ram_6515_X(slot) = 0u;
    F_ram_6518_X(slot) = 0u;
    F_ram_651B_X(slot) = 0u;
    F_NES_SLOTINFO_ACTIVE_0633_R(slot) = 0u;
    F_ram_062D_R6(slot) = 0u;
    F_ram_0630_R4(slot) = 0u;
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
        unsigned char active = F_ram_6512_X(s);
        F_NES_SLOTINFO_ACTIVE_0633_R(s) = active;
        if (active == 0u) save_file_a_format(s);
        F_ram_0630_R4(s) = F_ram_6518_X(s);
        F_ram_062D_R6(s) = F_ram_651B_X(s);
    }
    for (s = 0u; s < SAVE_SLOT_COUNT; ++s) {
        unsigned char hv = F_ram_6032_X2(s);   /* HeartValues */
        unsigned char hi = (unsigned char)(hv & 0xF0u);
        F_NES_SLOTINFO_HEARTS_0650_R2(2u * s) = (unsigned char)(hi | (hi >> 4));
        F_NES_SLOTINFO_HEARTS_0651_R2(2u * s) =
            F_ram_6033_X2(s);                   /* HeartPartial */
    }
    for (i = 0u; i < 3u * SAVE_NAME_BYTES; ++i)
        F_NES_SLOTINFO_NAMES_R3(i) = F_NES_FILEA_NAME_6002_X2(i);
}

void save_file_a_load(unsigned char slot)
{
    unsigned short i;
    if (slot >= SAVE_SLOT_COUNT) return;
    F_ram_0016_R3 = slot;
    F_ram_0010_R3 = 0u;
    F_ram_0656_R = 0u;
    for (i = 0u; i < SAVE_ITEMS_BYTES; ++i)
        F_ram_0657_R6(i) = F_ram_601A_X2(slot, i);
    F_ram_052E_R2 = 0u;
    F_ram_00AC_R4 = 0u;
    F_ram_066C_R2 = 0u;
    for (i = 0u; i < SAVE_WORLD_FLAGS_BYTES; ++i)
        F_ram_067F_R2(i) = F_ram_6092_X2(slot, i);
}

unsigned char save_file_a_save(unsigned char slot)
{
    unsigned short i;
    unsigned char hi;
    if (slot >= SAVE_SLOT_COUNT) return 0u;

    /* Sub0: Items copied BEFORE the heart refill below (NES order). */
    for (i = 0u; i < SAVE_ITEMS_BYTES; ++i)
        F_ram_601A_X2(slot, i) = F_ram_0657_R6(i);
    F_ram_6518_X(slot) = F_ram_0630_R4(slot);
    F_ram_6512_X(slot) = 1u;
    F_NES_SLOTINFO_ACTIVE_0633_R(slot) = 1u;
    F_ram_651B_X(slot) = F_ram_062D_R6(slot);
    F_ram_6515_X(slot) = 0u;   /* file B [C8] stays formatted 0 */
    for (i = 0u; i < SAVE_NAME_BYTES; ++i)
        F_NES_FILEA_NAME_6002_X(slot, i) = F_NES_SLOTINFO_NAMES_R2(8u * slot, i);

    /* Profile side effect: full hearts, then StoreSaveSlotHearts. */
    hi = (unsigned char)(F_ram_066F_R3 & 0xF0u);
    F_ram_066F_R3 = (unsigned char)(hi | (hi >> 4));
    F_ram_0670_R3 = 0xFFu;
    F_NES_SLOTINFO_HEARTS_0650_R2(2u * slot) = F_ram_066F_R3;
    F_NES_SLOTINFO_HEARTS_0651_R2(2u * slot) = F_ram_0670_R3;

    for (i = 0u; i < SAVE_WORLD_FLAGS_BYTES; ++i)
        F_ram_6092_X2(slot, i) = F_ram_067F_R2(i);

    /* CopyFileBToFileA tail: slot info from file A, markers, checksum,
     * file B committed. */
    F_ram_062D_R6(slot) = F_ram_651B_X(slot);
    F_ram_0630_R4(slot) = F_ram_6518_X(slot);
    save_file_a_commit(slot);
    return 1u;
}
