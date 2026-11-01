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
#include "game_profile.h"
#include "inventory.h"
#include "loadout.h"

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
 * Version2 MD files add independently checked exploration records at
 * +924/+944 in the unused tail. Original stays version1. Existing quest
 * offsets and the Original NES block never move. Remix B/C records occupy
 * +964/+969: marker, quest tag, B, C, xor check. Companion records at
 * +974/+983: C7 marker, quest tag, three ordered B/C pairs, CRC-8/ATM
 * bound to the accepted primary pair. Bytes +992..+1023 remain unused.
 * Markers are written last; invalidate companions before primary writes. */
#define MD_BASE(s) (0x900u + 0x400u * (s))
#define MD_QUEST(s,q) (MD_BASE(s) + 64u + 432u * (q))
#define MD_DISCOVERY(s,q) (MD_BASE(s) + 924u + 20u * (q))
#define MD_LOADOUT(s,q) (MD_BASE(s) + 964u + 5u * (q))
#define MD_PARTY_LOADOUT(s,q) (MD_BASE(s) + 974u + 9u * (q))
static unsigned char s_discovery[16];
/* T-172: changes whenever the discovery set may change (HUD map gate). */
static unsigned char s_discovery_gen;
static unsigned char ext_read(unsigned short a) {
    unsigned char v; sram_options_io_read_at(a,&v,1u); return v;
}
static void ext_write(unsigned short a,unsigned char v) {
    sram_options_io_write_at(a,&v,1u);
}
/* NES source: Z_02.asm:UpdateMode1Menu_Sub1 resets SelectedItemSlot; NES has no C slot.
 * Drained C: save_file_a_load/save_game_save_current and per-quest extension.
 * Coverage: PARTIAL (Remix per-file/quest selection; Original remains NES).
 * Stance: EXTEND the existing save boundary with validated native values. */
static unsigned char loadout_values_valid(unsigned char b,unsigned char c) {
    unsigned char physical_b=(b==0x0Fu) ? 7u : b;
    if (b==3u || (b>8u && b!=0x0Fu)) return 0u;
    if (c==3u || (c>8u && c!=0x0Fu && c!=LOADOUT_SLOT_NONE)) return 0u;
    return c==LOADOUT_SLOT_NONE || (c==0x0Fu ? 7u : c)!=physical_b;
}
static void loadout_store(unsigned char slot,unsigned char q) {
    unsigned short a=MD_LOADOUT(slot,q);
    unsigned char b=F_ram_0656_R,c=loadout_c_slot(),tag=(unsigned char)(0x30u+q);
    if (!loadout_values_valid(b,c)) { b=0u;c=LOADOUT_SLOT_NONE; }
    ext_write(a,0u);
    ext_write(a+1u,tag);ext_write(a+2u,b);ext_write(a+3u,c);
    ext_write(a+4u,(unsigned char)(0x5Au^tag^b^c));
    ext_write(a,0xC5u);
}
static unsigned char loadout_restore(unsigned char slot,unsigned char q) {
    unsigned short a=MD_LOADOUT(slot,q);
    unsigned char tag=(unsigned char)(0x30u+q),b=0u,c=LOADOUT_SLOT_NONE,valid=0u;
    if (ext_read(a)==0xC5u && ext_read(a+1u)==tag) {
        unsigned char stored_b=ext_read(a+2u),stored_c=ext_read(a+3u);
        if (ext_read(a+4u)==(unsigned char)(0x5Au^tag^stored_b^stored_c) &&
            loadout_values_valid(stored_b,stored_c)) {
            b=stored_b;c=stored_c;valid=1u;
        }
    }
    F_ram_0656_R=b;
    g_inventory.selected_b_item=b;
    loadout_set_c_slot(c);
    return valid;
}
/* NES source: SelectedItemSlot; drained C: loadout_store/loadout_restore.
 * Coverage: extra-player per-file/quest selection; legacy primary retained.
 * Stance: EXTEND with independently checked, marker-last records. */
