/* sprite_slots.h — H32 SAT slot contract (single source of truth).
 *
 * NES Z1 PPU has 64 OAM entries. Sega Genesis H32 mode also caps at 64
 * hardware sprites. After retiring the 32-sprite HUD backdrop strip
 * (PD priority 1: best long-term outcome), the H32 SAT is gameplay-only:
 *
 *   0..3   — UW door masks (T-131): NES WriteBlankPrioritySprites puts
 *            eight blank sprites on the rows at Y $3D and $DD, so the
 *            8-sprite line limit hides every later sprite there (Link
 *            under the N/S door frames). Genesis: a lead sprite with
 *            X != 0 then an X = 0 sprite masks later sprites on its lines.
 *   4..5   — Link (two 8x16 halves, like NES OAM 18/19)
 *   6..14  — items, projectiles, weapons (named per-slot below)
 *   15..16 — HUD equipped-item sprites
 *   17..18 — Original HUD player/compass position markers
 *   19..63 — enemy bridge (mirrors NES OAM scatter via enemy_render)
 *   64+    — never used in H32
 *
 * The opaque black HUD underlay now comes from BG_A tile 0 (PAL0 color 0),
 * produced by clear_hud_underlay_for_row_base() in RoomRom/src/main.c.
 *
 * SAT chain: each slot's link field points to the next slot in render
 * order (ROOMROM_SPRITE_NEXT: 0->1->...->18->19 enemy bridge entry). Per
 * `feedback_genesis_sprite_link_chain` memory: any slot with link=0 in
 * the middle of the chain hides downstream slots.
 *
 * Phase Z (2026-05-18 VRAM cleanup org): every renderer placing sprites
 * MUST use these named constants. Hard-coded literals are forbidden.
 */
#ifndef SPRITE_SLOTS_H
#define SPRITE_SLOTS_H

/* --- Gameplay slot assignments. Defined by sprite_render.c ownership
 * (set/clear function per slot). ---
 *
 * The door masks head the SAT chain, Link follows. Sword + beam are
 * weapon projectiles owned by combat_runtime.c. Projectiles are owned by
 * per-item runtime files in src/game/items/. Candle / magic shot are FX
 * sprites also owned by combat / item paths. */
#define ROOMROM_SPRITE_SLOT_MASK_N_LEAD     0u  /* N door band: X != 0 lead */
#define ROOMROM_SPRITE_SLOT_MASK_N          1u  /* N door band: X = 0 mask */
#define ROOMROM_SPRITE_SLOT_MASK_S_LEAD     2u  /* S door band: X != 0 lead */
#define ROOMROM_SPRITE_SLOT_MASK_S          3u  /* S door band: X = 0 mask */
#define ROOMROM_SPRITE_SLOT_LINK            4u  /* Link left half (8x16) */
#define ROOMROM_SPRITE_SLOT_LINK_R          5u  /* Link right half (8x16) */
#define ROOMROM_SPRITE_SLOT_SWORD           6u  /* Sword body (held during swing) */
#define ROOMROM_SPRITE_SLOT_BEAM            7u  /* Sword beam projectile (post-MakeSwordShot) */
#define ROOMROM_SPRITE_SLOT_BOOMERANG       8u  /* Boomerang in flight */
#define ROOMROM_SPRITE_SLOT_ARROW           9u  /* Arrow projectile */
#define ROOMROM_SPRITE_SLOT_BOMB           10u  /* Bomb (pre-explosion) */
#define ROOMROM_SPRITE_SLOT_EXPLOSION      11u  /* Bomb cloud animation */
#define ROOMROM_SPRITE_SLOT_ROOM_ITEM      12u  /* Room-pickup item (triforce / key / map) */
#define ROOMROM_SPRITE_SLOT_CANDLE_FIRE    13u  /* Candle flame FX */
#define ROOMROM_SPRITE_SLOT_MAGIC_SHOT     14u  /* Magic rod projectile */
#define ROOMROM_SPRITE_SLOT_HUD_B_ITEM     15u  /* HUD B-item LEFT half */
#define ROOMROM_SPRITE_SLOT_HUD_B_ITEM_R   16u  /* HUD B-item RIGHT half (hflip) */
#define ROOMROM_SPRITE_SLOT_HUD_PLAYER     17u  /* Original map player dot */
#define ROOMROM_SPRITE_SLOT_HUD_COMPASS    18u  /* Original map Triforce dot */

/* Play-area sprite Y: NES shows an OAM sprite one line below its Y, and
 * the Genesis frame is the NES frame without its top eight lines (the BG
 * and HUD are placed that way), so Genesis Y = NES OAM Y + 1 - 8. The
 * pause subscreen uses the same rule (inventory_render.c
 * SUBSCREEN_SAT_Y_OFFSET $79 = 128 - 7). Lockstep t131_uw_ndoor / newgame
 * screenshots: Link drew 7 rows below NES against the same BG rows. */
#define ROOMROM_PLAY_SPRITE_DY             (-7)

/* Next slot in the render chain. */
#define ROOMROM_SPRITE_NEXT(slot)          ((slot) + 1u)

/* --- Chain / range bounds --- */
#define ROOMROM_SPRITE_SLOT_LINK_FIRST       ROOMROM_SPRITE_SLOT_MASK_N_LEAD
#define ROOMROM_SPRITE_SLOT_GAMEPLAY_LAST    ROOMROM_SPRITE_SLOT_HUD_COMPASS
#define ROOMROM_SPRITE_SLOT_ENEMY_FIRST     19u
#define ROOMROM_SPRITE_SLOT_LAST_H32        63u
#define ROOMROM_SPRITE_UPLOAD_COUNT_H32     64u

/* --- Static asserts (caught at compile time per Phase Z) --- */
#ifdef __STDC_VERSION__
_Static_assert(ROOMROM_SPRITE_SLOT_GAMEPLAY_LAST < ROOMROM_SPRITE_SLOT_ENEMY_FIRST,
               "gameplay slots must not overlap enemy bridge range");
_Static_assert(ROOMROM_SPRITE_SLOT_ENEMY_FIRST <= ROOMROM_SPRITE_SLOT_LAST_H32,
               "enemy bridge must fit within H32 hardware sprite cap");
_Static_assert(ROOMROM_SPRITE_UPLOAD_COUNT_H32 == ROOMROM_SPRITE_SLOT_LAST_H32 + 1u,
               "upload count must cover slot 0 through LAST_H32");
#endif

#endif /* SPRITE_SLOTS_H */
