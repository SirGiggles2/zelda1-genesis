/* Phase 9 Task 9.1 — Redux Options Runtime implementation.
 *
 * Drain Rule D1 stance: GREENFIELD. No NES asm reference; sanctioned
 * new-build per master plan §Phase 9 (debate 004).
 *
 * Hard rule WT-1: edits live in main worktree only (substrate path
 * src/game/options/). Hard rule BT-1: builds via Debug.bat.
 */

#include "options_runtime.h"

static OptionsState g_options;

/* --- be u16 helpers (struct stores big-endian on-disk bytes) ------ */

static unsigned int read_be_u16(const unsigned char *p)
{
    return ((unsigned int)p[0] << 8) | (unsigned int)p[1];
}

static void write_be_u16(unsigned char *p, unsigned int v)
{
    p[0] = (unsigned char)((v >> 8) & 0xFFu);
    p[1] = (unsigned char)(v & 0xFFu);
}

/* --- Checksum: add-all-bytes-mod-65536 over bytes 0..29 ------------ */

static unsigned int compute_checksum(const unsigned char *buf)
{
    unsigned int sum = 0u;
    unsigned int i;
    for (i = 0u; i < 30u; ++i) {
        sum = (sum + (unsigned int)buf[i]) & 0xFFFFu;
    }
    return sum;
}

/* --- Defaults --------------------------------------------------- */

static void load_defaults_into(OptionsState *s)
{
    unsigned int i;
    s->magic[0] = OPTIONS_MAGIC0;
    s->magic[1] = OPTIONS_MAGIC1;
    s->version = OPTIONS_VERSION_CURRENT;

    /* All Redux toggles default OFF (vanilla NES feel). */
    s->bool_bits[0] = 0u;
    s->bool_bits[1] = 0u;

    s->sword_style  = OPTIONS_SWORD_VANILLA;
    s->like_like    = OPTIONS_LIKELIKE_VANILLA;
    s->bomb_upgrade = OPTIONS_BOMBUPG_VANILLA;
    s->start_hearts = OPTIONS_START_HEARTS_MIN;
    s->lost_woods   = OPTIONS_LWOODS_VANILLA;
    s->dark_room    = OPTIONS_DARK_VANILLA;
    s->room_scroll = OPTIONS_SCROLL_SMOOTH;

    for (i = 0u; i < sizeof(s->reserved); ++i) {
        s->reserved[i] = 0u;
    }

    {
        unsigned int sum = compute_checksum((const unsigned char *)s);
        write_be_u16(s->checksum, sum);
    }
}

void options_runtime_init(void)
{
    load_defaults_into(&g_options);
}

/* --- Bool bitfield helpers --------------------------------------- */

static unsigned char bool_bit_for_id(unsigned int id, unsigned int *out_bit)
{
    switch (id) {
    case OPTION_ID_LOW_HEALTH_WARNING:
        *out_bit = OPTION_BOOL_BIT_LOW_HEALTH_WARNING;
        return 1u;
    case OPTION_ID_AUTOMAP:
        *out_bit = OPTION_BOOL_BIT_AUTOMAP;
        return 1u;
    case OPTION_ID_DUNGEON_COLORS:
        *out_bit = OPTION_BOOL_BIT_DUNGEON_COLORS;
        return 1u;
    case OPTION_ID_VISIBLE_SECRETS:
        *out_bit = OPTION_BOOL_BIT_VISIBLE_SECRETS;
        return 1u;
    case OPTION_ID_DIAGONAL_SWORD:
        *out_bit = OPTION_BOOL_BIT_DIAGONAL_SWORD;
        return 1u;
    case OPTION_ID_NO_REDUCED_FLASHING:
        *out_bit = OPTION_BOOL_BIT_NO_REDUCED_FLASHING;
        return 1u;
    case OPTION_ID_AB_SWAP:
        *out_bit = OPTION_BOOL_BIT_AB_SWAP;
        return 1u;
    case OPTION_ID_AUTO_COLLECT_DROPS:
        *out_bit = OPTION_BOOL_BIT_AUTO_COLLECT_DROPS;
        return 1u;
    default:
        return 0u;
    }
}