/* CRC-8/ATM binds ordered companion pairs to their accepted primary pair.
 * C6 was the pre-integration XOR format; treat it as a missing record. */
static unsigned char party_crc(unsigned char crc,unsigned char v) {
    unsigned char bit;
    crc^=v;
    for(bit=0u;bit<8u;bit++)
        crc=(unsigned char)((crc&0x80u) ? (crc<<1)^0x07u : crc<<1);
    return crc;
}
static unsigned char party_crc_start(unsigned char tag) {
    return party_crc(party_crc(party_crc(0xA6u,tag),F_ram_0656_R),loadout_c_slot());
}
static void party_loadout_store(unsigned char slot,unsigned char q) {
    unsigned short a=MD_PARTY_LOADOUT(slot,q);
    unsigned char i,tag=(unsigned char)(0x40u+q),sum=party_crc_start(tag);
    ext_write(a,0u);ext_write(a+1u,tag);
    for(i=1u;i<4u;i++) {
        unsigned char b=loadout_party_b(i),c=loadout_party_c(i);
        if(!loadout_values_valid(b,c)){b=0u;c=LOADOUT_SLOT_NONE;}
        ext_write((unsigned short)(a+2u+(i-1u)*2u),b);
        ext_write((unsigned short)(a+3u+(i-1u)*2u),c);
        sum=party_crc(party_crc(sum,b),c);
    }
    ext_write(a+8u,sum);ext_write(a,0xC7u);
}
static void party_loadout_restore(unsigned char slot,unsigned char q) {
    unsigned short a=MD_PARTY_LOADOUT(slot,q);
    unsigned char i,tag=(unsigned char)(0x40u+q),sum=party_crc_start(tag),v[6];
    loadout_party_reset(F_ram_0656_R,loadout_c_slot());
    if(ext_read(a)!=0xC7u || ext_read(a+1u)!=tag)return;
    for(i=0u;i<6u;i++){v[i]=ext_read((unsigned short)(a+2u+i));sum=party_crc(sum,v[i]);}
    if(ext_read(a+8u)!=sum)return;
    for(i=0u;i<3u;i++)if(!loadout_values_valid(v[2u*i],v[2u*i+1u]))return;
    for(i=1u;i<4u;i++)(void)loadout_party_set(i,v[2u*(i-1u)],v[2u*(i-1u)+1u]);
}
static unsigned char header_valid(unsigned char slot) {
    unsigned char b[8], x=0u;
    sram_options_io_read_at(MD_BASE(slot),b,8u);
    for (unsigned char i=0u;i<8u;i++) x^=b[i];
    return b[0]=='M' && b[1]=='D' &&
        (b[2]==1u || (b[2]==2u && b[3]==SAVE_MODE_MD_REMIX)) &&
        b[3]<=1u && !(b[4]&0xF0u) && b[5]<=4u && !x;
}
static void header_write(unsigned char slot,unsigned char mode,unsigned char flags) {
    unsigned char b[8]={'M','D',mode ? 2u : 1u,mode,flags,save_game_slot_players(slot),0u,0u};
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
    if (i<40u) return F_ram_601A_X(slot, i);
    if (i<424u) return F_ram_6092_X(slot, i-40u);
    return F_ram_6518_X(slot);
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
/* NES source: Redux automap.asm:MapRam / UpdateMapSpot / SaveMapData.
 * Drained C: save_game quest codec; NES has no per-quest MD extension.
 * Coverage: PARTIAL(persistence and actual OW visits; map renderer separate).
 * Stance: EXTEND native16-column/8-row bits in independent tail records. */
static unsigned char discovery_valid(unsigned char slot,unsigned char q) {
    unsigned short a=MD_DISCOVERY(slot,q),sum=(unsigned short)(0x10u+q);
    if (ext_read(a)!=0xD7u || ext_read(a+1u)!=(unsigned char)(0x10u+q)) return 0u;
    for (unsigned char i=0u;i<16u;i++) sum+=ext_read(a+2u+i);
    return ext_read(a+18u)==(unsigned char)(sum>>8) && ext_read(a+19u)==(unsigned char)sum;
}
static void discovery_store(unsigned char slot,unsigned char q,const unsigned char *bits) {
    unsigned short a=MD_DISCOVERY(slot,q),sum=(unsigned short)(0x10u+q);
    ext_write(a,0u);ext_write(a+1u,(unsigned char)(0x10u+q));
    for (unsigned char i=0u;i<16u;i++) { unsigned char v=bits ? bits[i] : 0u;sum+=v;ext_write(a+2u+i,v); }
    ext_write(a+18u,(unsigned char)(sum>>8));ext_write(a+19u,(unsigned char)sum);
    ext_write(a,0xD7u);
}
void save_game_discovery_clear(void) {
    for (unsigned char i=0u;i<16u;i++) s_discovery[i]=0u;
    ++s_discovery_gen;
}
unsigned char save_game_discovery_gen(void) { return s_discovery_gen; }
void save_game_discovery_load(unsigned char slot,unsigned char q) {
    save_game_discovery_clear();
    if (slot>=3u || q>1u || save_game_slot_mode(slot)!=SAVE_MODE_MD_REMIX) return;
    if (!discovery_valid(slot,q)) { discovery_store(slot,q,0);return; }
    for (unsigned char i=0u;i<16u;i++) s_discovery[i]=ext_read(MD_DISCOVERY(slot,q)+2u+i);
    ++s_discovery_gen;
}
unsigned char save_game_room_discovered(unsigned char room) {
    if (!game_profile_is_remix() || room>=128u) return 0u;
    return (s_discovery[room&15u] & (1u<<(room>>4)))!=0u;
}
void save_game_discover_room(unsigned char room) {
    const GameProfile *profile=game_profile_current();
    unsigned char col,bit;
    if (!game_profile_is_remix() || F_ram_0010_R!=0u || room>=128u) return;
    col=room&15u;bit=(unsigned char)(1u<<(room>>4));
    if (s_discovery[col]&bit) return;
    s_discovery[col]|=bit;
    ++s_discovery_gen;
    discovery_store(profile->slot,profile->quest,s_discovery);
    inventory_hud_mark_dirty();
}
/* NES source: Redux rupee.asm:GenerateHexValue; arrows.asm; bombs.asm.
 * Drained C: save_game_boot / quest_store and Original save_file_a codec.
 * Coverage: FULL(old MD resource conversion; modern preview preservation).
 * Stance: EXTEND once by header version, retaining equipment and world flags. */
static void migrate_resources(unsigned char *items) {
    unsigned char cap=items[0x25u],old=(cap==8u || cap==12u || cap==16u);
    unsigned char decimal=items[0x23u],hundreds=items[0x24u];
    unsigned short compat=(unsigned short)(hundreds*160u+(decimal>>4)*10u+(decimal&15u));
    if (compat>255u) compat=255u;
    /* Recent v1 previews already wrote the Redux ledger. An authenticated
     * decimal encoding whose native compatibility projection agrees must
     * retain earned values above255 rather than reconvert that projection. */
    if (hundreds>9u || (decimal>>4)>9u || (decimal&15u)>9u || compat!=items[0x16u]) {
        unsigned char value=items[0x16u],rem=(unsigned char)(value%100u);
        hundreds=(unsigned char)(value/100u);
        decimal=(unsigned char)(((rem/10u)<<4)|(rem%10u));
        items[0x23u]=decimal;items[0x24u]=hundreds;
        compat=(unsigned short)(hundreds*160u+rem);
        items[0x16u]=(unsigned char)(compat>255u ? 255u : compat);
    }
    cap=cap==8u ? 10u : cap==12u ? 20u : cap==16u ? 30u :
        (cap==10u || cap==20u || cap==30u) ? cap : 10u;
    items[0x25u]=cap;
    if (items[1u]>cap) items[1u]=cap;
    cap=items[2u]==2u ? 60u : items[2u]==1u ? 30u : 0u;
    if (old || items[0x20u]>cap) items[0x20u]=cap;
}
static void migrate_md_file(unsigned char slot) {
    unsigned char items[40],current=save_game_slot_quest(slot) ? 1u : 0u;
    unsigned char flags=ext_read(MD_BASE(slot)+4u);
    if (save_game_slot_mode(slot)!=SAVE_MODE_MD_REMIX || ext_read(MD_BASE(slot)+2u)!=1u) return;
    /* FileA is authoritative for the currently selected quest. */
    for (unsigned char i=0u;i<40u;i++) items[i]=F_ram_601A_X(slot, i);
    migrate_resources(items);
    for (unsigned char i=0u;i<40u;i++) F_ram_601A_X(slot, i)=items[i];
    save_file_a_commit(slot);quest_store(slot,current);
    {
        unsigned char q=(unsigned char)(current^1u);
        unsigned short a=MD_QUEST(slot,q),sum=0u;
        if (quest_valid(slot,q)) {
            for (unsigned char i=0u;i<40u;i++) items[i]=ext_read(a+1u+i);
            migrate_resources(items);ext_write(a,0u);
            for (unsigned char i=0u;i<40u;i++) { sum+=items[i];ext_write(a+1u+i,items[i]); }
            for (unsigned short i=40u;i<425u;i++) sum+=ext_read(a+1u+i);
            ext_write(a+426u,(unsigned char)(sum>>8));ext_write(a+427u,(unsigned char)sum);ext_write(a,0xA5u);
        }
    }
    /* Historical exploration is deliberately empty, never inferred from
     * unrelated world-secret or dungeon-visit flags. Publish v2 last. */
    discovery_store(slot,0u,0);discovery_store(slot,1u,0);
    header_write(slot,SAVE_MODE_MD_REMIX,flags);
}
static void quest_restore(unsigned char slot,unsigned char q) {
    unsigned short a=MD_QUEST(slot,q);
    unsigned char valid=quest_valid(slot,q);
    for (unsigned short i=0u;i<40u;i++)
        F_ram_601A_X(slot, i)=valid ? ext_read(a+1u+i) :
            i==0x18u ? 0x22u : i==0x19u ? 0xFFu :
            i==0x25u ? (save_game_slot_mode(slot) ? 10u : 8u) : 0u;
    for (unsigned short i=0u;i<384u;i++)
        F_ram_6092_X(slot, i)=valid ? ext_read(a+41u+i) : 0u;
    F_ram_6518_X(slot)=valid ? ext_read(a+425u) : 0u;
    F_ram_651B_X(slot)=F_ram_062D_R5(slot)=q;
    F_ram_0630_R3(slot)=F_ram_6518_X(slot);
    /* Z_02.asm @LoopHeart (as save_serializer.c boot): slot info shows
     * full hearts, (containers & $F0) | containers >> 4. */
    {
        const unsigned char hi=(unsigned char)(F_ram_6032_X(slot)&0xF0u);
        F_NES_SLOTINFO_HEARTS_0650_R(2u*slot)=(unsigned char)(hi|(hi>>4));
    }
    F_NES_SLOTINFO_HEARTS_0651_R(2u*slot)=F_ram_6033_X(slot);
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
    sram_nes_save_block_store(&nes_ram[NES_SAVE_BLOCK_BASE],NES_SAVE_BLOCK_BYTES); /* native_ram bulk:src/state/save_game.c:254 */
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
    sram_nes_save_block_store(&nes_ram[NES_SAVE_BLOCK_BASE], NES_SAVE_BLOCK_BYTES); /* native_ram bulk:src/state/save_game.c:276 */
}

void save_game_boot(void)
{
    sram_nes_save_block_load(&nes_ram[NES_SAVE_BLOCK_BASE], NES_SAVE_BLOCK_BYTES); /* native_ram bulk:src/state/save_game.c:281 */
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
        migrate_md_file(s);
        name_unlocks_store(s);
    }
    /* Z_07.asm InitializeGameOrMode: "Mark Save RAM initialized" ($6001 =
     * $5A; its $7FFF = $A5 partner lies outside the persisted block). */
    F_ram_6001 = 0x5Au;
    /* The NES formats in battery RAM directly; keep the cart equal to
     * what was validated so a formatted blank slot stays formatted. */
    persist();
}

