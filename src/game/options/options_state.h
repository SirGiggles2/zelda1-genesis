#ifndef SRC_GAME_OPTIONS_OPTIONS_STATE_H
#define SRC_GAME_OPTIONS_OPTIONS_STATE_H

/* Phase 9 Task 9.1 — Redux Options Runtime state.
 *
 * Drain Rule D1 stance: GREENFIELD (sanctioned). NES Zelda 1 has no
 * options menu; Redux adds this. No NES asm reference for behavior.
 *
 * Hard rule: serializable layout under 32 bytes so the entire struct
 * fits inside the locked SRAM range $800-$81F (Task 9.2 SRAM
 * Persistence). Field order is wire-stable; new fields land in the
 * reserved tail and bump OPTIONS_VERSION_CURRENT.
 *
 * Layout (32 bytes total, network byte order on multi-byte fields):
 *   off 0..1   magic       'O','P'  (0x4F 0x50)        sanity
 *   off 2      version     u8       OPTIONS_VERSION_*  migration gate
 *   off 3..4   bool_bits   be u16   OPTION_BOOL_*      bitfields
 *   off 5      sword_style u8       OPTIONS_SWORD_*    radio
 *   off 6      like_like   u8       OPTIONS_LIKELIKE_* radio
 *   off 7      bomb_upgrade u8      OPTIONS_BOMBUPG_*  radio
 *   off 8      start_hearts u8      3..16              numeric
 *   off 9      lost_woods   u8      OPTIONS_LWOODS_*   radio
 *   off 10     dark_room    u8      OPTIONS_DARK_*     radio
 *   off 11     room_scroll  u8      OPTIONS_SCROLL_*   radio   (v2)
 *   off 12     uw_music     u8      OPTIONS_UWMUSIC_*  radio   (v3)
 *   off 13..29 reserved     17 bytes — future growth
 *   off 30..31 checksum     be u16  add-all-bytes-mod-65536
 *
 * Bitfield assignments inside `bool_bits` (set = enabled):
 *   bit 0  low_health_warning      (Redux: gentle "low HP" cue)
 *   bit 1  automap                  (Redux: dungeon automap visible)
 *   bit 2  dungeon_colors           (Redux: per-level palette tint)
 *   bit 3  visible_secrets          (Redux: secret-tile hint dots)
 *   bit 4  diagonal_sword           (Redux: 8-way melee swing)
 *   bit 5  no_reduced_flashing      (Redux: photosensitive guard)
 *   bit 6  ab_swap                  (Redux: A and B button swap)
 *   bit 7  auto_collect_drops       (Redux: auto-pickup hearts/rupees)
 *   bit 8..15 reserved
 *
 * Enum values are consecutive small ints starting at 0; default
 * always = 0 unless noted.
 */

