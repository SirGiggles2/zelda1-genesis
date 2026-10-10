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
#include "../game/options/options_runtime.h"

extern void sram_options_io_read_at(unsigned int offset, unsigned char *buf, unsigned int size);
extern void sram_options_io_write_at(unsigned int offset, const unsigned char *buf, unsigned int size);
extern void sram_nes_save_block_load(volatile unsigned char *dst, unsigned short bytes);
extern void sram_nes_save_block_store(const volatile unsigned char *src, unsigned short bytes);
extern unsigned char options_persistence_load_or_default(void);

/* MD file extension: SRAM $0900..$14FF, three 1KB records.
 * Existing NES bytes/options remain in their original ranges. No extra
 * work-RAM mirror: each quest is streamed through the adapter in the menu.
 * Header[8]: M,D,version,mode,completion/exception flags,players,reserved,xor.
 * Options[32] at +16. Quest records at +64/+496: marker, 425 payload
 * bytes (40 items,384 world flags,death count), 16-bit additive checksum.
 * Marker is written last. Headers and quest records validate separately. */
#define MD_BASE(s) (0x900u + 0x400u * (s))
#define MD_QUEST(s,q) (MD_BASE(s) + 64u + 432u * (q))
static unsigned char ext_read(unsigned short a) {
    unsigned char v; sram_options_io_read_at(a,&v,1u); return v;
}
static void ext_write(unsigned short a,unsigned char v) {
    sram_options_io_write_at(a,&v,1u);
}
static unsigned char header_valid(unsigned char slot) {
    unsigned char b[8], x=0u;
    sram_options_io_read_at(MD_BASE(slot),b,8u);
    for (unsigned char i=0u;i<8u;i++) x^=b[i];
    return b[0]=='M' && b[1]=='D' && b[2]==1u && b[3]<=1u && !(b[4]&0xF0u) && b[5]<=4u && !x;
}
static void header_write(unsigned char slot,unsigned char mode,unsigned char flags) {
    unsigned char b[8]={'M','D',1u,mode,flags,save_game_slot_players(slot),0u,0u};
    for (unsigned char i=0u;i<7u;i++) b[7]^=b[i];
    sram_options_io_write_at(MD_BASE(slot),b,8u);
}
/* Names use the original five-character ZELDA comparison convention.
 * Header flags: Q1 complete=1, Q2 complete=2, Q2 exception=4,
 * earned/name Gauntlet access=8. Access never fabricates completion. */
