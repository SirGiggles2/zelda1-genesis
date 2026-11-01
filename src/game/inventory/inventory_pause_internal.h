/* inventory_pause_internal.h — pause sheet services inventory_render.c
 * lends to pause_actions.c (T-256). Not for other subsystems. */
#ifndef INVENTORY_PAUSE_INTERNAL_H
#define INVENTORY_PAUSE_INTERNAL_H

/* Gen plane rows the settled pause sheet shows (NES rows 9..29). */
#define INVENTORY_PAUSE_ROWS 21u

/* BG word for an ASCII glyph (A-Z, 0-9, space, ! ' , - . ? ") in a NES BG
 * sub-palette (0 white, 1 red on the pause sheet). */
unsigned short inventory_pause_glyph(char c, unsigned char sub_pal);
/* BG word for a raw NES BG tile in a sub-palette. */
unsigned short inventory_pause_tile(unsigned char nes_tile, unsigned char sub_pal);
/* Write 32 words to gen row `row` of the menu plane slot. */
void inventory_pause_write_row(unsigned char row, const unsigned short *cells);
/* Rewrite the NES rows holding the actions list (13..19). */
void inventory_pause_refresh_actions(void);
/* Rewrite the whole sheet (all rows + B/C labels); item sprites return on
 * the next active tick. */
void inventory_pause_redraw_sheet(void);
/* Publish the page sprite chain: HUD status markers plus, when show, the
 * position dot at NES coordinates (x,y). */
void inventory_pause_page_sprites(unsigned char show, unsigned short x, unsigned short y);
/* 1 when this pause opened over a dungeon (UW sheet). */
unsigned char inventory_pause_scene_uw(void);

#endif /* INVENTORY_PAUSE_INTERNAL_H */
