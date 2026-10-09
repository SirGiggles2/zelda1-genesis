/* save_game.c — NES save block <-> cart SRAM, plus the NES save/load
 * entry points (T-100). The codec is save_serializer.c.
 *
 * Replaces the Genesis-only 43-byte slot format and its $FF6000 staging
 * mirror. Old-format carts fail the NES marker/checksum check at boot and
 * are formatted, exactly as a NES formats a corrupt file.
 */

#include "save_game.h"
#include "save_serializer.h"
#include "platform_abi.h"

extern void sram_nes_save_block_load(volatile unsigned char *dst, unsigned short bytes);
extern void sram_nes_save_block_store(const volatile unsigned char *src, unsigned short bytes);

static void persist(void)
{
    sram_nes_save_block_store(&nes_ram[NES_SAVE_BLOCK_BASE], NES_SAVE_BLOCK_BYTES);
}

void save_game_boot(void)
{
    sram_nes_save_block_load(&nes_ram[NES_SAVE_BLOCK_BASE], NES_SAVE_BLOCK_BYTES);
    save_files_boot_validate();
    /* Z_07.asm InitializeGameOrMode: "Mark Save RAM initialized" ($6001 =
     * $5A; its $7FFF = $A5 partner lies outside the persisted block). */
    nes_ram[0x6001u] = 0x5Au;
    /* The NES formats in battery RAM directly; keep the cart equal to
     * what was validated so a formatted blank slot stays formatted. */
    persist();
}

unsigned char save_game_slot_active(unsigned char slot)
{
    if (slot >= SAVE_SLOT_COUNT) return 0u;
    return RAM(NES_SLOTINFO_ACTIVE + slot) ? 1u : 0u;
}

unsigned char save_game_slot_quest(unsigned char slot)
{
    if (slot >= SAVE_SLOT_COUNT) return 0u;
    return RAM(NES_SLOTINFO_QUEST + slot);
}

unsigned char save_game_load_slot(unsigned char slot)
{
    if (!save_game_slot_active(slot)) return 0u;
    save_file_a_load(slot);
    return 1u;
}

unsigned char save_game_save_current(void)
{
    if (!save_file_a_save(RAM(NES_CUR_SAVE_SLOT))) return 0u;
    persist();
    return 1u;
}

/* NES ZeldaString (Z_02.asm), compared over 5 characters. */
static const unsigned char k_zelda[5] = { 0x23u, 0x0Eu, 0x15u, 0x0Du, 0x0Au };

unsigned char save_game_register(unsigned char slot, const unsigned char *name)
{
    unsigned char i;
    unsigned char blank = 1u;
    unsigned char zelda = 1u;

    if (slot >= SAVE_SLOT_COUNT || save_game_slot_active(slot)) return 0u;
    for (i = 0u; i < SAVE_NAME_BYTES; ++i) {
        RAM(NES_SLOTINFO_NAMES + 8u * slot + i) = name[i];
        if (name[i] != 0x24u) blank = 0u;
    }
    if (blank) return 0u;
    for (i = 0u; i < 5u; ++i) if (name[i] != k_zelda[i]) zelda = 0u;

    /* File B init in UpdateModeERegister, then CopyFileBToFileA. */
    save_file_a_format(slot);
    for (i = 0u; i < SAVE_NAME_BYTES; ++i)
        nes_ram[NES_FILEA_NAME(slot) + i] = name[i];
    nes_ram[NES_FILEA_ITEMS(slot) + 0x18u] = 0x22u;   /* HeartValues */
    nes_ram[NES_FILEA_ITEMS(slot) + 0x19u] = 0xFFu;   /* HeartPartial */
    nes_ram[NES_FILEA_ITEMS(slot) + 0x25u] = 0x08u;   /* MaxBombs */
    nes_ram[NES_FILEA_ACTIVE(slot)] = 1u;
    nes_ram[NES_FILEA_QUEST(slot)] = zelda;
    save_file_a_commit(slot);
    RAM(NES_SLOTINFO_ACTIVE + slot) = 1u;
    RAM(NES_SLOTINFO_QUEST + slot) = zelda;
    RAM(NES_SLOTINFO_DEATHS + slot) = 0u;
    RAM(NES_SLOTINFO_HEARTS + 2u * slot) = 0x22u;
    RAM(NES_SLOTINFO_HEARTS + 2u * slot + 1u) = 0xFFu;
    persist();
    return 1u;
}