static const unsigned char k_zelda[5]={0x23u,0x0Eu,0x15u,0x0Du,0x0Au};
static const unsigned char k_ganon[5]={0x10u,0x0Au,0x17u,0x18u,0x17u};
static unsigned char name_matches(unsigned char slot,const unsigned char *expected) {
    const volatile unsigned char *name=save_game_slot_name(slot);
    for (unsigned char i=0u;i<5u;i++) if (name[i]!=expected[i]) return 0u;
    return 1u;
}
static void name_unlocks_store(unsigned char slot) {
    unsigned char flags,updated;
    if (!save_game_slot_active(slot) || !header_valid(slot)) return;
    flags=ext_read(MD_BASE(slot)+4u); updated=flags;
    if (name_matches(slot,k_ganon)) updated|=12u;
    else if ((flags&3u) && name_matches(slot,k_zelda)) updated|=8u;
    if (updated!=flags) header_write(slot,save_game_slot_mode(slot),updated);
}
unsigned char save_game_gauntlet_available(unsigned char slot) {
    unsigned char flags;
    if (!save_game_slot_active(slot) || !header_valid(slot)) return 0u;
    flags=ext_read(MD_BASE(slot)+4u);
    return (flags&3u)==3u || (flags&8u)!=0u;
}
static unsigned char quest_byte(unsigned char slot,unsigned short i) {
    if (i<40u) return nes_ram[NES_FILEA_ITEMS(slot)+i];
    if (i<424u) return nes_ram[NES_FILEA_FLAGS(slot)+i-40u];
    return nes_ram[NES_FILEA_DEATHS(slot)];
}
static void quest_store(unsigned char slot,unsigned char q) {
    unsigned short a=MD_QUEST(slot,q),sum=0u;
    ext_write(a,0u);
    for (unsigned short i=0u;i<425u;i++) {
        unsigned char v=quest_byte(slot,i); sum+=v; ext_write(a+1u+i,v);
    }
    ext_write(a+426u,(unsigned char)(sum>>8)); ext_write(a+427u,(unsigned char)sum);
    ext_write(a,0xA5u);
}
static unsigned char quest_valid(unsigned char slot,unsigned char q) {
    unsigned short a=MD_QUEST(slot,q),sum=0u;
    if (ext_read(a)!=0xA5u) return 0u;
    for (unsigned short i=0u;i<425u;i++) sum+=ext_read(a+1u+i);
    return (unsigned char)(sum>>8)==ext_read(a+426u) && (unsigned char)sum==ext_read(a+427u);
}
static void quest_restore(unsigned char slot,unsigned char q) {
    unsigned short a=MD_QUEST(slot,q);
    unsigned char valid=quest_valid(slot,q);
    for (unsigned short i=0u;i<40u;i++)
        nes_ram[NES_FILEA_ITEMS(slot)+i]=valid ? ext_read(a+1u+i) :
            i==0x18u ? 0x22u : i==0x19u ? 0xFFu : i==0x25u ? 8u : 0u;
    for (unsigned short i=0u;i<384u;i++)
        nes_ram[NES_FILEA_FLAGS(slot)+i]=valid ? ext_read(a+41u+i) : 0u;
    nes_ram[NES_FILEA_DEATHS(slot)]=valid ? ext_read(a+425u) : 0u;
    nes_ram[NES_FILEA_QUEST(slot)]=RAM(NES_SLOTINFO_QUEST+slot)=q;
    RAM(NES_SLOTINFO_DEATHS+slot)=nes_ram[NES_FILEA_DEATHS(slot)];
    RAM(NES_SLOTINFO_HEARTS+2u*slot)=nes_ram[NES_FILEA_ITEMS(slot)+0x18u];
    RAM(NES_SLOTINFO_HEARTS+2u*slot+1u)=nes_ram[NES_FILEA_ITEMS(slot)+0x19u];
    save_file_a_commit(slot);
}
unsigned char save_game_slot_mode(unsigned char slot) {
    return slot<SAVE_SLOT_COUNT && header_valid(slot) ? ext_read(MD_BASE(slot)+3u) : 0u;
}
/* Header byte 5 was reserved in older files: zero means one player. */
unsigned char save_game_slot_players(unsigned char slot) {
    unsigned char n;
    if (slot>=SAVE_SLOT_COUNT || !header_valid(slot)) return 1u;
    n=ext_read(MD_BASE(slot)+5u);
    return n ? n : 1u;
}
void save_game_set_players(unsigned char slot,unsigned char players) {
    unsigned char b[8];
    if (!save_game_slot_active(slot) || !header_valid(slot) || players<1u || players>4u) return;
    sram_options_io_read_at(MD_BASE(slot),b,8u);
    b[5]=players; b[7]=0u;
    for (unsigned char i=0u;i<7u;i++) b[7]^=b[i];
    sram_options_io_write_at(MD_BASE(slot),b,8u);
}
void save_game_options_store(unsigned char slot) {
    unsigned char b[32];
    if (slot>=SAVE_SLOT_COUNT) return;
    options_runtime_serialize(b,32u); sram_options_io_write_at(MD_BASE(slot)+16u,b,32u);
}
void save_game_options_load(unsigned char slot) {
    unsigned char b[32];
    if (slot>=SAVE_SLOT_COUNT) return;
    sram_options_io_read_at(MD_BASE(slot)+16u,b,32u);
    options_runtime_init(); (void)options_runtime_apply(b,32u);
}
unsigned char save_game_quest_available(unsigned char slot,unsigned char q) {
    if (!save_game_slot_active(slot) || q>1u) return 0u;
    return !q || (ext_read(MD_BASE(slot)+4u)&5u)!=0u;
}
unsigned char save_game_select_quest(unsigned char slot,unsigned char q) {
    if (!save_game_quest_available(slot,q)) return 0u;
    quest_store(slot,save_game_slot_quest(slot));
    quest_restore(slot,q);
    sram_nes_save_block_store(&nes_ram[NES_SAVE_BLOCK_BASE],NES_SAVE_BLOCK_BYTES);
    return 1u;
}
unsigned char save_game_quest_stat(unsigned char slot,unsigned char q,unsigned char stat) {
    if (slot>=3u || q>1u || stat>2u) return 0u;
    if (q==save_game_slot_quest(slot)) return stat==0u ? save_game_slot_hearts(slot) :
        stat==1u ? save_game_slot_heart_partial(slot) : save_game_slot_deaths(slot);
    return ext_read(MD_QUEST(slot,q))==0xA5u ?
        ext_read(MD_QUEST(slot,q)+(stat==0u ? 25u : stat==1u ? 26u : 425u)) :
        stat==0u ? 0x22u : stat==1u ? 0xFFu : 0u;
}
unsigned char save_game_any_quest2_complete(void) {
    for (unsigned char s=0u;s<3u;s++)
        if (save_game_slot_active(s) && (ext_read(MD_BASE(s)+4u)&2u)) return 1u;
    return 0u;
}

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
    (void)options_persistence_load_or_default();
    for (unsigned char s=0u;s<3u;s++) {
        if (save_game_slot_active(s) && !header_valid(s)) {
            /* Legacy saves keep their current quest and all its bytes.
             * A legacy Q2 file retains Q2 eligibility; other quest blank. */
            ext_write(MD_QUEST(s,0u),0u); ext_write(MD_QUEST(s,1u),0u);
            quest_store(s,save_game_slot_quest(s) ? 1u : 0u);
            save_game_options_store(s);
            header_write(s,0u,save_game_slot_quest(s) ? 4u : 0u);
        }
        name_unlocks_store(s);
    }
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
    save_game_options_load(slot);
    save_file_a_load(slot);
    return 1u;
}