#ifdef __cplusplus
extern "C" {
#endif

#define OPTIONS_MAGIC0          0x4Fu  /* 'O' */
#define OPTIONS_MAGIC1          0x50u  /* 'P' */

#define OPTIONS_VERSION_NONE    0x00u  /* uninitialized — triggers migrate */
#define OPTIONS_VERSION_V1      0x01u  /* initial schema */
#define OPTIONS_VERSION_V2      0x02u  /* room_scroll option */
#define OPTIONS_VERSION_V3      0x03u  /* uw_music option */
#define OPTIONS_VERSION_CURRENT OPTIONS_VERSION_V3

#define OPTIONS_STATE_SIZE      32u

/* OPTION_ID — public addressable id for getters / setters. Ordered
 * by physical layout: bool group first, then enums by offset. */
typedef enum {
    OPTION_ID_LOW_HEALTH_WARNING = 0,   /* bool */
    OPTION_ID_AUTOMAP            = 1,   /* bool */
    OPTION_ID_DUNGEON_COLORS     = 2,   /* bool */
    OPTION_ID_VISIBLE_SECRETS    = 3,   /* bool */
    OPTION_ID_DIAGONAL_SWORD     = 4,   /* bool */
    OPTION_ID_NO_REDUCED_FLASHING = 5,  /* bool */
    OPTION_ID_AB_SWAP            = 6,   /* bool */
    OPTION_ID_AUTO_COLLECT_DROPS = 7,   /* bool */
    OPTION_ID_SWORD_STYLE        = 8,   /* enum */
    OPTION_ID_LIKE_LIKE_BEHAVIOR = 9,   /* enum */
    OPTION_ID_BOMB_UPGRADE       = 10,  /* enum */
    OPTION_ID_START_HEARTS       = 11,  /* numeric (3..16) */
    OPTION_ID_LOST_WOODS         = 12,  /* enum */
    OPTION_ID_DARK_ROOM_LIGHT    = 13,  /* enum */
    OPTION_ID_ROOM_SCROLL        = 14,  /* enum */
    OPTION_ID_UW_MUSIC           = 15,  /* enum */
    OPTION_ID_COUNT              = 16
} OptionId;

/* Bitfield bit indices inside `bool_bits` u16. */
#define OPTION_BOOL_BIT_LOW_HEALTH_WARNING    0u
#define OPTION_BOOL_BIT_AUTOMAP               1u
#define OPTION_BOOL_BIT_DUNGEON_COLORS        2u
#define OPTION_BOOL_BIT_VISIBLE_SECRETS       3u
#define OPTION_BOOL_BIT_DIAGONAL_SWORD        4u
#define OPTION_BOOL_BIT_NO_REDUCED_FLASHING   5u
#define OPTION_BOOL_BIT_AB_SWAP               6u
#define OPTION_BOOL_BIT_AUTO_COLLECT_DROPS    7u

/* sword_style enum — Redux per-style behavior. */
#define OPTIONS_SWORD_VANILLA      0u  /* default */
#define OPTIONS_SWORD_STAB_ONLY    1u
#define OPTIONS_SWORD_BEAM_ALWAYS  2u
#define OPTIONS_SWORD_COUNT        3u

/* like_like_behavior enum — Redux. */
#define OPTIONS_LIKELIKE_VANILLA   0u  /* default — eats magic shield */
#define OPTIONS_LIKELIKE_NO_EAT    1u
#define OPTIONS_LIKELIKE_COUNT     2u

/* bomb_upgrade enum — Redux. */
#define OPTIONS_BOMBUPG_VANILLA    0u  /* default — +4 cap per heart */
#define OPTIONS_BOMBUPG_PLUS4      1u
#define OPTIONS_BOMBUPG_PLUS8      2u
#define OPTIONS_BOMBUPG_COUNT      3u

/* lost_woods enum — Redux. */
#define OPTIONS_LWOODS_VANILLA     0u  /* default — strict path lock */
#define OPTIONS_LWOODS_RELAXED     1u
#define OPTIONS_LWOODS_COUNT       2u

/* dark_room_light enum — Redux. */
#define OPTIONS_DARK_VANILLA       0u  /* default — full dark */
#define OPTIONS_DARK_PARTIAL       1u
#define OPTIONS_DARK_BRIGHT        2u
#define OPTIONS_DARK_COUNT         3u

/* room_scroll enum. */
/* NES room scroll (ow_scroll.c): SMOOTH glides vertical scrolls at the
 * horizontal speed (OW 4 / UW 2 px a frame; picture only, NES RAM and
 * timing unchanged); CLASSIC shows the NES 8 px row steps. The non-NES
 * move style uses them as 32 / 64 frame fixed scrolls. */
#define OPTIONS_SCROLL_SMOOTH      0u  /* default */
#define OPTIONS_SCROLL_CLASSIC     1u
#define OPTIONS_SCROLL_COUNT       2u

/* uw_music enum: which underworld (dungeon) theme plays. Older saves
 * (v1/v2) have 0 here, the default. */
#define OPTIONS_UWMUSIC_INGLEBARD  0u  /* default */
#define OPTIONS_UWMUSIC_CYBERDEOUS 1u
#define OPTIONS_UWMUSIC_COUNT      2u

/* start_hearts numeric — clamp range. */
#define OPTIONS_START_HEARTS_MIN   3u
#define OPTIONS_START_HEARTS_MAX   16u

/* Wire-stable on-disk layout. Pack-equivalent — the struct uses
 * unsigned char arrays + explicit be u16 helpers in options_runtime.c
 * to dodge compiler-defined alignment padding. */
typedef struct OptionsState {
    unsigned char magic[2];       /* off 0..1 */
    unsigned char version;        /* off 2    */
    unsigned char bool_bits[2];   /* off 3..4 (be u16) */
    unsigned char sword_style;    /* off 5    */
    unsigned char like_like;      /* off 6    */
    unsigned char bomb_upgrade;   /* off 7    */
    unsigned char start_hearts;   /* off 8    */
    unsigned char lost_woods;     /* off 9    */
    unsigned char dark_room;      /* off 10   */
    unsigned char room_scroll;    /* off 11   */
    unsigned char uw_music;       /* off 12   */
    unsigned char reserved[17];   /* off 13..29 */
    unsigned char checksum[2];    /* off 30..31 (be u16) */
} OptionsState;

#ifdef __cplusplus
}
#endif

#endif /* SRC_GAME_OPTIONS_OPTIONS_STATE_H */
