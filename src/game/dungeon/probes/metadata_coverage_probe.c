/* T-058 read-only compiled metadata coverage; armed boot self-tests only.
 * NES source: Z_05 CheckWarps / FindAndCreatePushBlockObject; Z_06 Q2 patch.
 * Drained C: cellar_meta.c / push_block_meta.c / level_info_install.c.
 * Coverage: FULL (metadata queries only; no connected progression claim).
 * Stance: EXTEND the existing armed metadata probe. No RAM installs. */
#include "../cellar_meta.h"
#include "../push_block_meta.h"
#include "../../world/level_info_install.h"

/* Header 'MD', version1, rows18, invalid-input failures; 18 sixteen-byte
 * rows: level,quest,cellar count,push count, query/attrs/list FNV1a hashes. */
volatile unsigned char g_dungeon_metadata_coverage[8u + 18u * 16u];

static unsigned long hash_byte(unsigned long hash, unsigned char b)
{
    return (hash ^ b) * 16777619ul;
}
static void put32(volatile unsigned char *out, unsigned long v)
{
    out[0] = (unsigned char)(v >> 24); out[1] = (unsigned char)(v >> 16);
    out[2] = (unsigned char)(v >> 8); out[3] = (unsigned char)v;
}
void dungeon_metadata_coverage_probe_run(void)
{
    unsigned char q, level, room, i, dest, hit;
    unsigned short row = 0u;
    roomrom_pushblock_meta_t pb;
    volatile unsigned char *out;
    g_dungeon_metadata_coverage[0] = 0u;
    g_dungeon_metadata_coverage[1] = 'D';
    g_dungeon_metadata_coverage[2] = 1u;
    g_dungeon_metadata_coverage[3] = 18u;
    for (i = 4u; i < 8u; ++i) g_dungeon_metadata_coverage[i] = 0u;
    dest = 0xAAu;
    if (level_info_uw_attributes(0u,1u) || level_info_uw_attributes(10u,1u) ||
        level_info_uw_cellars(1u,0u) || level_info_uw_cellars(1u,3u) ||
        roomrom_uw_cellar_for_source(1u,1u,128u,&dest) || dest != 0xAAu ||
        roomrom_uw_cellar_source_for_cellar(1u,1u,128u,&dest) || dest != 0xAAu ||
        roomrom_uw_room_is_cellar(1u,1u,128u) ||
        roomrom_pushblock_for_room(1u,1u,128u,&pb) ||
        roomrom_pushblock_for_room(1u,1u,0x22u,0) ||
        roomrom_uw_cellar_for_source(1u,1u,0x22u,0))
        g_dungeon_metadata_coverage[4] = 1u;
    for (q = 1u; q <= 2u; ++q) for (level = 1u; level <= 9u; ++level) {
        const unsigned char *attrs = level_info_uw_attributes(level,q);
        const unsigned char *cellars = level_info_uw_cellars(level,q);
        unsigned long queries = 2166136261ul, ah = 2166136261ul, ch = 2166136261ul;
        unsigned short k;
        unsigned char nc = 0u, np = 0u;
        for (room = 0u; room < 128u; ++room) {
            hit = roomrom_uw_room_is_cellar(level,q,room); nc += hit;
            queries = hash_byte(queries,hit);
            dest = 0xFFu; hit = roomrom_uw_cellar_for_source(level,q,room,&dest);
            queries = hash_byte(hash_byte(queries,hit),dest);
            dest = 0xFFu; hit = roomrom_uw_cellar_source_for_cellar(level,q,room,&dest);
            queries = hash_byte(hash_byte(queries,hit),dest);
            hit = roomrom_pushblock_for_room(level,q,room,&pb); np += hit;
            queries = hash_byte(queries,hit);
            queries = hash_byte(queries,hit ? pb.block_col_mt : 0u);
            queries = hash_byte(queries,hit ? pb.block_row_mt : 0u);
            queries = hash_byte(queries,hit ? pb.allowed_dirs : 0u);
            queries = hash_byte(queries,hit ? pb.trigger_kind : 0u);
        }
        for (k = 0u; k < 768u; ++k) ah = hash_byte(ah,attrs[k]);
        for (i = 0u; i < LEVEL_INFO_UW_CELLAR_COUNT; ++i) ch = hash_byte(ch,cellars[i]);
        out = &g_dungeon_metadata_coverage[8u + row * 16u];
        out[0]=level; out[1]=q; out[2]=nc; out[3]=np;
        put32(out+4u,queries); put32(out+8u,ah); put32(out+12u,ch); ++row;
    }
    g_dungeon_metadata_coverage[0] = 'M'; /* Publish complete last. */
}