unsigned char save_game_slot_active(unsigned char slot)
{
    if (slot >= SAVE_SLOT_COUNT) return 0u;
    return F_NES_SLOTINFO_ACTIVE_0633_R(slot) ? 1u : 0u;
}

unsigned char save_game_slot_quest(unsigned char slot)
{
    if (slot >= SAVE_SLOT_COUNT) return 0u;
    return F_ram_062D_R6(slot);
}

unsigned char save_game_load_slot(unsigned char slot)
{
    if (!save_game_slot_active(slot)) return 0u;
    save_game_options_load(slot);
    save_file_a_load(slot);
    if (save_game_slot_mode(slot)==SAVE_MODE_MD_REMIX) {
        if(loadout_restore(slot,save_game_slot_quest(slot) ? 1u : 0u))
            party_loadout_restore(slot,save_game_slot_quest(slot) ? 1u : 0u);
        else loadout_party_reset(0u,LOADOUT_SLOT_NONE);
    } else {
        loadout_set_c_slot(LOADOUT_SLOT_NONE);
        loadout_party_reset(F_ram_0656_R,LOADOUT_SLOT_NONE);
    }
    return 1u;
}

unsigned char save_game_save_current(void)
{
    if (!save_file_a_save(F_ram_0016_R3)) return 0u;
    quest_store(F_ram_0016_R3,save_game_slot_quest(F_ram_0016_R3));
    if (save_game_slot_mode(F_ram_0016_R3)==SAVE_MODE_MD_REMIX) {
        /* A cut during primary publication must not resurrect older companions. */
        ext_write(MD_PARTY_LOADOUT(F_ram_0016_R3,save_game_slot_quest(F_ram_0016_R3) ? 1u : 0u),0u);
        loadout_store(F_ram_0016_R3,save_game_slot_quest(F_ram_0016_R3) ? 1u : 0u);
        party_loadout_store(F_ram_0016_R3,save_game_slot_quest(F_ram_0016_R3) ? 1u : 0u);
    }
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
        F_NES_SLOTINFO_NAMES_R2(8u * slot, i) = name[i];
        if (name[i] != 0x24u) blank = 0u;
    }
    if (blank) return 0u;
    for (i = 0u; i < 5u; ++i) if (name[i] != k_zelda[i]) zelda = 0u;

    /* File B init in UpdateModeERegister, then CopyFileBToFileA. */
    save_file_a_format(slot);
    for (i = 0u; i < SAVE_NAME_BYTES; ++i)
        F_NES_FILEA_NAME_6002_X(slot, i) = name[i];
    F_ram_6032_X2(slot) = 0x22u;   /* HeartValues */
    F_ram_6033_X2(slot) = 0xFFu;   /* HeartPartial */
    /* NES source: Redux bombs.asm bank2:$9F4B/$AF83.
     * Drained C: save_game_register_mode / quest_restore native records.
     * Coverage: PARTIAL(new records; historical migration remains).
     * Stance: EXTEND per-file default without altering Original bytes. */
    F_ram_603F_X(slot) = mode ? 10u : 8u;
    F_ram_6512_X(slot) = 1u;
    F_ram_651B_X(slot) = zelda;
    save_file_a_commit(slot);
    F_NES_SLOTINFO_ACTIVE_0633_R(slot) = 1u;
    F_ram_062D_R6(slot) = zelda;
    F_ram_0630_R4(slot) = 0u;
    F_NES_SLOTINFO_HEARTS_0650_R2(2u * slot) = 0x22u;
    F_NES_SLOTINFO_HEARTS_0651_R2(2u * slot) = 0xFFu;
    for (unsigned short n=0u;n<1024u;n++) ext_write(MD_BASE(slot)+n,0u);
    options_runtime_init(); save_game_options_store(slot);
    quest_store(slot,zelda);
    if (mode==SAVE_MODE_MD_REMIX) { discovery_store(slot,0u,0);discovery_store(slot,1u,0); }
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
        F_NES_FILEA_NAME_6002_X(slot, i) = name[i];
        F_NES_SLOTINFO_NAMES_R2(8u * slot, i) = name[i];
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
    unsigned char s=F_ram_0016_R3,q;
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
        F_NES_SLOTINFO_NAMES_R2(8u * slot, i) = 0x24u;
    F_NES_SLOTINFO_HEARTS_0650_R2(2u * slot) = 0u;
    F_NES_SLOTINFO_HEARTS_0651_R2(2u * slot) = 0u;
    persist();
}