static unsigned char read_bool_bit(unsigned int bit_index)
{
    unsigned int word = read_be_u16(g_options.bool_bits);
    return (unsigned char)((word >> bit_index) & 1u);
}

static void write_bool_bit(unsigned int bit_index, unsigned char value)
{
    unsigned int word = read_be_u16(g_options.bool_bits);
    if (value != 0u) {
        word |= (1u << bit_index);
    } else {
        word &= ~(1u << bit_index);
    }
    write_be_u16(g_options.bool_bits, word & 0xFFFFu);
}

/* --- Public getter / setter -------------------------------------- */

unsigned char options_get(unsigned int id)
{
    unsigned int bit;
    if (bool_bit_for_id(id, &bit) != 0u) {
        return read_bool_bit(bit);
    }
    switch (id) {
    case OPTION_ID_SWORD_STYLE:        return g_options.sword_style;
    case OPTION_ID_LIKE_LIKE_BEHAVIOR: return g_options.like_like;
    case OPTION_ID_BOMB_UPGRADE:       return g_options.bomb_upgrade;
    case OPTION_ID_START_HEARTS:       return g_options.start_hearts;
    case OPTION_ID_LOST_WOODS:         return g_options.lost_woods;
    case OPTION_ID_DARK_ROOM_LIGHT:    return g_options.dark_room;
    case OPTION_ID_ROOM_SCROLL:        return g_options.room_scroll;
    default:                           return 0u;
    }
}

static void recheck_after_write(void)
{
    unsigned int sum = compute_checksum((const unsigned char *)&g_options);
    write_be_u16(g_options.checksum, sum);
}

void options_set(unsigned int id, unsigned char value)
{
    unsigned int bit;
    if (bool_bit_for_id(id, &bit) != 0u) {
        write_bool_bit(bit, (value != 0u) ? 1u : 0u);
        recheck_after_write();
        return;
    }
    switch (id) {
    case OPTION_ID_SWORD_STYLE:
        if (value < OPTIONS_SWORD_COUNT) g_options.sword_style = value;
        break;
    case OPTION_ID_LIKE_LIKE_BEHAVIOR:
        if (value < OPTIONS_LIKELIKE_COUNT) g_options.like_like = value;
        break;
    case OPTION_ID_BOMB_UPGRADE:
        if (value < OPTIONS_BOMBUPG_COUNT) g_options.bomb_upgrade = value;
        break;
    case OPTION_ID_START_HEARTS:
        if (value < OPTIONS_START_HEARTS_MIN) {
            g_options.start_hearts = OPTIONS_START_HEARTS_MIN;
        } else if (value > OPTIONS_START_HEARTS_MAX) {
            g_options.start_hearts = OPTIONS_START_HEARTS_MAX;
        } else {
            g_options.start_hearts = value;
        }
        break;
    case OPTION_ID_LOST_WOODS:
        if (value < OPTIONS_LWOODS_COUNT) g_options.lost_woods = value;
        break;
    case OPTION_ID_DARK_ROOM_LIGHT:
        if (value < OPTIONS_DARK_COUNT) g_options.dark_room = value;
        break;
    case OPTION_ID_ROOM_SCROLL:
        if (value < OPTIONS_SCROLL_COUNT) g_options.room_scroll = value;
        break;
    default:
        return;
    }
    recheck_after_write();
}

unsigned char options_get_version(void)
{
    return g_options.version;
}

/* --- Validation -------------------------------------------------- */

