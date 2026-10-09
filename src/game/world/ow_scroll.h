#ifndef GAME_OW_SCROLL_H
#define GAME_OW_SCROLL_H

/* NES room-to-room modes 6 (leave), 7 (scroll) and 4 (enter) for the OW
 * and, with CurLevel != 0, the UW (T-131). */

/* Host scroll directions: right, left, down, up. Zero means no edge;
 * 0x80 blocks movement at an outer map edge without starting a scroll. */
unsigned char ow_scroll_edge(short x, short y, unsigned char nes_dir,
                             signed char grid, unsigned char room);
void ow_scroll_begin(unsigned char direction, unsigned char target);
/* T-132: mode 4 only (level entry after the curtain), Link facing
 * NES direction nes_dir; the room is already current. */
void ow_scroll_begin_enter(unsigned char nes_dir);

/* ow_scroll_tick results. */
#define OW_SCROLL_BUSY  0u
#define OW_SCROLL_PLAY  1u   /* mode 5: play resumes */
#define OW_SCROLL_WALK  2u   /* mode 6/4 update: move Link one frame along
                                ObjDir (MoveObject + Link_EndMoveAndAnimate),
                                ObjGridOffset $394 is not at 0 / +-8 */
#define OW_SCROLL_ENTER 3u   /* InitMode_EnterRoom (mode 4 submode 0): the
                                caller makes the new room current; in the
                                UW it then calls ow_scroll_enter_room_uw() */

/* Advances leave / prepare / scroll / enter. */
/* T-145: NES frames a fast Genesis step skipped (their FrameCounter,
 * Random and timer work must still run); read once, then cleared. */
unsigned char ow_scroll_take_catch_up(void);
unsigned char ow_scroll_tick(short *x, short *y);
/* UW InitMode_EnterRoom @Method2 for Link: DoorwayDir, X at the entered
 * edge (horizontal), ObjGridOffset from the entered door's type, and the
 * close command for an opened entered door. */
void ow_scroll_enter_room_uw(short *x);
/* Native renderer stages one column on each of the first 16 prepare ticks. */
unsigned char ow_scroll_column(void); /* 0..15, or 0xff */
unsigned short ow_scroll_pixels(void);
/* Picture only (NES RAM keeps the row steps): the camera distance and
 * Link's sprite Y to show this frame. Room scroll SMOOTH glides vertical
 * scrolls at the horizontal speed; otherwise ow_scroll_pixels() / y. */
unsigned short ow_scroll_display_pixels(void);
short ow_scroll_display_link_y(short y);
/* NES mode 7 in the UW hides every sprite (DrawSpritesBetweenRooms; the
 * UW Link_EndMoveAndAnimateBetweenRooms draws nothing). */
unsigned char ow_scroll_link_hidden(void);
#endif
