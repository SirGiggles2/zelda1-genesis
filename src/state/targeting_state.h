#ifndef TARGETING_STATE_H
#define TARGETING_STATE_H

#include "world_state.h"
#include "scratch_state.h"

/* Scratch and targeting state for promoted targeting helpers.
 * Zero-page scratch slots aliased through scratch_state.h. */
#define TARGET_DIR_ACCUM               ZP_TMP0
#define TARGET_MIN_COORD               ZP_TMP1
#define TARGET_MAX_COORD               ZP_TMP2
#define TARGET_H_DIST                  ZP_TMP3
#define TARGET_V_DIST                  ZP_TMP4
#define TARGET_H_DIR                   ZP_TMPA
#define TARGET_V_DIR                   ZP_TMPB

#endif
