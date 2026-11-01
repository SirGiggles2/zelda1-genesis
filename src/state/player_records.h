#ifndef PLAYER_RECORDS_H
#define PLAYER_RECORDS_H

/* Primitive native record shapes: no ABI includes (safe for generated bindings).
 * Coordinate byte names describe m68k big-endian storage. A low-byte write
 * retains the high byte; image imports must still explicitly zero-extend. */
typedef struct LinkState {
    /* Position. NES ObjX[0] = $70, ObjY[0] = $84. Signed 16-bit so
     * room-edge math stays correct under Genesis scroll. */
    union { signed short x; struct { unsigned char x_hi, x_lo; }; };
    union { signed short y; struct { unsigned char y_hi, y_lo; }; };

    /* Sub-pixel accumulators. NES ObjPosFrac[0] = $3A8,
     * ObjQSpeedFrac[0] = $3BC. */
    unsigned char pos_frac;
    unsigned char qspeed_frac;

    /* Facing / movement direction.
     *   `dir`       = native ordinal (down/up/left/right = 1/2/3/4);
     *                 the legacy bridge publishes NES ObjDir ($98) bits
     *   `face`      = persisted facing for sprite rendering
     *   `input_dir` = NES ObjInputDir[0] = $3F8 (this frame's pad mask)
     *   `moving_dir`= NES LINK_MOVING_DIR = $0F (last active axis)
     */
    unsigned char dir;
    unsigned char face;
    unsigned char input_dir;
    unsigned char moving_dir;

    /* Action state. NES ObjState[0] = $AC. go_straight is NES
     * Link_GoStraight ($57). The separate diagonal latch is $56;
     * neither field is ObjMetastate ($405). */
    unsigned char state;
    unsigned char go_straight;

    /* Movement / collision. NES DoorwayDir = $53 and
     * ObjGridOffset[0] = $394. Doorway direction was previously
     * mislabelled as ObjMovingLimit ($380). */
    unsigned char doorway_dir;
    signed char   grid_offset;

    /* Animation. NES ObjAnimCounter[0] = $3D0, ObjAnimFrame[0] = $3E4. */
    unsigned char anim_counter;
    unsigned char anim_frame;

    /* Invincibility. NES ObjInvincibilityTimer[0] = $4F0,
     * ObjInvincibilityMask[0] = $4B2. */
    unsigned char invincibility_timer;
    unsigned char invincibility_mask;

    /* Damage / knockback. NES ObjShoveDir[0] = $C0, ObjShoveDistance[0]
     * = $D3. */
    unsigned char shove_dir;
    unsigned char shove_distance;

    /* Item/action input. NES ObjShootTimer[0] = $451; halt flag = $66C
     * (LINK_HALT_FLAG). Sword lifecycle now lives in g_player_sword[]. */
    unsigned char shoot_timer;
    unsigned char halt_flag;

    /* Hit points. NES ObjHP[0] = $485. */
    unsigned char hp;

    /* Host render/animation timing. These were previously mislabelled as
     * NES death cells; the real Mode 11 death timer/counter remain in the
     * legacy cells until the death owner migrates. */
    unsigned char anim_tick;
    unsigned char draw_pending;

    /* Render walk-frame bit. Previously mislabelled as LINK_ROOM_SCRATCH
     * ($59); room scratch is not this field. */
    unsigned char render_frame;

    /* Persistent diagonal-direction latch, distinct from go_straight.
     * NES Link_GoStraightWhenDiagInput ($56); now owned per player. */
    unsigned char go_straight_when_diag;
} LinkState;

/* PlayerState alias — the ONLY shape Phase 13 multiplayer needs to
 * differ. Adding more players is `players[i]`, not a struct rewrite. */
typedef LinkState PlayerState;

typedef struct PlayerSwordState {
    unsigned char state;    /* 0 idle, 1..5 NES swing states */
    unsigned char timer;    /* state countdown */
    unsigned char dir;      /* NES direction bit: 1 right,2 left,4 down,8 up */
    unsigned char face;     /* link_face_t: 0 down,1 up,2 left,3 right */
    unsigned char owner;    /* 0..3 */
    union { signed short x; struct { unsigned char x_hi, x_lo; }; };
    union { signed short y; struct { unsigned char y_hi, y_lo; }; };
} PlayerSwordState;

/* T-367: per-player sword shot / magic shot record. State and pose are
 * native; the legacy $0E slot remains the movement/collision bridge. */
typedef struct PlayerBeamState {
    unsigned char state;    /* 0, $10 sword, $11 spread, $80 magic */
    unsigned char dir;      /* NES direction bits, or spread offset */
    unsigned char grid;     /* NES grid offset */
    unsigned char qspeed;   /* NES q-speed */
    unsigned char owner;    /* 0..3 */
    unsigned char x;
    unsigned char y;
    unsigned char pos_frac; /* retained across spawn and state-only reset */
} PlayerBeamState;

#endif /* PLAYER_RECORDS_H */
