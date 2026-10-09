/* Native credits presentation. The mode owns timing; this owns the plane. */
#ifndef ENDING_RENDER_H
#define ENDING_RENDER_H
void ending_render_begin(void);
void ending_render_scroll(unsigned short pixels);
void ending_render_write_row(unsigned short virtual_row, unsigned char nt_row);
unsigned char ending_render_record(unsigned char hi, unsigned char lo,
    unsigned char ctrl, unsigned char count, const unsigned char *src,
    unsigned char src_off, unsigned char src_end);
void ending_render_reset(void);
#endif