unsigned char save_game_copy(unsigned char src, unsigned char dst)
{
    unsigned short i;
    if (src >= SAVE_SLOT_COUNT || dst >= SAVE_SLOT_COUNT || src == dst) return 0u;
    if (!save_game_slot_active(src)) return 0u;
    for (i=0u;i<1024u;i++) ext_write(MD_BASE(dst)+i,ext_read(MD_BASE(src)+i));
    for (i = 0u; i < SAVE_NAME_BYTES; ++i)
        F_NES_FILEA_NAME_6002_X(dst, i) = F_NES_FILEA_NAME_6002_X(src, i);
    for (i = 0u; i < SAVE_ITEMS_BYTES; ++i)
        F_ram_601A_X2(dst, i) = F_ram_601A_X2(src, i);
    for (i = 0u; i < SAVE_WORLD_FLAGS_BYTES; ++i)
        F_ram_6092_X2(dst, i) = F_ram_6092_X2(src, i);
    F_ram_6512_X(dst)  = F_ram_6512_X(src);
    F_ram_6515_X(dst) = F_ram_6515_X(src);
    F_ram_6518_X(dst)  = F_ram_6518_X(src);
    F_ram_651B_X(dst)   = F_ram_651B_X(src);
    save_file_a_commit(dst);
    for (i = 0u; i < SAVE_NAME_BYTES; ++i)
        F_NES_SLOTINFO_NAMES_R2(8u * dst, i) = F_NES_SLOTINFO_NAMES_R2(8u * src, i);
    F_NES_SLOTINFO_ACTIVE_0633_R(dst) = F_NES_SLOTINFO_ACTIVE_0633_R(src);
    F_ram_062D_R6(dst)  = F_ram_062D_R6(src);
    F_ram_0630_R4(dst) = F_ram_0630_R4(src);
    F_NES_SLOTINFO_HEARTS_0650_R2(2u * dst)      = F_NES_SLOTINFO_HEARTS_0650_R2(2u * src);
    F_NES_SLOTINFO_HEARTS_0651_R2(2u * dst) = F_NES_SLOTINFO_HEARTS_0651_R2(2u * src);
    persist();
    return 1u;
}

const volatile unsigned char *save_game_slot_name(unsigned char slot)
{
    if (slot >= SAVE_SLOT_COUNT) slot = 0u;
    return &F_NES_SLOTINFO_NAMES_X(8u * slot); /* native_ram bulk:dyn_addr */
}

unsigned char save_game_slot_hearts(unsigned char slot)
{
    return (slot < SAVE_SLOT_COUNT) ? F_NES_SLOTINFO_HEARTS_0650_R2(2u * slot) : 0u;
}

unsigned char save_game_slot_heart_partial(unsigned char slot)
{
    return (slot < SAVE_SLOT_COUNT) ? F_NES_SLOTINFO_HEARTS_0651_R2(2u * slot) : 0u;
}

unsigned char save_game_slot_deaths(unsigned char slot)
{
    return (slot < SAVE_SLOT_COUNT) ? F_ram_0630_R4(slot) : 0u;
}

/* File Select occupancy (overrides the weak default in fs_render.c). */
unsigned char fs_sram_slot_occupied(unsigned char slot)
{
    return save_game_slot_active(slot);
}