unsigned char options_runtime_validate(void)
{
    unsigned int actual_sum;
    unsigned int stored_sum;

    if (g_options.magic[0] != OPTIONS_MAGIC0) return 0u;
    if (g_options.magic[1] != OPTIONS_MAGIC1) return 0u;
    if (g_options.version  == OPTIONS_VERSION_NONE) return 0u;
    if (g_options.version  >  OPTIONS_VERSION_CURRENT) return 0u;
    if (g_options.sword_style  >= OPTIONS_SWORD_COUNT)    return 0u;
    if (g_options.like_like    >= OPTIONS_LIKELIKE_COUNT) return 0u;
    if (g_options.bomb_upgrade >= OPTIONS_BOMBUPG_COUNT)  return 0u;
    if (g_options.lost_woods   >= OPTIONS_LWOODS_COUNT)   return 0u;
    if (g_options.dark_room    >= OPTIONS_DARK_COUNT)     return 0u;
    if (g_options.room_scroll  >= OPTIONS_SCROLL_COUNT)   return 0u;
    if (g_options.start_hearts <  OPTIONS_START_HEARTS_MIN) return 0u;
    if (g_options.start_hearts >  OPTIONS_START_HEARTS_MAX) return 0u;

    actual_sum = compute_checksum((const unsigned char *)&g_options);
    stored_sum = read_be_u16(g_options.checksum);
    return (actual_sum == stored_sum) ? 1u : 0u;
}

/* --- Migration --------------------------------------------------- */

static unsigned char migrate_image(unsigned char *buf, unsigned int size)
{
    unsigned char ver;
    if (size < OPTIONS_STATE_SIZE) return 0u;
    if (buf[0] != OPTIONS_MAGIC0) return 0u;
    if (buf[1] != OPTIONS_MAGIC1) return 0u;
    ver = buf[2];
    if (ver == OPTIONS_VERSION_NONE) return 0u;
    if (ver > OPTIONS_VERSION_CURRENT) return 0u;
    /* v1 == CURRENT — no migration steps needed yet. Future: branch
     * here per ver and rewrite fields in place + bump buf[2]. */
    return 1u;
}

unsigned char options_runtime_apply(const unsigned char *buf,
                                    unsigned int size)
{
    unsigned char tmp[OPTIONS_STATE_SIZE];
    unsigned int i;
    unsigned int actual_sum;
    unsigned int stored_sum;

    if (buf == 0 || size < OPTIONS_STATE_SIZE) return 0u;

    for (i = 0u; i < OPTIONS_STATE_SIZE; ++i) {
        tmp[i] = buf[i];
    }

    if (migrate_image(tmp, OPTIONS_STATE_SIZE) == 0u) {
        return 0u;
    }

    actual_sum = compute_checksum(tmp);
    stored_sum = read_be_u16(&tmp[30]);
    if (actual_sum != stored_sum) return 0u;

    /* Field-by-field range check before commit. */
    if (tmp[5]  >= OPTIONS_SWORD_COUNT)    return 0u;
    if (tmp[6]  >= OPTIONS_LIKELIKE_COUNT) return 0u;
    if (tmp[7]  >= OPTIONS_BOMBUPG_COUNT)  return 0u;
    if (tmp[8]  <  OPTIONS_START_HEARTS_MIN) return 0u;
    if (tmp[8]  >  OPTIONS_START_HEARTS_MAX) return 0u;
    if (tmp[9]  >= OPTIONS_LWOODS_COUNT)   return 0u;
    if (tmp[10] >= OPTIONS_DARK_COUNT)     return 0u;
    if (tmp[11] >= OPTIONS_SCROLL_COUNT)   return 0u;

    /* Commit. */
    {
        unsigned char *dst = (unsigned char *)&g_options;
        for (i = 0u; i < OPTIONS_STATE_SIZE; ++i) {
            dst[i] = tmp[i];
        }
    }
    return 1u;
}

unsigned int options_runtime_serialize(unsigned char *buf,
                                       unsigned int size)
{
    unsigned int i;
    const unsigned char *src;
    if (buf == 0 || size < OPTIONS_STATE_SIZE) return 0u;

    /* Refresh magic/version/checksum before snapshot. */
    g_options.magic[0] = OPTIONS_MAGIC0;
    g_options.magic[1] = OPTIONS_MAGIC1;
    g_options.version  = OPTIONS_VERSION_CURRENT;
    recheck_after_write();

    src = (const unsigned char *)&g_options;
    for (i = 0u; i < OPTIONS_STATE_SIZE; ++i) {
        buf[i] = src[i];
    }
    return OPTIONS_STATE_SIZE;
}

unsigned char options_runtime_peek(unsigned int offset)
{
    if (offset >= OPTIONS_STATE_SIZE) return 0u;
    return ((const unsigned char *)&g_options)[offset];
}
