#ifndef INVENTORY_SPRITE_CHR_H
#define INVENTORY_SPRITE_CHR_H
/* 8 live-extracted subscreen item icon tiles (Genesis 4bpp, 32B each):
 * idx 0/1=recorder $24/$25, 2/3=candle $26/$27, 4/5=raft $6C/$6D,
 * 6/7=ladder $76/$77. Uploaded to VRAM on subscreen enter. */
extern const unsigned char k_inventory_sprite_chr[16][32];
#endif