void save_game_erase(unsigned char slot)
{
    unsigned char i;
    if (slot >= SAVE_SLOT_COUNT) return;
    save_file_a_format(slot);
    for (i = 0u; i < SAVE_NAME_BYTES; ++i)
        RAM(NES_SLOTINFO_NAMES + 8u * slot + i) = 0x24u;
    RAM(NES_SLOTINFO_HEARTS + 2u * slot) = 0u;
    RAM(NES_SLOTINFO_HEARTS + 2u * slot + 1u) = 0u;
    persist();
}

unsigned char save_game_copy(unsigned char src, unsigned char dst)
{
    unsigned short i;
    if (src >= SAVE_SLOT_COUNT || dst >= SAVE_SLOT_COUNT || src == dst) return 0u;
    if (!save_game_slot_active(src)) return 0u;
    for (i = 0u; i < SAVE_NAME_BYTES; ++i)
        nes_ram[NES_FILEA_NAME(dst) + i] = nes_ram[NES_FILEA_NAME(src) + i];
    for (i = 0u; i < SAVE_ITEMS_BYTES; ++i)
        nes_ram[NES_FILEA_ITEMS(dst) + i] = nes_ram[NES_FILEA_ITEMS(src) + i];
    for (i = 0u; i < SAVE_WORLD_FLAGS_BYTES; ++i)
        nes_ram[NES_FILEA_FLAGS(dst) + i] = nes_ram[NES_FILEA_FLAGS(src) + i];
    nes_ram[NES_FILEA_ACTIVE(dst)]  = nes_ram[NES_FILEA_ACTIVE(src)];
    nes_ram[NES_FILEA_UNKNOWN(dst)] = nes_ram[NES_FILEA_UNKNOWN(src)];
    nes_ram[NES_FILEA_DEATHS(dst)]  = nes_ram[NES_FILEA_DEATHS(src)];
    nes_ram[NES_FILEA_QUEST(dst)]   = nes_ram[NES_FILEA_QUEST(src)];
    save_file_a_commit(dst);
    for (i = 0u; i < SAVE_NAME_BYTES; ++i)
        RAM(NES_SLOTINFO_NAMES + 8u * dst + i) = RAM(NES_SLOTINFO_NAMES + 8u * src + i);
    RAM(NES_SLOTINFO_ACTIVE + dst) = RAM(NES_SLOTINFO_ACTIVE + src);
    RAM(NES_SLOTINFO_QUEST + dst)  = RAM(NES_SLOTINFO_QUEST + src);
    RAM(NES_SLOTINFO_DEATHS + dst) = RAM(NES_SLOTINFO_DEATHS + src);
    RAM(NES_SLOTINFO_HEARTS + 2u * dst)      = RAM(NES_SLOTINFO_HEARTS + 2u * src);
    RAM(NES_SLOTINFO_HEARTS + 2u * dst + 1u) = RAM(NES_SLOTINFO_HEARTS + 2u * src + 1u);
    persist();
    return 1u;
}

const volatile unsigned char *save_game_slot_name(unsigned char slot)
{
    if (slot >= SAVE_SLOT_COUNT) slot = 0u;
    return &nes_ram[NES_SLOTINFO_NAMES + 8u * slot];
}

unsigned char save_game_slot_hearts(unsigned char slot)
{
    return (slot < SAVE_SLOT_COUNT) ? RAM(NES_SLOTINFO_HEARTS + 2u * slot) : 0u;
}

unsigned char save_game_slot_heart_partial(unsigned char slot)
{
    return (slot < SAVE_SLOT_COUNT) ? RAM(NES_SLOTINFO_HEARTS + 2u * slot + 1u) : 0u;
}

unsigned char save_game_slot_deaths(unsigned char slot)
{
    return (slot < SAVE_SLOT_COUNT) ? RAM(NES_SLOTINFO_DEATHS + slot) : 0u;
}

/* File Select occupancy (overrides the weak default in fs_render.c). */
unsigned char fs_sram_slot_occupied(unsigned char slot)
{
    return save_game_slot_active(slot);
}
