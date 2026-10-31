#ifndef CONTROLLER_ABI_H
#define CONTROLLER_ABI_H
/* Initialize detected adapter support before the title/file-select loop.
 * Reads return raw SGDK button masks sampled by its VBlank polling. */
void controller_party_init(void);
unsigned short controller_read_party(unsigned char player);
#endif
