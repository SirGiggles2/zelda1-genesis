/* save_serializer.h — NES save file A codec (T-100).
 *
 * The Genesis save format IS the NES format: the NES SaveRAM block
 * $6000..$652F (three "file A" records plus markers, checksums and file B
 * commit flags) lives in the NES RAM mirror at the same offsets the NES
 * uses, and save_game.c persists that block byte-for-byte to cart SRAM.
 * Every function here is a port of the NES routine named beside it.
 *
 * NES source: reference/aldonunez/Z_01.asm SaveFileAAddressSets /
 * FetchFileAAddressSet; Z_02.asm CalculateFileAChecksum, FormatFileA,
 * UpdateMode0Demo_Sub1/Sub2, UpdateMode1Menu_Sub1 (@ChoseSlot),
 * UpdateModeDSave_Sub0 + CopyFileBToFileA.
 * Drained C: frontend_runtime.c frontdemo_update_mode0_demo_sub2 (unlinked;
 * depends on the unlinked c_shims.asm FormatFileA import). Coverage:
 * FULL for file A; file B is not materialised (see save_file_a_save).
 * Stance: REPLACE the Genesis-only 43-byte format, whose slot images sat on
 * NES $6000+slot*682 (slot 2 overlapped PlayAreaTiles $6530).
 *
 * File A slot s (address sets, 14 bytes each, Z_01.asm):
 *   name   $6002 + 8*s      (8)       items $601A + $28*s (40)
 *   flags  $6092 + $180*s   ($180)    active $6512+s, unknown $6515+s,
 *   deaths $6518+s, quest $651B+s,    markers $651E+s=$5A / $6521+s=$A5,
 *   checksum $6524+2s = [hi, lo] of the 16-bit sum of all of the above,
 *   file B committed $652A+s.
 */
#ifndef SAVE_SERIALIZER_H
#define SAVE_SERIALIZER_H

#ifdef __cplusplus
extern "C" {
#endif

#define SAVE_SLOT_COUNT            3u
#define SAVE_NAME_BYTES            8u
#define SAVE_ITEMS_BYTES           0x28u
#define SAVE_WORLD_FLAGS_BYTES     0x180u

/* Profile (live game) cells, Variables.inc. */
#define NES_PROFILE_ITEMS          0x0657u
#define NES_PROFILE_WORLD_FLAGS    0x067Fu
#define NES_SLOTINFO_NAMES         0x0638u   /* 3 x 8 */
#define NES_SLOTINFO_ACTIVE        0x0633u
#define NES_SLOTINFO_QUEST         0x062Du
#define NES_SLOTINFO_DEATHS        0x0630u
#define NES_SLOTINFO_HEARTS        0x0650u   /* 3 x (value, partial) */
#define NES_CUR_SAVE_SLOT          0x0016u

/* NES SaveRAM block persisted to cart. */
#define NES_SAVE_BLOCK_BASE        0x6000u
#define NES_SAVE_BLOCK_BYTES       0x0530u
#define NES_FILEA_NAME(s)          (0x6002u + 8u * (s))
#define NES_FILEA_ITEMS(s)         (0x601Au + 0x28u * (s))
#define NES_FILEA_FLAGS(s)         (0x6092u + 0x180u * (s))
#define NES_FILEA_ACTIVE(s)        (0x6512u + (s))
#define NES_FILEA_UNKNOWN(s)       (0x6515u + (s))
#define NES_FILEA_DEATHS(s)        (0x6518u + (s))
#define NES_FILEA_QUEST(s)         (0x651Bu + (s))
#define NES_FILEA_OPEN_MARKER(s)   (0x651Eu + (s))
#define NES_FILEA_CLOSE_MARKER(s)  (0x6521u + (s))
#define NES_FILEA_CHECKSUM(s)      (0x6524u + 2u * (s))
#define NES_FILEB_COMMITTED(s)     (0x652Au + (s))
#define SAVE_MAGIC_OPEN            0x5Au
#define SAVE_MAGIC_CLOSE           0xA5u

/* CalculateFileAChecksum: 16-bit sum of name, items, flags, 4 singles. */
unsigned short save_file_a_checksum(unsigned char slot);

/* Mark file B committed, write markers and the checksum of file A as it
 * stands (tail of FormatFileA / CopyFileBToFileA). */
void save_file_a_commit(unsigned char slot);

/* 1 when markers are $5A/$A5 and the stored checksum matches. */
unsigned char save_file_a_valid(unsigned char slot);

/* FormatFileA: blank name ($24), zero items/flags/singles, markers,
 * checksum; resets the slot info (active/quest/deaths) and marks file B
 * committed. */
void save_file_a_format(unsigned char slot);

/* UpdateMode0Demo_Sub1 (file A half) then Sub2: validate/format every
 * file A, then copy active/deaths/quest/hearts/names into the slot info. */
void save_files_boot_validate(void);

/* UpdateMode1Menu_Sub1 @ChoseSlot: file A items + world flags -> profile,
 * clear SwordBlocked/ObjState/InvClock, CurLevel/SelectedItemSlot = 0. */
void save_file_a_load(unsigned char slot);

/* UpdateModeDSave_Sub0 then CopyFileBToFileA, collapsed onto file A:
 * the resulting file A bytes and profile/slot-info side effects are the
 * NES's. Returns 0 on a bad slot. */
unsigned char save_file_a_save(unsigned char slot);

#ifdef __cplusplus
}
#endif

#endif /* SAVE_SERIALIZER_H */