unsigned char save_game_save_current(void)
{
    if (!save_file_a_save(RAM(NES_CUR_SAVE_SLOT))) return 0u;
    quest_store(RAM(NES_CUR_SAVE_SLOT),save_game_slot_quest(RAM(NES_CUR_SAVE_SLOT)));
    persist();
    return 1u;
}

unsigned char save_game_register_mode(unsigned char slot, const unsigned char *name,unsigned char mode)
{
    unsigned char i;
    unsigned char blank = 1u;
    unsigned char zelda = 1u;

    if (slot >= SAVE_SLOT_COUNT || mode>1u || save_game_slot_active(slot)) return 0u;
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
    for (unsigned short n=0u;n<1024u;n++) ext_write(MD_BASE(slot)+n,0u);
    options_runtime_init(); save_game_options_store(slot);
    quest_store(slot,zelda);
    header_write(slot,mode,zelda ? 4u : 0u);
    name_unlocks_store(slot);
    persist();
    return 1u;
}

unsigned char save_game_rename(unsigned char slot, const unsigned char *name)
{
    unsigned char i, blank = 1u;
    if (!save_game_slot_active(slot)) return 0u;
    for (i = 0u; i < SAVE_NAME_BYTES; ++i)
        if (name[i] != 0x24u) blank = 0u;
    if (blank) return 0u;
    for (i = 0u; i < SAVE_NAME_BYTES; ++i) {
        nes_ram[NES_FILEA_NAME(slot) + i] = name[i];
        RAM(NES_SLOTINFO_NAMES + 8u * slot + i) = name[i];
    }
    save_file_a_commit(slot);
    name_unlocks_store(slot);
    persist();
    return 1u;
}
unsigned char save_game_register(unsigned char slot,const unsigned char *name) {
    return save_game_register_mode(slot,name,SAVE_MODE_ORIGINAL);
}
void save_game_complete_quest(void) {
    unsigned char s=RAM(NES_CUR_SAVE_SLOT),q;
    if (!save_game_slot_active(s)) return;
    q=save_game_slot_quest(s);
    (void)save_game_save_current();
    header_write(s,save_game_slot_mode(s),(unsigned char)(ext_read(MD_BASE(s)+4u)|(q ? 2u : 1u)));
    name_unlocks_store(s);
}

void save_game_erase(unsigned char slot)
{
    unsigned char i;
    if (slot >= SAVE_SLOT_COUNT) return;
    save_file_a_format(slot);
    for (unsigned short n=0u;n<1024u;n++) ext_write(MD_BASE(slot)+n,0u);
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
    for (i=0u;i<1024u;i++) ext_write(MD_BASE(dst)+i,ext_read(MD_BASE(src)+i));
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
